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

struct ads1x18_data_rate {
    uint8_t config;
    uint16_t rate; 
};

struct ads1x18_spec {
    uint8_t chip_type; 
    uint8_t precision;
    struct ads1x18_data_rate data_rates[8];
    uint8_t default_rate_index; 
};

struct ads1x18_spec ADS1X118_CHIPS[2] = {{
    ADS1018,
    12, 
    {{0b000, 128},
    {0b001, 250},
    {0b010, 490}, 
    {0b011, 920}, 
    {0b100, 1600},
    {0b101, 2400},
    {0b110, 3300},
    {0b111, 0}},
    4},{
    ADS1118,
    16, 
    {{0b000, 8},
    {0b001, 16},
    {0b010, 32}, 
    {0b011, 64}, 
    {0b100, 128},
    {0b101, 250},
    {0b110, 475},
    {0b111, 860}},
    4}
};

struct ads1x18_spi {
    struct timer timer;
    uint32_t rest_time;
    uint8_t data_rate_index; 
    // uint32_t min_value;           // Min allowed ADC value
    // uint32_t max_value;           // Max allowed ADC value
    struct spidev_s *spi;
    // uint8_t max_invalid, invalid_count;
    // uint8_t chip_type, flags;
    struct ads1x18_spec *chip_type;
    struct task_wake task_wake;
};

static uint_fast8_t ads1x18_event(struct timer *timer);


static void set_data_rate(struct ads1x18_spi *ads1x18,  uint32_t data_rate) {
    struct ads1x18_data_rate *data_rate_iter = ads1x18->chip_type->data_rates; 
    struct ads1x18_data_rate *end = data_rate_iter + 8; 
    while(data_rate_iter != end && data_rate_iter->rate != 0) {
        if(data_rate_iter->rate == data_rate) {
            ads1x18->data_rate_index = data_rate_iter - ads1x18->chip_type->data_rates;
            return;
        }
    }
    shutdown("Invalid data rate for ads1x18");
}

static void doExchange(struct ads1x18_spi *spi)
{
    uint8_t msg[4] = { 0x00, 0x00, 0x00, 0x00 };
    msg[0] |= 0 << 7;
    msg[0] |= 0b000 << 4; 
    msg[0] |= 0b010 << 1;
    msg[0] |= 0;
    msg[1] |= spi->chip_type->data_rates[spi->data_rate_index].config << 5;
    msg[1] |= 0b1 << 4;
    msg[1] |= 0b1 << 3;
    msg[1] |= 0b01 << 1;
    msg[1] |= 1;
    msg[2] = msg[0];
    msg[3] = msg[1];
    output("ads1118 doExchange Sending %u %u %u %u", msg[0], msg[1], msg[2], msg[3]); 
    spidev_transfer(spi->spi, 1, 4, msg);
    output("ads1118 doExchange Received %u %u %u %u", msg[0], msg[1], msg[2], msg[3]);
}

void command_config_ads1x18_start(struct ads1x18_spi *spi) {
    doExchange(spi); 
}

void command_config_ads1x18(uint32_t *args)
{
    uint8_t spec_index = 1; 
    if(spec_index > ADS1118) 
        shutdown("Invalid ads1x18 chip type");

    struct ads1x18_spi *spi = oid_alloc(
        args[0], command_config_ads1x18, sizeof(*spi));
    spi->chip_type = &ADS1X118_CHIPS[spec_index];
    spi->spi = spidev_oid_lookup(args[1]);
    spi->data_rate_index = spi->chip_type->default_rate_index; 
    spi->rest_time = 168000000;
    spi->timer.func = ads1x18_event;
    output("end of config");
    doExchange(spi);
    spi->timer.waketime = timer_read_time() + spi->rest_time;
    sched_add_timer(&spi->timer);
}
DECL_COMMAND(command_config_ads1x18,
             "config_ads1x18 oid=%u spi_oid=%u");


static uint_fast8_t ads1x18_event(struct timer *timer) {
    struct ads1x18_spi *spi = container_of(
            timer, struct ads1x18_spi, timer);
    output("eventhandler!");
    // Trigger task to read and send results
    sched_wake_task(&spi->task_wake);
    doExchange(spi);
    // spi->flags |= TS_PENDING;
    spi->timer.waketime = timer_read_time() + spi->rest_time;
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
