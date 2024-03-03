import math
import logging
from . import bus

REPORT_TIME = 0.300
MAX_INVALID_COUNT = 3


def log(message):
    logging.warning("ads1118 " + message.__str__())


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

# microvolts


def celsius_to_volt(temp):
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
def volt_to_celsius(voltage):
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


class SensorBase:
    def __init__(self, config, chip_type, config_cmd=None, spi_mode=1):
        log("In Sensorbase")
        self.printer = config.get_printer()
        self.data_rate = 128
        self.chip_type = chip_type
        self._callback = None
        self.min_sample_value = self.max_sample_value = 0
        self._report_clock = 0
        self.spi = bus.MCU_SPI_from_config(config, spi_mode)
        log("spi " + self.spi.__dict__.__str__())
        if config_cmd is not None:
            self.spi.spi_send(config_cmd)
        self.mcu = mcu = self.spi.get_mcu()
        # Reader chip configuration
        self.oid = oid = mcu.create_oid()
        log("A self " + self.__dict__.__str__())
        mcu.register_response(self._handle_spi_response, "ads1118_result", oid)
        mcu.register_config_callback(self._build_config)

    # def setup_minmax(self, min_temp, max_temp):
    #     adc_range = [self.calc_adc(min_temp), self.calc_adc(max_temp)]
    #     self.min_sample_value = min(adc_range)
    #     self.max_sample_value = max(adc_range)
    # def setup_callback(self, cb):
    #     self._callback = cb
    # def get_report_time_delta(self):
    #     return REPORT_TIME
    def _build_config(self):
        dr = 0b100
        pga = 0b111
        mux = 0b000
        self.mcu.add_config_cmd(
            "config_ads1x18 oid=%u spi_oid=%u data_rate=%u response_interval=%u"
            % (self.oid, self.spi.get_oid(), dr, 200)
        )
        log(
            "config_ads1x18 oid=%u spi_oid=%u data_rate=%u response_interval=%u"
            % (self.oid, self.spi.get_oid(), dr, 200)
        )
        self.mcu.add_config_cmd(
            "ads_sensor_ads1x18 oid=%u mux=%u pga=%u dr=%u"
            % (self.oid, mux, pga, dr)
        )
        log(
            "ads_sensor_ads1x18 oid=%u mux=%u pga=%u dr=%u"
            % (self.oid, mux, pga, dr)
        )
        mux = 0b011
        self.mcu.add_config_cmd(
            "ads_sensor_ads1x18 oid=%u mux=%u pga=%u dr=%u"
            % (self.oid, mux, pga, dr)
        )
        log(
            "ads_sensor_ads1x18 oid=%u mux=%u pga=%u dr=%u"
            % (self.oid, mux, pga, dr)
        )

        # clock = self.mcu.get_query_slot(self.oid)
        self._report_clock = self.mcu.seconds_to_clock(REPORT_TIME)
        # self.mcu.add_config_cmd(
        #     "query_thermocouple oid=%u clock=%u rest_ticks=%u"
        #     " min_value=%u max_value=%u max_invalid_count=%u" % (
        #         self.oid, clock, self._report_clock,
        #         self.min_sample_value, self.max_sample_value,
        #         MAX_INVALID_COUNT), is_init=True)

    def _handle_spi_response(self, params):
        temp = (params["temperature"] >> 2) * 0.03125

        if params["sensor"] == 0:
            log("Temperature ADS1118 %i°C " % (temp))
        else:
            # TODO: fault detection/processing
            uvalue = params["value"]
            svalue = -(65536 - uvalue) if uvalue > 32767 else uvalue
            voltage = svalue * 7.8125 / 1e3
            voltage = voltage + celsius_to_volt(temp)
            sensor_temp = volt_to_celsius(voltage)
            log(
                "temperature voltage %f thermocouple voltage %f"
                % (celsius_to_volt(temp), svalue * 7.8125 / 1e3)
            )
            log(
                "Temperature ADS1118 %i °C raw signed %i unsigned %u voltage %f Guessed temperature: %f °C "
                % (temp, svalue, uvalue, voltage, sensor_temp)
            )

        # if params['fault']:
        #     self.handle_fault(params['value'], params['fault'])
        #     return
        # temp = self.calc_temp(params['value'])
        # next_clock      = self.mcu.clock32_to_clock64(params['next_clock'])
        # last_read_clock = next_clock - self._report_clock
        # last_read_time  = self.mcu.clock_to_print_time(last_read_clock)
        # self._callback(last_read_time, temp)
        # log("Got a response :-)" + params.__str__())

    def report_fault(self, msg):
        log("report_fault: " + msg.__str__())


