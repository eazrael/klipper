#!/usr/bin/env python3
# Disclaimer. This script is AI generated and it is incredible 0.o
"""
flash_firmware.py — Standalone firmware upload tool for the Snapmaker controller
                    via the HMI serial port (SACP protocol, command set 0xAD).

Implements the two-phase firmware update protocol as specified in SACP_Protocol_Specification.md
§12 (Command Set 0xAD):

  Phase 1 — Start Update (Screen → Machine, 0xAD/0x01)
    Screen sends the 256-byte firmware header wrapped in a BytesProp payload.
    Machine validates, enters update mode, and ACKs with ResponseStructure{result=0}.

  Phase 2 — Chunk Transfer (Machine → Screen, 0xAD/0x02)
    Machine requests firmware body chunks on demand:
      Request  (machine → screen):  { byteIndex: UInt32, maxBufSpace: UInt16 }
      Response (screen → machine):  { result=0, startPos: UInt32,
                                      packagePayload: BytesProp(chunk) }
    When byteIndex is past end of binary the screen responds with bare UInt8(0x01).

  Phase 3 — Notify Update Result (Machine → Screen, 0xAD/0x03)
    Machine sends UInt8(result).  Screen ACKs with UInt8(0x00).

Source references
    SACP_Protocol_Specification.md §12   — protocol definition
    snapmaker/event/event_update.h/.cpp  — machine-side command IDs and handler
    snapmaker/module/update.h/.cpp       — update_packet_info_t, validation, flash ops

Usage
    python3 tools/flash_firmware.py --port /dev/ttyUSB0 firmware.bin
    python3 tools/flash_firmware.py --port /dev/ttyUSB0 --force firmware.bin
    python3 tools/flash_firmware.py --port /dev/ttyUSB0 --klipper firmware.bin
"""

import argparse
import struct
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Callable, Dict, Optional, Tuple

try:
    import serial
except ImportError:
    raise SystemExit("pyserial is required: pip install pyserial")


# ---------------------------------------------------------------------------
# Klipper serial bootloader trigger
# ---------------------------------------------------------------------------

_KLIPPER_TRIGGER_MSG  = b"~ \x1c Request Serial Bootloader!! ~\n"
_KLIPPER_TRIGGER_BAUD = 250000
_KLIPPER_TRIGGER_WAIT = 3.0   # seconds to wait after sending


def klipper_trigger(port: str) -> None:
    """
    Send the Klipper serial bootloader request string at 250000 baud,
    then wait for the bootloader to initialise before continuing.
    """
    print(f"[KLIPPER] Sending bootloader trigger on {port} "
          f"@ {_KLIPPER_TRIGGER_BAUD} baud …")
    with serial.Serial(port, baudrate=_KLIPPER_TRIGGER_BAUD, timeout=1) as s:
        s.write(_KLIPPER_TRIGGER_MSG)
        s.write(b'\x00' * 128)
        s.flush()
    print(f"[KLIPPER] Waiting {_KLIPPER_TRIGGER_WAIT:.0f} s for bootloader …")
    time.sleep(_KLIPPER_TRIGGER_WAIT)


# ---------------------------------------------------------------------------
# SACP protocol constants
# ---------------------------------------------------------------------------

_SOF_H   = 0xAA
_SOF_L   = 0x55
_VERSION = 0x01

_ID_CONTROLLER   = 1
_ID_HMI          = 2

ATTR_REQ = 0   # request  (attribute field value)
ATTR_ACK = 1   # response (attribute field value)

CMD_SET_UPDATE = 0xAD

_PACK_PARSE_MAX_SIZE = 512

_HandlerKey = Tuple[int, int]   # (command_set, command_id)


# ---------------------------------------------------------------------------
# SACP checksum helpers
# ---------------------------------------------------------------------------

def _crc8(data: bytes) -> int:
    """CRC-8/SMBUS (poly=0x07, init=0x00, MSB-first) — covers header bytes [0..5]."""
    crc = 0x00
    for byte in data:
        for bit in range(7, -1, -1):
            b   = (byte >> bit) & 1
            c07 = (crc >> 7) & 1
            crc = (crc << 1) & 0xFF
            if c07 ^ b:
                crc ^= 0x07
    return crc


