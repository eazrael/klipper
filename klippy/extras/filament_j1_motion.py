# Snapmaker J1 analog filament-motion (runout/jam/grind) sensor
#
# The J1's filament sensor is not a switch: it is an analog motion encoder
# whose ADC voltage ramps as filament moves and wraps around a "dead zone".
# Detection is meaningful only while extruding: if the commanded filament
# advances but the ADC does not change, the filament is stalled (runout,
# jam, tangle, or extruder-gear grinding).
#
# This replicates the stock Marlin (Snapmaker_J1 fork) filament_sensor.cpp
# algorithm as a Klipper host module. See
# filament-motion-sensor-klipper-port.md for the full spec.
#
# Copyright (C) 2026
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import logging
from . import filament_switch_sensor

# ADC hardware sampling: oversample in the MCU (the stock firmware's 5-sample
# average), and report at a fixed cadence to the host.
ADC_SAMPLE_TIME = 0.001
ADC_SAMPLE_COUNT = 8
ADC_REPORT_TIME = 0.050

# How often the host re-evaluates the extruder position / window state.
CHECK_TIMEOUT = 0.100

# Extra host-side smoothing: average this many recent ADC reports when
# latching a window-edge reading (belt-and-braces on top of ADC_SAMPLE_COUNT).
EDGE_AVG_SAMPLES = 5

# 12-bit ADC full scale (0..4095 spans 0..3.3 V). Klipper hands ADC callbacks
# a normalized 0.0..1.0 float, so we scale to counts to match the firmware's
# thresholds directly.
ADC_MAX_COUNT = 4095.0

# Per hardware-version constants, in 12-bit counts, from the stock firmware.
#   change_threshold: min |end-start| over a window to count as "moved"
#   deadzone(counts) : reading is in the pot's wrap band -> suppress fail path
HW_VERSION_PARAMS = {
    1: {'change_threshold': 8,  'deadzone': lambda c: c > 1433},
    2: {'change_threshold': 15, 'deadzone': lambda c: c < 60 or c > 4060},
}


