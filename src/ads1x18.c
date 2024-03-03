// Basic support for ADS1018/ADS1118 SPI controlled ADCs
//
// Copyright (C) 2024  Christoph Nelles <evilazrael@evilazrael.de>
// Copyright (C) 2018  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // memcpy
// #include "board/irq.h" // irq_disable
#include "basecmd.h" // oid_alloc
#include "byteorder.h" // be32_to_cpu
#include "command.h" // DECL_COMMAND
#include "sched.h" // DECL_TASK
#include "spicmds.h" // spidev_transfer
#include "generic/misc.h"
// There are two differences between ADS1018/ADS118:
// Sample rates max 3300/860
// Precision 12bit/16bit

enum {
    ADS1018 = 0, ADS1118
};

DECL_ENUMERATION("ads1x18_type", "ADS1018", ADS1018);
DECL_ENUMERATION("ads1x18_type", "ADS1118", ADS1118);

struct ads1x18_sensor_config {
    unsigned adc : 1; 
    unsigned mux : 3;
    unsigned pga : 3;
    unsigned dr : 3;
};

struct ads1x18_spi
{
    struct spidev_s *spi;
    struct task_wake task_wake;
    struct timer timer;
    uint32_t response_interval;
    int16_t last_temperature; 
    uint8_t oid; 
    uint8_t sensor_count;
    uint8_t current_sensor; 
    struct ads1x18_sensor_config sensor_configs[5];
};

static uint_fast8_t ads1x18_event(struct timer *timer);

static void doExchange(struct ads1x18_spi *spi)
{
    uint8_t current_sensor = spi->current_sensor;
    uint8_t next_sensor = (current_sensor + 1) % spi->sensor_count;

    uint8_t msg[4] = {0x00, 0x00, 0x00, 0x00};
    msg[1] |= 0b1 << 3;
    msg[1] |= 0b01 << 1;
    msg[1] |= 1;
    if(spi->sensor_configs[next_sensor].adc == 0) {
    // 0 - ADC / 1 - Temperature
        msg[1] |= 0b1 << 4;    
    } else {
        //mux setting        
        msg[0] |= (spi->sensor_configs[next_sensor].mux & 0b111) << 4; 
        //pga
        msg[0] |= (spi->sensor_configs[next_sensor].pga & 0b111) << 1;    
    }
    msg[1] |= spi->sensor_configs[next_sensor].dr << 5;
    msg[2] = msg[0];
    msg[3] = msg[1];
    //output("ads1118 doExchange Sending %u %u %u %u", msg[0], msg[1], msg[2], msg[3]); 
    spidev_transfer(spi->spi, 1, 4, msg);
    if(spi->sensor_configs[current_sensor].adc == 0)
        spi->last_temperature = msg[0] << 8 | msg[1];
    sendf("ads1118_result oid=%u temperature=%c sensor=%c value=%c",
          spi->oid, spi->last_temperature, current_sensor, (msg[0] << 8) | msg[1]);
    //output("ads1118 doExchange Received %u %u %u %u", msg[0], msg[1], msg[2], msg[3]);
    spi->current_sensor = next_sensor;
}

void command_config_ads1x18_start(struct ads1x18_spi *spi) {
    doExchange(spi); 
}

void command_config_ads1x18(uint32_t *args)
{
    struct ads1x18_spi *spi = oid_alloc( args[0], command_config_ads1x18, 
        sizeof(*spi));
    spi->oid = args[0];
    spi->spi = spidev_oid_lookup(args[1]);
    spi->sensor_configs[0].adc = 0;
    spi->sensor_configs[0].dr = args[2] & 0b111;
    spi->sensor_count = 1; 
    spi->response_interval = timer_from_us(1000) * args[3]; 
    spi->timer.func = ads1x18_event;
    doExchange(spi);
    spi->timer.waketime = timer_read_time() + spi->response_interval;
    sched_add_timer(&spi->timer);
}
DECL_COMMAND(command_config_ads1x18,
             "config_ads1x18 oid=%u spi_oid=%u data_rate=%u response_interval=%u");