def _sacp_checksum(data: bytes) -> int:
    """16-bit one's-complement checksum — covers the payload region of each frame."""
    total = 0
    n = len(data)
    for i in range(0, n - 1, 2):
        total += (data[i] << 8) | data[i + 1]
    if n % 2:
        total += data[-1]
    while total > 0xFFFF:
        total = (total >> 16) + (total & 0xFFFF)
    return (~total) & 0xFFFF


# ---------------------------------------------------------------------------
# SACPClient — low-level SACP framing, receive loop, and dispatch
# ---------------------------------------------------------------------------

class SACPClient:
    """
    Low-level SACP client.

    Handles framing, checksums, and a background receive thread.
    Incoming frames are dispatched to callbacks registered with on().
    """

    def __init__(self, port: str, baudrate: int = 115200,
                 sender_id: int = _ID_HMI) -> None:
        self.port      = port
        self.baudrate  = baudrate
        self.sender_id = sender_id

        self._serial: Optional[serial.Serial] = None
        self._sequence  = 0
        self._seq_lock  = threading.Lock()
        self._running   = False
        self._recv_thread: Optional[threading.Thread] = None

        self._handlers: Dict[_HandlerKey, Callable] = {}
        self._default_handler: Optional[Callable] = None

        self._rx_buf = bytearray(_PACK_PARSE_MAX_SIZE)
        self._rx_len = 0

    # --- connection -------------------------------------------------------

    def connect(self) -> None:
        self._serial = serial.Serial(self.port, self.baudrate, timeout=0.05)
        self._running = True
        self._recv_thread = threading.Thread(
            target=self._recv_loop, name="sacp-recv", daemon=True)
        self._recv_thread.start()
        print(f"[SACP] connected to {self.port} @ {self.baudrate} baud")

    def disconnect(self) -> None:
        self._running = False
        if self._recv_thread:
            self._recv_thread.join(timeout=1.0)
        if self._serial and self._serial.is_open:
            self._serial.close()
        print("[SACP] disconnected")

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *_):
        self.disconnect()

    # --- handler registration ---------------------------------------------

    def on(self, command_set: int, command_id: int, callback: Callable) -> None:
        self._handlers[(command_set, command_id)] = callback

    def on_unhandled(self, callback: Callable) -> None:
        self._default_handler = callback

    # --- sending ----------------------------------------------------------

    def send(self, command_set: int, command_id: int,
             data: bytes = b'',
             attr: int = ATTR_REQ,
             recever_id: int = _ID_CONTROLLER,
             sequence: Optional[int] = None) -> int:
        """Build and transmit one SACP frame. Returns the sequence number used."""
        if self._serial is None or not self._serial.is_open:
            raise RuntimeError("Not connected — call connect() first")
        if sequence is None:
            sequence = self._next_sequence()
        frame = self._package(recever_id, attr, sequence, command_set, command_id, data)
        self._serial.write(frame)
        return sequence

    # --- framing ----------------------------------------------------------

    def _next_sequence(self) -> int:
        with self._seq_lock:
            seq = self._sequence
            self._sequence = (self._sequence + 1) & 0xFFFF
            return seq

    def _package(self, recever_id: int, attr: int, sequence: int,
                 command_set: int, command_id: int, payload: bytes) -> bytes:
        data_len = len(payload) + 8
        hdr = struct.pack('<BBHBB', _SOF_H, _SOF_L, data_len, _VERSION, recever_id)
        crc = _crc8(hdr)
        body = struct.pack('<BBHBB', self.sender_id, attr, sequence,
                           command_set, command_id)
        checksum_region = body + bytes(payload)
        cksum = _sacp_checksum(checksum_region)
        return hdr + bytes([crc]) + checksum_region + struct.pack('<H', cksum)

    # --- parsing ----------------------------------------------------------

    def _parse_byte(self, byte: int) -> Optional[dict]:
        buf = self._rx_buf
        if buf[0] != _SOF_H:
            self._rx_len = 0
        n = self._rx_len

        if n == 0:
            if byte == _SOF_H:
                buf[0] = byte
                self._rx_len = 1
        elif n == 1:
            if byte == _SOF_L:
                buf[1] = byte
                self._rx_len = 2
            else:
                self._rx_len = 0
        else:
            if n < _PACK_PARSE_MAX_SIZE:
                buf[n] = byte
                self._rx_len += 1
                n = self._rx_len
            if n < 7:
                return None
            if n == 7:
                if _crc8(bytes(buf[0:6])) != buf[6]:
                    self._rx_len = 0
                return None
            data_len  = buf[2] | (buf[3] << 8)
            total_len = data_len + 7
            if n < total_len:
                return None
            if n == total_len:
                expected = _sacp_checksum(bytes(buf[7:total_len - 2]))
                stored   = buf[total_len - 2] | (buf[total_len - 1] << 8)
                self._rx_len = 0
                if expected == stored:
                    return self._decode_frame(bytes(buf[:total_len]))
                return None
            self._rx_len = 0
        return None

    def _decode_frame(self, raw: bytes) -> dict:
        data_len = (raw[2] | (raw[3] << 8)) - 8
        return {
            'sender_id':   raw[7],
            'attr':        raw[8],
            'sequence':    raw[9] | (raw[10] << 8),
            'command_set': raw[11],
            'command_id':  raw[12],
            'data':        raw[13:13 + data_len],
        }

    # --- receive loop -----------------------------------------------------

    def _recv_loop(self) -> None:
        while self._running:
            try:
                chunk = self._serial.read(64)
            except serial.SerialException:
                break
            for byte in chunk:
                frame = self._parse_byte(byte)
                if frame is not None:
                    self._dispatch(frame)

    def _dispatch(self, frame: dict) -> None:
        key     = (frame['command_set'], frame['command_id'])
        handler = self._handlers.get(key) or self._default_handler
        if handler:
            try:
                handler(frame)
            except Exception as exc:
                print(f"[SACP] handler error for {key}: {exc}")