class FilamentJ1Motion:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.name = config.get_name().split()[-1]
        self.reactor = self.printer.get_reactor()

        # --- config: hardware version selects the default thresholds ---
        # 'auto' would read HW_VERSION_PIN (PB1); how the firmware reads that
        # pin (digital vs analog divider) is not yet pinned down, so 'auto'
        # currently falls back to v2 (the safer rails-deadzone profile) with a
        # warning. Set hw_version explicitly to 1 or 2 to be sure.
        hw_version = config.get('hw_version', 'auto')
        if hw_version == 'auto':
            logging.warning("filament_j1_motion %s: hw_version=auto is not yet"
                            " implemented; defaulting to v2. Set hw_version:1"
                            " or 2 explicitly.", self.name)
            self.hw_version = 2
        else:
            self.hw_version = config.getint('hw_version', 2, minval=1, maxval=2)
        params = HW_VERSION_PARAMS[self.hw_version]

        # --- config: algorithm tuning (defaults derived from hw_version) ---
        self.change_threshold = config.getfloat(
                'change_threshold', params['change_threshold'], above=0.)
        self._in_deadzone = params['deadzone']
        # mm of commanded filament advance per short evaluation window.
        self.detection_window = config.getfloat(
                'detection_window', 3.0, above=0.)
        # consecutive failing short windows required to declare a fault.
        self.trigger_windows = config.getint(
                'trigger_windows', 3, minval=1)
        # longer backstop window (mm): catches a real stall that happens to
        # park the pot inside the dead-zone band, which the short window
        # suppresses. Ignores the dead-zone guard.
        self.deadzone_window = config.getfloat(
                'deadzone_window', 15.0, above=0.)

        # --- ADC input on the analog sensor pin (PA4 / PA0) ---
        ppins = self.printer.lookup_object('pins')
        self.mcu_adc = ppins.setup_pin('adc', config.get('adc_pin'))
        self.mcu_adc.setup_adc_sample(ADC_SAMPLE_TIME, ADC_SAMPLE_COUNT)
        self.mcu_adc.setup_adc_callback(ADC_REPORT_TIME, self._adc_callback)
        query_adc = self.printer.load_object(config, 'query_adc')
        query_adc.register_adc('filament_j1_motion ' + self.name, self.mcu_adc)

        # --- reuse Klipper's runout plumbing (pause + gcode + mux cmds) ---
        self.extruder_name = config.get('extruder')
        self.runout_helper = filament_switch_sensor.RunoutHelper(config)
        self.extruder = None
        self.estimated_print_time = None

        # --- internal state ---
        self._adc_history = []          # recent ADC counts for edge averaging
        self._last_adc = 0.0            # most recent averaged ADC (counts)
        self._filament_present = True   # what we last reported to RunoutHelper
        self._reset_windows()

        # --- events / timer (only runs while printing, like EncoderSensor) ---
        self.printer.register_event_handler('klippy:ready', self._handle_ready)
        self.printer.register_event_handler('idle_timeout:printing',
                self._handle_printing)
        self.printer.register_event_handler('idle_timeout:ready',
                self._handle_not_printing)
        self.printer.register_event_handler('idle_timeout:idle',
                self._handle_not_printing)

    def _reset_windows(self):
        # (Re)arm both detectors starting from the next observed position.
        self._window_start_pos = None
        self._window_start_adc = None
        self._long_start_pos = None
        self._long_start_adc = None
        self._fail_windows = 0

    # ---- ADC ----------------------------------------------------------
    def _adc_callback(self, read_time, read_value):
        counts = read_value * ADC_MAX_COUNT
        hist = self._adc_history
        hist.append(counts)
        if len(hist) > EDGE_AVG_SAMPLES:
            del hist[0]
        self._last_adc = sum(hist) / len(hist)

    # ---- lifecycle ----------------------------------------------------
    def _handle_ready(self):
        self.extruder = self.printer.lookup_object(self.extruder_name)
        self.estimated_print_time = (
                self.printer.lookup_object('mcu').estimated_print_time)
        self._timer = self.reactor.register_timer(self._check_event)

    def _handle_printing(self, print_time):
        # Re-arm on every print start/resume so a prior fault can trigger again.
        self._reset_windows()
        self._filament_present = True
        self.reactor.update_timer(self._timer, self.reactor.NOW)

    def _handle_not_printing(self, print_time):
        self.reactor.update_timer(self._timer, self.reactor.NEVER)

    def _get_extruder_pos(self, eventtime):
        print_time = self.estimated_print_time(eventtime)
        return self.extruder.find_past_position(print_time)

    # ---- detection core ----------------------------------------------
    def _check_event(self, eventtime):
        pos = self._get_extruder_pos(eventtime)
        adc = self._last_adc

        # First tick after (re)arming: latch both window origins and prime
        # RunoutHelper to "present" so the later present->absent transition
        # actually fires (its note_filament_present ignores no-op repeats, and
        # it initializes to absent).
        if self._window_start_pos is None:
            self._window_start_pos = pos
            self._window_start_adc = adc
            self._long_start_pos = pos
            self._long_start_adc = adc
            self.runout_helper.note_filament_present(eventtime, True)
            return eventtime + CHECK_TIMEOUT

        faulted = False

        # -- short window: consecutive-fail runout detection --
        if pos - self._window_start_pos >= self.detection_window:
            moved = abs(adc - self._window_start_adc)
            # Suppress the fail path inside the pot's wrap/dead-zone band.
            suppressed = (self._in_deadzone(adc)
                          or self._in_deadzone(self._window_start_adc))
            if moved < self.change_threshold and not suppressed:
                self._fail_windows += 1
            else:
                self._fail_windows = 0
            self._window_start_pos = pos
            self._window_start_adc = adc
            if self._fail_windows >= self.trigger_windows:
                faulted = True

        # -- long backstop window: dead-zone-agnostic stall confirmation --
        if pos - self._long_start_pos >= self.deadzone_window:
            if abs(adc - self._long_start_adc) < self.change_threshold:
                faulted = True
            self._long_start_pos = pos
            self._long_start_adc = adc

        if faulted and self._filament_present:
            logging.info("filament_j1_motion %s: stall detected at e-pos %.3f"
                         " (adc %.0f)", self.name, pos, adc)
            self._filament_present = False
            self._fail_windows = 0
            # RunoutHelper decides pause/gcode based on printing state.
            self.runout_helper.note_filament_present(eventtime, False)

        return eventtime + CHECK_TIMEOUT

    # ---- status -------------------------------------------------------
    def get_status(self, eventtime):
        status = dict(self.runout_helper.get_status(eventtime))
        # Extra fields help empirically map the sensor (log ADC vs mm).
        status['adc'] = round(self._last_adc, 1)
        status['hw_version'] = self.hw_version
        return status


def load_config_prefix(config):
    return FilamentJ1Motion(config)