void command_add_sensor_ads1x18(uint32_t *args) {
    //need to lock object
    struct ads1x18_spi *spi = oid_lookup(args[0], command_config_ads1x18);
    if(spi->sensor_count == 5)
        shutdown("ads1x18 too many sensors");
    spi->sensor_configs[spi->sensor_count].adc = 1;
    spi->sensor_configs[spi->sensor_count].mux = args[1] & 0b111;
    spi->sensor_configs[spi->sensor_count].pga = args[2] & 0b111;
    spi->sensor_configs[spi->sensor_count].dr = args[3] & 0b111;
    spi->sensor_count++;
}
DECL_COMMAND(command_add_sensor_ads1x18,
             "ads_sensor_ads1x18 oid=%u mux=%u pga=%u dr=%u");

static uint_fast8_t ads1x18_event(struct timer *timer) {
    struct ads1x18_spi *spi = container_of(
            timer, struct ads1x18_spi, timer);
    // Trigger task to read and send results
    sched_wake_task(&spi->task_wake);
    doExchange(spi);

    spi->timer.waketime = timer_read_time() + spi->response_interval;
    return SF_RESCHEDULE;
}

// void
// trigger_sampling(uint8_t oid)
// {
//     struct ads1x18_spi *spi = oid_lookup(
//         oid, command_config_ads1x18);

//     sched_del_timer(&spi->timer);
//     spi->timer.waketime = args[1];
//     spi->rest_time = args[2];
//     if (! spi->rest_time)
//         return;
//     // spi->min_value = args[3];
//     // spi->max_value = args[4];
//     // spi->max_invalid = args[5];
//     // spi->invalid_count = 0;
//     sched_add_timer(&spi->timer);
// }



// struct thermocouple_spi {
//     struct timer timer;
//     uint32_t rest_time;
//     uint32_t min_value;           // Min allowed ADC value
//     uint32_t max_value;           // Max allowed ADC value
//     struct spidev_s *spi;
//     uint8_t max_invalid, invalid_count;
//     uint8_t chip_type, flags;
// };

// enum {
//     TS_PENDING = 1,
// };

// static struct task_wake thermocouple_wake;

// static uint_fast8_t thermocouple_event(struct timer *timer) {
//     struct thermocouple_spi *spi = container_of(
//             timer, struct thermocouple_spi, timer);
//     // Trigger task to read and send results
//     sched_wake_task(&thermocouple_wake);
//     spi->flags |= TS_PENDING;
//     spi->timer.waketime += spi->rest_time;
//     return SF_RESCHEDULE;
// }

// void
// command_config_thermocouple(uint32_t *args)
// {
//     uint8_t chip_type = args[2];
//     if (chip_type > TS_CHIP_MAX6675)
//         shutdown("Invalid thermocouple chip type");
//     struct thermocouple_spi *spi = oid_alloc(
//         args[0], command_config_thermocouple, sizeof(*spi));
//     spi->timer.func = thermocouple_event;
//     spi->spi = spidev_oid_lookup(args[1]);
//     spi->chip_type = chip_type;
// }
// DECL_COMMAND(command_config_thermocouple,
//              "config_thermocouple oid=%c spi_oid=%c thermocouple_type=%c");

// void
// command_query_thermocouple(uint32_t *args)
// {
//     struct thermocouple_spi *spi = oid_lookup(
//         args[0], command_config_thermocouple);

//     sched_del_timer(&spi->timer);
//     spi->timer.waketime = args[1];
//     spi->rest_time = args[2];
//     if (! spi->rest_time)
//         return;
//     spi->min_value = args[3];
//     spi->max_value = args[4];
//     spi->max_invalid = args[5];
//     spi->invalid_count = 0;
//     sched_add_timer(&spi->timer);
// }
// DECL_COMMAND(command_query_thermocouple,
//              "query_thermocouple oid=%c clock=%u rest_ticks=%u"
//              " min_value=%u max_value=%u max_invalid_count=%c");

// static void
// thermocouple_respond(struct thermocouple_spi *spi, uint32_t next_begin_time
//                      , uint32_t value, uint8_t fault, uint8_t oid)
// {
//     sendf("thermocouple_result oid=%c next_clock=%u value=%u fault=%c",
//           oid, next_begin_time, value, fault);
//     /* check the result and stop if below or above allowed range */
//     if (fault || value < spi->min_value || value > spi->max_value) {
//         spi->invalid_count++;
//         if (spi->invalid_count < spi->max_invalid)
//             return;
//         try_shutdown("Thermocouple reader fault");
//     }
//     spi->invalid_count = 0;
// }