class ADS1118(SensorBase):
    def __init__(self, config):
        SensorBase.__init__(self, config, "ADS1118",
                            self.build_spi_init(config))

    # def handle_fault(self, adc, fault):
    #     log("Fault '" + adc.__str__() + "' '" + fault.__str__())
    #     if fault & MAX31856_FAULT_CJRANGE:
    #         self.report_fault("Max31856: Cold Junction Range Fault")
    #     if fault & MAX31856_FAULT_TCRANGE:
    #         self.report_fault("Max31856: Thermocouple Range Fault")
    #     if fault & MAX31856_FAULT_CJHIGH:
    #         self.report_fault("Max31856: Cold Junction High Fault")
    #     if fault & MAX31856_FAULT_CJLOW:
    #         self.report_fault("Max31856: Cold Junction Low Fault")
    #     if fault & MAX31856_FAULT_TCHIGH:
    #         self.report_fault("Max31856: Thermocouple High Fault")
    #     if fault & MAX31856_FAULT_TCLOW:
    #         self.report_fault("Max31856: Thermocouple Low Fault")
    #     if fault & MAX31856_FAULT_OVUV:
    #         self.report_fault("Max31856: Over/Under Voltage Fault")
    #     if fault & MAX31856_FAULT_OPEN:
    #         self.report_fault("Max31856: Thermocouple Open Fault")
    # def calc_temp(self, adc):
    #     # adc = adc >> MAX31856_SCALE
    #     # # Fix sign bit:
    #     # if adc & 0x40000:
    #     #     adc = ((adc & 0x3FFFF) + 1) * -1
    #     # temp = MAX31856_MULT * adc
    #     # return temp
    #     return 15
    # def calc_adc(self, temp):
    #     log("adc convert")
    #     # adc = int( ( temp / MAX31856_MULT ) + 0.5 ) # convert to ADC value
    #     # adc = max(0, min(0x3FFFF, adc)) << MAX31856_SCALE
    #     #return adc
    #     return 17
    def build_spi_init(self, config):
        cmds = []
        msb = 0
        # Set Mux config
        msb = msb | (0b000 << 4)
        # Set PGA
        msb = msb | (0b010 << 1)
        # Set Mode
        msb = msb | (0b1 << 0)
        # Set Data rate
        lsb = 0
        lsb = lsb | (0b100 << 5)
        # Set TS_MODE
        lsb = lsb | (0b0 << 4)
        # Pullup
        lsb = lsb | (0b1 << 3)
        # NOP
        lsb = lsb | (0b01 << 1)
        # RESERVED (let us test this..)
        lsb = lsb | 1
        cmds.append(msb)
        cmds.append(lsb)
        return cmds


# class ADS1118:
#     def __init__(self, config):
#         log("Init start")
#         self.printer = config.get_printer()
#         self._callback = None
#         self.spi = bus.MCU_SPI_from_config(config, 1, default_speed=4000000)
#         config = self._build_config();
#         log("Config 0b" + "{:08b} {:08b}".format(config[0], config[1]))

#         self.mcu = mcu = self.spi.get_mcu()
#         self.oid = oid = mcu.create_oid()
#         mcu.register_response(self._handle_spi_response,
#                               "ads1118_response", oid)
#         self.spi.spi_send(config)

#         log("Init end")
#         log(self.__dict__)
#         log(self.spi.__dict__)
#     def _build_config(self):
# msb = 0
# # Set Mux config
# msb = msb | (0b000 << 4)
# # Set PGA
# msb = msb | (0b010 << 1)
# # Set Mode
# msb = msb | (0b1 << 0)
# # Set Data rate
# lsb = 0
# lsb = lsb | (0b100 << 5)
# # Set TS_MODE
# lsb = lsb | (0b0 << 4)
# # Pullup
# lsb = lsb | (0b1 << 3)
# # NOP
# lsb = lsb | (0b01 << 1)
# # RESERVED (let us test this..)
# lsb = lsb | 1
# return [msb, lsb]
#     def _handle_spi_response(self, params):
#         log("Response: " + params)


def load_config(config):
    return ADS1118(config)
