# ADS1118 ADC with support for thermocouples
#
# Copyright (C) 2023 Christoph Nelles <klipper@evilazrael.de>
#
# This file may be distributed under the terms of the GNU GPLv3 license.

import math
import logging
from . import bus

REPORT_TIME = 0.600

def log(message):
    logging.info("ads1118 " + message.__str__())

C_TO_V_COEFFICIENTS_SUB_ZERO = [
    0.0,
    0.394501280250e-1,
    0.236223735980e-4,
    -0.328589067840e-6,
    -0.499048287770e-8,
    -0.675090591730e-10,
    -0.574103274280e-12,
    -0.310888728940e-14,
    -0.104516093650e-16,
    -0.198892668780e-19,
    -0.163226974860e-22,
]

C_TO_V_COEFFICIENTS_ABOVE_ZERO = [
    -0.176004136860e-1,
    0.389212049750e-1,
    0.185587700320e-4,
    -0.994575928740e-7,
    0.318409457190e-9,
    -0.560728448890e-12,
    0.560750590590e-15,
    -0.320207200030e-18,
    0.971511471520e-22,
    -0.121047212750e-25,
]

C_TO_V_EXPONENTIALS = [0.1185976, -0.118343200000e-3, 0.126968600000e3]

V_TO_C_SUB_ZERO = [
    0.0,
    2.5173462e1,
    -1.1662878,
    -1.0833638,
    -8.9773540e-1,
    -3.7342377e-1,
    -8.6632643e-2,
    -1.0450598e-2,
    -5.1920577e-4,
]

V_TO_C_SUB_500 = [
    0.0,
    2.508355e1,
    7.860106e-2,
    -2.503131e-1,
    8.315270e-2,
    -1.228034e-2,
    9.804036e-4,
    -4.413030e-5,
    1.057734e-6,
    -1.052755e-8,
]

V_TO_C_ABOVE_500 = [
    -1.318058e2,
    4.830222e1,
    -1.646031,
    5.464731e-2,
    -9.650715e-4,
    8.802193e-6,
    -3.110810e-8,
]

def celsius_to_microvolt(temp):
    sum = 0.0
    if temp <= 0.0:
        for i in range(0, len(C_TO_V_COEFFICIENTS_SUB_ZERO)):
            sum += C_TO_V_COEFFICIENTS_SUB_ZERO[i] * (temp**i)
    else:
        sum = C_TO_V_EXPONENTIALS[0] * math.exp(
            C_TO_V_EXPONENTIALS[1] * (temp - C_TO_V_EXPONENTIALS[2]) ** 2)
        for i in range(0, len(C_TO_V_COEFFICIENTS_ABOVE_ZERO)):
            sum += C_TO_V_COEFFICIENTS_ABOVE_ZERO[i] * (temp ** i)
    return sum

# microvolts
def microvolt_to_celsius(voltage):
    coeffs = []
    if voltage < 0:
        coeffs = V_TO_C_SUB_ZERO
    elif voltage < 500:
        coeffs = V_TO_C_SUB_500
    else:
        coeffs = V_TO_C_ABOVE_500
    sum = 0.0
    for i in range(0, len(coeffs)):
        sum += coeffs[i] * voltage ** i
    return sum