# ---------------------------------------------------------------------------
# Update command IDs  (event_update.h / SACP spec §11 command set 0xAD)
# ---------------------------------------------------------------------------

UPDATE_ID_REQ_UPDATE      = 0x01   # Screen → Machine: start update
UPDATE_ID_REQ_UPDATE_PACK = 0x02   # Machine → Screen: request chunk
UPDATE_ID_REPORT_STATUS   = 0x03   # Machine → Screen: notify result


# ---------------------------------------------------------------------------
# Flash layout constants  (Marlin/src/core/macros.h)
# ---------------------------------------------------------------------------

FLASH_BASE      = 0x08000000
BOOT_CODE_SIZE  = 10 * 1024        # 10 KB — bootloader region
APP_FLASH_START = FLASH_BASE + BOOT_CODE_SIZE   # 0x08002800
APP_FLASH_PAGE  = 2 * 1024         # 2 KB pages (app region)


# ---------------------------------------------------------------------------
# update_packet_info_t constants  (update.h / update.cpp)
# ---------------------------------------------------------------------------

UPDATE_TYPE_APP      = 3        # type field must be exactly 3
UPDATE_STATUS_NORMAL = 0xAA05  # UPDATE_STATUS_APP_NORMAL

_FILE_FLAG = b'snapmaker update.bin\x00'   # exactly 21 bytes
assert len(_FILE_FLAG) == 21

# The spec sends a 256-byte header in Phase 1 (BytesProp).
# update_packet_info_t is 101 bytes; the rest is zero-padded.
_HEADER_SIZE = 256


# ---------------------------------------------------------------------------
# Firmware checksum helpers  (update.cpp — update_calc_checksum)
# ---------------------------------------------------------------------------

