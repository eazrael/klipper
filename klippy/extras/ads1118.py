import math, logging
from . import bus

REPORT_TIME = 0.300
MAX_INVALID_COUNT = 3

def log(message): 
    logging.warning("ads1118 " + message.__str__())


class SensorBase:
    def __init__(self, config, chip_type, config_cmd=None, spi_mode=1):
        log("In Sensorbase")
        self.printer = config.get_printer()
        self.chip_type = chip_type
        self._callback = None
        self.min_sample_value = self.max_sample_value = 0
        self._report_clock = 0
        self.spi = bus.MCU_SPI_from_config(
            config, spi_mode)
        log("spi " + self.spi.__dict__.__str__())
        if config_cmd is not None:
            self.spi.spi_send(config_cmd)
        self.mcu = mcu = self.spi.get_mcu()
        # Reader chip configuration
        self.oid = oid = mcu.create_oid()
        log("A self " + self.__dict__.__str__())
        # mcu.register_response(self._handle_spi_response,
        #                       "thermocouple_result", oid)
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
        self.mcu.add_config_cmd(
            "config_ads1x18 oid=%u spi_oid=%u" % (
                self.oid, self.spi.get_oid()))
        log("config_ads1x18 oid=%u spi_oid=%u" % (
                self.oid, self.spi.get_oid()))
        clock = self.mcu.get_query_slot(self.oid)
        self._report_clock = self.mcu.seconds_to_clock(REPORT_TIME)
        # self.mcu.add_config_cmd(
        #     "query_thermocouple oid=%u clock=%u rest_ticks=%u"
        #     " min_value=%u max_value=%u max_invalid_count=%u" % (
        #         self.oid, clock, self._report_clock,
        #         self.min_sample_value, self.max_sample_value,
        #         MAX_INVALID_COUNT), is_init=True)
    def _handle_spi_response(self, params):
        # if params['fault']:
        #     self.handle_fault(params['value'], params['fault'])
        #     return
        # temp = self.calc_temp(params['value'])
        # next_clock      = self.mcu.clock32_to_clock64(params['next_clock'])
        # last_read_clock = next_clock - self._report_clock
        # last_read_time  = self.mcu.clock_to_print_time(last_read_clock)
        # self._callback(last_read_time, temp)
        log("Got a response :-)" + params.__str__())
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