// static void
// thermocouple_handle_max31855(struct thermocouple_spi *spi
//                              , uint32_t next_begin_time, uint8_t oid)
// {
//     uint8_t msg[4] = { 0x00, 0x00, 0x00, 0x00 };
//     spidev_transfer(spi->spi, 1, sizeof(msg), msg);
//     uint32_t value;
//     memcpy(&value, msg, sizeof(value));
//     value = be32_to_cpu(value);
//     thermocouple_respond(spi, next_begin_time, value, value & 0x07, oid);
// }

// #define MAX31856_LTCBH_REG 0x0C
// #define MAX31856_SR_REG 0x0F

// static void
// thermocouple_handle_max31856(struct thermocouple_spi *spi
//                              , uint32_t next_begin_time, uint8_t oid)
// {
//     uint8_t msg[4] = { MAX31856_LTCBH_REG, 0x00, 0x00, 0x00 };
//     spidev_transfer(spi->spi, 1, sizeof(msg), msg);
//     uint32_t value;
//     memcpy(&value, msg, sizeof(value));
//     value = be32_to_cpu(value) & 0x00ffffff;
//     // Read faults
//     msg[0] = MAX31856_SR_REG;
//     msg[1] = 0x00;
//     spidev_transfer(spi->spi, 1, 2, msg);
//     thermocouple_respond(spi, next_begin_time, value, msg[1], oid);
// }

// #define MAX31865_RTDMSB_REG 0x01
// #define MAX31865_FAULTSTAT_REG 0x07

// static void
// thermocouple_handle_max31865(struct thermocouple_spi *spi
//                              , uint32_t next_begin_time, uint8_t oid)
// {
//     uint8_t msg[4] = { MAX31865_RTDMSB_REG, 0x00, 0x00, 0x00 };
//     spidev_transfer(spi->spi, 1, 3, msg);
//     uint32_t value;
//     memcpy(&value, msg, sizeof(value));
//     value = (be32_to_cpu(value) >> 8) & 0xffff;
//     // Read faults
//     msg[0] = MAX31865_FAULTSTAT_REG;
//     msg[1] = 0x00;
//     spidev_transfer(spi->spi, 1, 2, msg);
//     uint8_t fault = (msg[1] & ~0x03) | (value & 0x0001);
//     thermocouple_respond(spi, next_begin_time, value, fault, oid);
// }

// static void
// thermocouple_handle_max6675(struct thermocouple_spi *spi
//                             , uint32_t next_begin_time, uint8_t oid)
// {
//     uint8_t msg[2] = { 0x00, 0x00};
//     spidev_transfer(spi->spi, 1, sizeof(msg), msg);
//     uint16_t value;
//     memcpy(&value, msg, sizeof(msg));
//     value = be16_to_cpu(value);
//     thermocouple_respond(spi, next_begin_time, value, value & 0x06, oid);
// }

// // task to read thermocouple and send response
// void
// thermocouple_task(void)
// {
//     if (!sched_check_wake(&thermocouple_wake))
//         return;
//     uint8_t oid;
//     struct thermocouple_spi *spi;
//     foreach_oid(oid, spi, command_config_thermocouple) {
//         if (!(spi->flags & TS_PENDING))
//             continue;
//         irq_disable();
//         uint32_t next_begin_time = spi->timer.waketime;
//         spi->flags &= ~TS_PENDING;
//         irq_enable();
//         switch (spi->chip_type) {
//         case TS_CHIP_MAX31855:
//             thermocouple_handle_max31855(spi, next_begin_time, oid);
//             break;
//         case TS_CHIP_MAX31856:
//             thermocouple_handle_max31856(spi, next_begin_time, oid);
//             break;
//         case TS_CHIP_MAX31865:
//             thermocouple_handle_max31865(spi, next_begin_time, oid);
//             break;
//         case TS_CHIP_MAX6675:
//             thermocouple_handle_max6675(spi, next_begin_time, oid);
//             break;
//         }
//     }
// }
// DECL_TASK(thermocouple_task);