def _app_checksum(data: bytes) -> int:
    """32-bit one's-complement checksum used in update_packet_info_t.app_checknum."""
    checksum = 0
    n = len(data)
    for j in range(0, (n // 2) * 2, 2):
        checksum += (data[j] << 8) | data[j + 1]
    if n % 2:
        checksum += data[-1]
    return (~checksum) & 0xFFFFFFFF


def _header_checksum(data: bytes) -> int:
    """32-bit one's-complement checksum used in update_packet_info_t.pack_head_checknum."""
    total = 0
    n = len(data)
    for i in range(0, n - 1, 2):
        total += (data[i] << 8) | data[i + 1]
    if n % 2:
        total += data[-1]
    return (~total) & 0xFFFFFFFF


# ---------------------------------------------------------------------------
# update_packet_info_t  (update.h, #pragma pack(1), 101 bytes total)
# ---------------------------------------------------------------------------
# Offset  Size  Field                 Type
#   0      21   file_flag             uint8[21]
#  21       1   pack_version          uint8
#  22       2   type                  uint16  (must be 3)
#  24       1   is_force_update       uint8
#  25       2   start_id              uint16
#  27       2   end_id                uint16
#  29      32   app_version           uint8[32]
#  61      20   pack_time             uint8[20]
#  81       2   status_flag           uint16
#  83       4   app_length            uint32
#  87       4   app_checknum          uint32
#  91       4   app_flash_start_addr  uint32
#  95       1   usart_num             uint8
#  96       1   receiver_id           uint8
#  97       4   pack_head_checknum    uint32   ← checksum of bytes [0..96]

_INFO_FMT_BODY = '<21sBHBHH32s20sHIIIBB'   # 97 bytes
_INFO_FMT_FULL = _INFO_FMT_BODY + 'I'       # 101 bytes

assert struct.calcsize(_INFO_FMT_BODY) == 97
assert struct.calcsize(_INFO_FMT_FULL) == 101


def build_update_info(binary: bytes, version: str,
                      flash_addr: int = APP_FLASH_START,
                      force: bool = False) -> bytes:
    """Build a packed update_packet_info_t (101 bytes) for a raw firmware binary."""
    if flash_addr % APP_FLASH_PAGE:
        raise ValueError(
            f"flash_addr 0x{flash_addr:08X} is not "
            f"{APP_FLASH_PAGE // 1024} KB aligned"
        )
    if flash_addr < FLASH_BASE + BOOT_CODE_SIZE:
        raise ValueError(
            f"flash_addr 0x{flash_addr:08X} is inside the bootloader region "
            f"(must be ≥ 0x{FLASH_BASE + BOOT_CODE_SIZE:08X})"
        )

    version_bytes = version.encode('ascii')[:32].ljust(32, b'\x00')
    pack_time     = (datetime.now().strftime('%Y%m%dT%H%M%S')
                     .encode('ascii').ljust(20, b'\x00'))

    body = struct.pack(
        _INFO_FMT_BODY,
        _FILE_FLAG,
        0x01,                          # pack_version
        UPDATE_TYPE_APP,               # type = 3
        1 if force else 0,             # is_force_update
        0,                             # start_id
        0,                             # end_id
        version_bytes,
        pack_time,
        UPDATE_STATUS_NORMAL,
        len(binary),                   # app_length
        _app_checksum(binary),         # app_checknum
        flash_addr,                    # app_flash_start_addr
        0,                             # usart_num
        0,                             # receiver_id
    )
    return body + struct.pack('<I', _header_checksum(body))


# ---------------------------------------------------------------------------
# Debug helpers
# ---------------------------------------------------------------------------

_CMD_NAMES = {
    UPDATE_ID_REQ_UPDATE:      'UPDATE/START     ',
    UPDATE_ID_REQ_UPDATE_PACK: 'UPDATE/CHUNK     ',
    UPDATE_ID_REPORT_STATUS:   'UPDATE/STATUS    ',
}


def _ts() -> str:
    return datetime.now().strftime('%H:%M:%S.%f')[:-3]


# ---------------------------------------------------------------------------
# FlashClient
# ---------------------------------------------------------------------------

class FlashClient(SACPClient):
    """
    Firmware upload client for the Snapmaker controller.

    Implements the screen-side firmware update protocol from SACP spec §12:
      1. Screen sends firmware header in 0xAD/0x01 (BytesProp, 256 bytes).
      2. Machine requests body chunks via 0xAD/0x02 REQs; screen serves each one.
      3. Machine notifies result via 0xAD/0x03 REQ; screen ACKs with 0x00.
    """

    def __init__(self, port: str, baudrate: int = 115200,
                 debug: bool = False) -> None:
        super().__init__(port, baudrate, sender_id=_ID_HMI)
        self.debug = debug

        self._update_ack    = threading.Event()
        self._update_result: Optional[int] = None

        self._status_done   = threading.Event()
        self._status_result: Optional[int] = None

        self._binary: Optional[bytes] = None
        self._first_req = threading.Event()

        self.on(CMD_SET_UPDATE, UPDATE_ID_REQ_UPDATE,      self._on_update_ack)
        self.on(CMD_SET_UPDATE, UPDATE_ID_REQ_UPDATE_PACK, self._on_chunk_req)
        self.on(CMD_SET_UPDATE, UPDATE_ID_REPORT_STATUS,   self._on_status)

    # --- debug logging ----------------------------------------------------

    def _dbg_tx(self, seq: int, cmd_id: int, n_bytes: int,
                attr: int = ATTR_REQ, extra: str = '') -> None:
        if not self.debug:
            return
        name     = _CMD_NAMES.get(cmd_id, f'0x{CMD_SET_UPDATE:02X}/0x{cmd_id:02X}')
        attr_str = 'ACK' if attr == ATTR_ACK else 'REQ'
        tail     = f'  | {extra}' if extra else ''
        print(f'[DBG TX] {_ts()}  seq={seq:04d}  {attr_str}  {name}  {n_bytes:4d} B{tail}')

    def _dbg_rx(self, frame: dict, extra: str = '') -> None:
        if not self.debug:
            return
        cmd_id = frame['command_id']
        name   = _CMD_NAMES.get(cmd_id, f'0x{frame["command_set"]:02X}/0x{cmd_id:02X}')
        attr   = 'ACK' if frame['attr'] == ATTR_ACK else 'REQ'
        tail   = f'  | {extra}' if extra else ''
        print(f'[DBG RX] {_ts()}  seq={frame["sequence"]:04d}  {attr}  '
              f'{name}  {len(frame["data"]):4d} B{tail}')

    # --- callbacks --------------------------------------------------------

    def _on_update_ack(self, frame: dict) -> None:
        """Phase 1 response: machine ACKs the firmware header."""
        if frame['attr'] != ATTR_ACK:
            return
        status = frame['data'][0] if frame['data'] else 0xFF
        label  = 'OK' if status == 0 else f'FAIL (0x{status:02X})'
        if self.debug:
            self._dbg_rx(frame, f'result=0x{status:02X} ({label})')
        else:
            print(f'[FLASH] Phase 1 ACK → {label}')
        self._update_result = status
        self._update_ack.set()

    def _on_chunk_req(self, frame: dict) -> None:
        """
        Phase 2: machine requests a firmware body chunk (spec §12.4–12.5).

        Request payload:  { byteIndex: UInt32-LE, maxBufSpace: UInt16-LE }
        Response payload: { result=0x00, startPos: UInt32-LE,
                            packagePayload: BytesProp(chunk) }
        EOF response:     bare UInt8(0x01)
        """
        if frame['attr'] != ATTR_REQ:
            return

        self._first_req.set()

        binary = self._binary
        if binary is None:
            return

        if len(frame['data']) < 6:
            self._dbg_rx(frame, f'malformed — data={frame["data"].hex()}')
            return

        byte_index, max_buf = struct.unpack_from('<IH', frame['data'])
        total = len(binary)

        self._dbg_rx(frame, f'byteIndex={byte_index}  maxBufSpace={max_buf}')

        if byte_index >= total:
            payload = b'\x01'
            seq = self.send(CMD_SET_UPDATE, UPDATE_ID_REQ_UPDATE_PACK, payload,
                            attr=ATTR_ACK, sequence=frame['sequence'],
                            recever_id=frame['sender_id'])
            self._dbg_tx(seq, UPDATE_ID_REQ_UPDATE_PACK, 1, ATTR_ACK, 'EOF')
            if not self.debug:
                print()
            return

        chunk   = binary[byte_index: byte_index + min(max_buf, total - byte_index)]
        payload = (b'\x00'
                   + struct.pack('<I', byte_index)
                   + struct.pack('<H', len(chunk))
                   + chunk)
        seq = self.send(CMD_SET_UPDATE, UPDATE_ID_REQ_UPDATE_PACK, payload,
                        attr=ATTR_ACK, sequence=frame['sequence'],
                        recever_id=frame['sender_id'])

        served = byte_index + len(chunk)
        if self.debug:
            self._dbg_tx(seq, UPDATE_ID_REQ_UPDATE_PACK, len(payload), ATTR_ACK,
                         f'byteIndex={byte_index}  chunk={len(chunk)} B  '
                         f'({served}/{total})')
        else:
            pct = served / total * 100
            bar = '#' * int(pct / 2)
            print(f'\r  [{bar:<50}] {pct:5.1f}%  '
                  f'{served:>{len(str(total))}}/{total} bytes',
                  end='', flush=True)

    def _on_status(self, frame: dict) -> None:
        """Phase 3: machine notifies update result; screen ACKs with 0x00."""
        if frame['attr'] != ATTR_REQ:
            return
        status = frame['data'][0] if frame['data'] else 0xFF
        label  = ('success'             if status == 0
                  else 'skipped (no module)' if status == 10
                  else f'error (0x{status:02X})')

        seq = self.send(CMD_SET_UPDATE, UPDATE_ID_REPORT_STATUS, b'\x00',
                        attr=ATTR_ACK, sequence=frame['sequence'],
                        recever_id=frame['sender_id'])

        if self.debug:
            self._dbg_rx(frame, f'result=0x{status:02X} ({label})')
            self._dbg_tx(seq, UPDATE_ID_REPORT_STATUS, 1, ATTR_ACK)
        else:
            print(f'[FLASH] Update result → {label}')

        self._status_result = status
        self._status_done.set()

    # --- public API -------------------------------------------------------

    def flash_firmware(self,
                       image_path: str,
                       flash_addr: int = APP_FLASH_START,
                       force: bool = False,
                       bootloader_wait: float = 10.0,
                       completion_timeout: float = 60.0) -> bool:
        """
        Flash a raw firmware binary (.bin) to the controller.

        Returns True on success, False on any error.
        """
        # --- Read binary --------------------------------------------------
        print(f"[FLASH] Reading {image_path}")
        try:
            binary = Path(image_path).read_bytes()
        except OSError as exc:
            print(f"[FLASH] ERROR: {exc}")
            return False

        version  = Path(image_path).stem
        checksum = _app_checksum(binary)
        print(f"[FLASH]   version : {version}")
        print(f"[FLASH]   binary  : {len(binary):,} bytes")
        print(f"[FLASH]   checksum: 0x{checksum:08X}")
        print(f"[FLASH]   target  : 0x{flash_addr:08X}")

        # --- Build 256-byte header ----------------------------------------
        try:
            info_bytes = build_update_info(binary, version, flash_addr, force)
        except ValueError as exc:
            print(f"[FLASH] ERROR: {exc}")
            return False

        header = info_bytes + b'\x00' * (_HEADER_SIZE - len(info_bytes))

        # --- Pre-sync: 512 raw zero bytes, one at a time ------------------
        print("[FLASH] Sending sync bytes …")
        for _ in range(512):
            self._serial.write(b'\x00')
            self._serial.flush()
        time.sleep(1.0)

        # --- Phase 1: send firmware header (BytesProp) --------------------
        print("[FLASH] Phase 1 — sending firmware header …")
        self._update_ack.clear()
        self._update_result = None

        payload = struct.pack('<H', _HEADER_SIZE) + header
        seq = self.send(CMD_SET_UPDATE, UPDATE_ID_REQ_UPDATE, payload)
        self._dbg_tx(seq, UPDATE_ID_REQ_UPDATE, len(payload), ATTR_REQ,
                     f'ver={version!r}  size={len(binary):,}  '
                     f'addr=0x{flash_addr:08X}  cksum=0x{checksum:08X}  '
                     f'force={int(force)}')

        if not self._update_ack.wait(timeout=10.0):
            print("[FLASH] ERROR: no ACK from machine (timeout after 10 s)")
            return False

        if self._update_result != 0:
            print("[FLASH] ERROR: machine rejected the update header")
            _explain_errcode(self._update_result)
            return False

        # --- Phase 2: serve chunk requests from machine -------------------
        self._binary     = binary
        self._first_req.clear()
        self._status_done.clear()
        self._status_result = None

        print(f"[FLASH] Phase 2 — waiting up to {bootloader_wait:.1f} s for "
              f"first chunk request …")

        if not self._first_req.wait(timeout=bootloader_wait):
            print("[FLASH] ERROR: machine did not request any chunks "
                  f"within {bootloader_wait:.1f} s")
            self._binary = None
            return False

        if not self.debug:
            print(f"[FLASH] Serving {len(binary):,} bytes on demand …")

        completed = self._status_done.wait(timeout=completion_timeout)
        self._binary = None

        if not completed:
            print(
                "\n[FLASH] WARNING: no update result received from machine "
                f"within {completion_timeout:.0f} s.\n"
                "        The update may have succeeded; power-cycle and check "
                "the firmware version."
            )
            return False

        if self._status_result == 0:
            print("[FLASH] Firmware update successful. Controller is rebooting.")
            return True

        if self._status_result == 10:
            print("[FLASH] No module detected — update skipped.")
            return False

        print(f"[FLASH] ERROR: update failed (result=0x{self._status_result:02X})")
        return False


# ---------------------------------------------------------------------------
# Error code descriptions
# ---------------------------------------------------------------------------

_ERRCODES = {
    0x00: "E_SUCCESS",
    0x01: "E_IN_PROGRESS",
    0x05: "E_COMMAND_SET",
    0x06: "E_COMMAND_ID",
    0x07: "E_PARAM — header checksum, address, or type field failed validation",
    0x0B: "E_BUSY",
    0x0C: "E_HARDWARE",
    0x0D: "E_INVALID_STATE",
}


def _explain_errcode(code: int) -> None:
    desc = _ERRCODES.get(code, f"unknown code 0x{code:02X}")
    print(f"         error: {desc}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Flash Snapmaker controller firmware via the HMI serial port "
                    "(SACP protocol, command set 0xAD)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 tools/flash_firmware.py --port /dev/ttyUSB0 firmware.bin
  python3 tools/flash_firmware.py --port COM3 --force firmware.bin
  python3 tools/flash_firmware.py --port /dev/ttyUSB0 --klipper klipper.bin
""",
    )
    p.add_argument("image",
                   help="Raw firmware .bin file")
    p.add_argument("--port", required=True,
                   help="Serial port (e.g. /dev/ttyUSB0 or COM3)")
    p.add_argument("--baud", type=int, default=115200,
                   help="Baud rate (default: 115200)")
    p.add_argument("--flash-addr", type=lambda x: int(x, 0),
                   default=APP_FLASH_START,
                   help=f"App flash start address (default: 0x{APP_FLASH_START:08X})")
    p.add_argument("--force", action="store_true",
                   help="Set is_force_update=1 in the update header")
    p.add_argument("--bootloader-wait", type=float, default=5.0,
                   help="Seconds to wait for the first chunk request (default: 5)")
    p.add_argument("--completion-timeout", type=float, default=60.0,
                   help="Seconds to wait for the update result notification (default: 60)")
    p.add_argument("--klipper", action="store_true",
                   help=f"Send Klipper serial bootloader trigger at "
                        f"{_KLIPPER_TRIGGER_BAUD} baud before flashing")
    p.add_argument("--debug", action="store_true",
                   help="Log every TX/RX packet header instead of a progress bar")
    return p


def main() -> None:
    args = _build_parser().parse_args()

    if args.klipper:
        klipper_trigger(args.port)

    with FlashClient(port=args.port, baudrate=args.baud, debug=args.debug) as client:
        ok = client.flash_firmware(
            image_path=args.image,
            flash_addr=args.flash_addr,
            force=args.force,
            bootloader_wait=args.bootloader_wait,
            completion_timeout=args.completion_timeout,
        )

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