class ADS1118(object):
    def __init__(self, config):
        self.sensors = [{'cb': []}]
        self.printer = config.get_printer()
        self._reactor = self.printer.get_reactor()
        self.name = config.get_name().split()[1]
        self.data_rate = config.getint("data_rate", 0b100, 0b000, 0b111)

        spi_mode = 1
        self.spi = bus.MCU_SPI_from_config(config, spi_mode)

        self.mcu = mcu = self.spi.get_mcu()
        self.oid = oid = mcu.create_oid()

        # Reader chip configuration
        mcu.register_response(self._handle_spi_response, "ads1118_result", oid)
        mcu.register_config_callback(self._build_config)

    def add_sensor(self, params):
        config = params.getsection(params.section)
        mux = int(config.get("ads1118_mux"), 0)
        pga = int(config.get("ads1118_pga"), 0)
        dr = int(config.get("ads1118_dr"), 0)
        sensor = ADS1118_THERMOCOUPLE_K(self)
        self._add_sensor(mux, pga, dr, sensor.update_readings)

        return sensor

    def add_temp_sensor(self, _params):
        sensor = ADS1118_INTERNAL_SENSOR(self)
        self.sensors[0]['cb'].append(sensor.update_readings)
        return sensor

    def _add_sensor(self, mux, pga, dr, cb):
        self.sensors.append({"mux": mux, 'pga': pga, 'dr': dr, 'cb': cb})

    def _build_config(self):
        self.mcu.add_config_cmd(
            "config_ads1118 oid=%u spi_oid=%u data_rate=%u response_interval=%u"
            % (self.oid, self.spi.get_oid(), self.data_rate, 200)
        )
        log(
            "config_ads1118 oid=%u spi_oid=%u data_rate=%u response_interval=%u"
            % (self.oid, self.spi.get_oid(), self.data_rate, 200)
        )

        for sensor in self.sensors[1:]:
            log(
                "add_sensor_ads1118 oid=%u mux=%u pga=%u dr=%u"
                % (self.oid, sensor['mux'], sensor['pga'], sensor['dr'])
            )
            self.mcu.add_config_cmd(
                "add_sensor_ads1118 oid=%u mux=%u pga=%u dr=%u"
                % (self.oid, sensor['mux'], sensor['pga'], sensor['dr'])
            )

        self._report_clock = self.mcu.seconds_to_clock(REPORT_TIME)

    def _handle_spi_response(self, params):
        temp = (params["temperature"] >> 2) * 0.03125
        measured_time = self._reactor.monotonic()
        print_time = self.mcu.estimated_print_time(measured_time)

        if params["sensor"] == 0:
            for cb in self.sensors[0]['cb']:
                cb(print_time, temp)
        else:
            uvalue = params["value"]
            self.sensors[params['sensor']]['cb'](print_time, temp, uvalue)


class ADS1118_INTERNAL_SENSOR:
    def __init__(self, parent):
        self.mcu = parent.mcu

    def setup_minmax(self, _min_temp, _max_temp):
        # has its own range
        pass
    # callback prototype def temperature_callback(self, read_time, temp):

    def setup_callback(self, cb):
        self.temperature_callback = cb
    # TODO effect of value here?

    def get_report_time_delta(self):
        return REPORT_TIME

    # TODO check for null
    def update_readings(self, report_time, adc_temp):
        self.temperature_callback(report_time, adc_temp)


class ADS1118_THERMOCOUPLE_K:
    def __init__(self, parent):
        self.mcu = parent.mcu

    def setup_minmax(self, min_temp, max_temp):
        self.min_temp = min_temp
        self.max_temp = max_temp
        pass

    def setup_callback(self, cb):
        self.temperature_callback = cb

    def get_report_time_delta(self):
        return 1.0

    # TODO: value interpretation part of pin setup
    def update_readings(self, report_time, _adc_temp, uvalue):
        # temp = (params["temperature"] >> 2) * 0.03125

        # TODO: fault detection/processing
        svalue = -((~uvalue & 0xFFFF) + 1) if uvalue & 0x8000 else uvalue
        voltage = svalue * 7.8125 / 1e3
        #It should be this formula, but if the ADS1118 is hotter, the result is always to hot
        # Open FFCP2, 25° ambient temperature, ADS1118 reports 33° which also pushes the extruder thermocouple to 33°C. Seems that 25°C is the real temperature. At least in this test case
        # Especially for the FFCP2 you have multiple thermal zones, the extruder, the print chamber, the mainboard compartment and the room... so, what is the reference for the cold junction?
        # need a colder room for testing ;-)
        #voltage = voltage + celsius_to_microvolts(adc_temp)
        voltage = voltage + celsius_to_microvolt(25)
        sensor_temp = microvolt_to_celsius(voltage)
        # log(
        #     "Temperature ADS1118 %i °C raw signed %i unsigned %u voltage %f Guessed temperature: %f °C "
        #     % (adc_temp, svalue, uvalue, voltage, sensor_temp)
        # )
        self.temperature_callback(report_time, sensor_temp )


def load_config_prefix(config):
    chip = ADS1118(config)
    pheaters = config.get_printer().load_object(config, "heaters")
    pheaters.add_sensor_factory(chip.name, chip.add_sensor)
    pheaters.add_sensor_factory(chip.name + "_temp", chip.add_temp_sensor)
    return chip