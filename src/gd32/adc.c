// ADC functions on GD32
//
// Copyright (C) 2019-2020  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "board/irq.h"           // irq_save
#include "board/misc.h"          // timer_from_us
#include "command.h"             // shutdown
#include "compiler.h"            // ARRAY_SIZE
#include "generic/armcm_timer.h" // udelay
#include "gpio.h"                // gpio_adc_setup
#include "internal.h"            // GPIO
#include "sched.h"               // sched_shutdown
#include "gpio.h"
#include "gd32f30x_adc.h" 


DECL_CONSTANT("ADC_MAX", 4095);

#define ADC_TEMPERATURE_PIN 0xfe
#define ADC_VOLTAGE_REFERENCE_PIN 0xff
DECL_ENUMERATION("pin", "ADC_TEMPERATURE", ADC_TEMPERATURE_PIN);
DECL_ENUMERATION("pin", "ADC_VOLTAGE", ADC_VOLTAGE_REFERENCE_PIN);

//ordered by adc_channel
static const uint8_t adc_pins[] = {
    GPIO('A', 0), // ADC01_IN0
    GPIO('A', 1), // ADC01_IN1
    GPIO('A', 2), // ADC01_IN2
    GPIO('A', 3), // ADC01_IN3
    GPIO('A', 4), // ADC01_IN4
    GPIO('A', 5), // ADC01_IN5
    GPIO('A', 6), // ADC01_IN6
    GPIO('A', 7), // ADC01_IN7

    GPIO('B', 0), // ADC01_IN8
    GPIO('B', 1), // ADC01_IN9

    GPIO('C', 0), // ADC01_IN10
    GPIO('C', 1), // ADC01_IN11
    GPIO('C', 2), // ADC01_IN12
    GPIO('C', 3), // ADC01_IN13
    GPIO('C', 4), // ADC01_IN14
    GPIO('C', 5), // ADC01_IN15
    ADC_TEMPERATURE_PIN, // internal temperature sensor
    ADC_VOLTAGE_REFERENCE_PIN // voltage sensor
};

static const uint8_t adc_channels[] = {
    ADC_CHANNEL_0,
    ADC_CHANNEL_1,
    ADC_CHANNEL_2,
    ADC_CHANNEL_3,
    ADC_CHANNEL_4,
    ADC_CHANNEL_5,
    ADC_CHANNEL_6,
    ADC_CHANNEL_7,
    ADC_CHANNEL_8,
    ADC_CHANNEL_9,
    ADC_CHANNEL_10,
    ADC_CHANNEL_11,
    ADC_CHANNEL_12,
    ADC_CHANNEL_13,
    ADC_CHANNEL_14,
    ADC_CHANNEL_15,
    ADC_CHANNEL_16,
    ADC_CHANNEL_17
};

static int is_periph_clock_enabled(rcu_periph_enum periph) {
    return !!(RCU_REG_VAL(periph) & BIT(RCU_BIT_POS(periph)));
}

static int find_adc_channel (uint32_t pin)
{
    // Find pin in adc_pins table
    for (int chan = 0; chan < ARRAY_SIZE(adc_pins) ; chan++) 
    {
        if (adc_pins[chan] == pin)
            return chan; 
    }

    shutdown("Not a valid ADC pin");
}

struct gpio_adc gpio_adc_setup(uint32_t pin_number)
{
    //TODO: check pin!
    int channel_idx = find_adc_channel(pin_number); 

    // TODO: init GPIO clock
    // TODO: DMA?? 

    // TODO: make use of second ADC? 
    uint32_t adc = ADC0;
    int needs_init = !is_periph_clock_enabled(RCU_ADC0);
    
    //warning, these are shared, need to change this when using second ADC
    //GD32F307 can run at 40Mhz, but not good prescaler available
    //30Mhz is possible, but then either 71 sampling ticks ~ 3.5us, 239 ticks ~ 10.5us
    //24Mhz 71 ticks ~ 3.5us, 239 ticks ~ 10.5us
    //20Mhz 71 ticks ~ 4.2us
    if(needs_init)
    {
        // output("ADC init"); 
        rcu_periph_clock_enable(RCU_ADC0);
        rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV4); 
        adc_deinit(adc);       
        
        adc_mode_config(ADC_MODE_FREE);
        adc_data_alignment_config(adc, ADC_DATAALIGN_RIGHT); 
        adc_channel_length_config(adc, ADC_REGULAR_CHANNEL, 1);
        adc_external_trigger_source_config(adc, ADC_REGULAR_CHANNEL, ADC0_1_2_EXTTRIG_REGULAR_NONE);
        adc_external_trigger_config(adc, ADC_REGULAR_CHANNEL, ENABLE); 
        
        adc_enable(adc); 
        adc_calibration_enable(adc);
    }

    if(pin_number == ADC_TEMPERATURE_PIN || pin_number == ADC_VOLTAGE_REFERENCE_PIN)
        adc_tempsensor_vrefint_enable();
    else
        gpio_setup_adc(pin_number, GPIO_HIGH_SPEED);
    struct gpio_adc g = {.adc = adc, .channel = adc_channels[channel_idx]};
    // output("gpio_adc_setup status=%u ctl0=%u ctl1=%u", ADC_STAT(adc), ADC_CTL0(adc), ADC_CTL1(adc));
    return g;
}

uint32_t gpio_adc_sample(struct gpio_adc g)
{
    // sampling was started
    // output("gpio_adc_sample entry status=%u ctl0=%u ctl1=%u", ADC_STAT(g.adc), ADC_CTL0(g.adc), ADC_CTL1(g.adc));
    if (adc_flag_get(g.adc, ADC_FLAG_STRC) == SET)
    {
        // and is completed? 
        if(adc_flag_get(g.adc, ADC_FLAG_EOC) == SET)
        {
            adc_flag_clear(g.adc, ADC_FLAG_STRC); 
            // output("complete status=%u ctl0=%u ctl1=%u", ADC_STAT(g.adc), ADC_CTL0(g.adc), ADC_CTL1(g.adc));

            return 0;
        }
        // output("sample not complete"); 
    }
    else
    {
        // output("gpio_adc_sample start status=%u ctl0=%u ctl1=%u", ADC_STAT(g.adc), ADC_CTL0(g.adc), ADC_CTL1(g.adc));
        adc_regular_channel_config(g.adc, 0, g.channel, ADC_SAMPLETIME_239POINT5);
        adc_software_trigger_enable(g.adc, ADC_REGULAR_CHANNEL); 
    }

    // output("gpio_adc_sample ende status=%u ctl0=%u ctl1=%u", ADC_STAT(g.adc), ADC_CTL0(g.adc), ADC_CTL1(g.adc));
    return timer_from_us(20);
}

uint16_t  gpio_adc_read(struct gpio_adc g) 
{
    //output("gpio_adc_read  status=%u ctl0=%u ctl1=%u", ADC_STAT(g.adc), ADC_CTL0(g.adc), ADC_CTL1(g.adc));
    adc_flag_clear(g.adc, ADC_FLAG_STRC);
    uint16_t value = adc_regular_data_read(g.adc);
    // output("raw value=%u", value); 
    // output("gpio_adc_read  status=%u ctl0=%u ctl1=%u", ADC_STAT(g.adc), ADC_CTL0(g.adc), ADC_CTL1(g.adc));
    return value;
}

void gpio_adc_cancel_sample(struct gpio_adc g)
{
    //output("gpio_adc_cancel_sample  status=%u ctl0=%u ctl1=%u", ADC_STAT(g.adc), ADC_CTL0(g.adc), ADC_CTL1(g.adc));
    irqstatus_t flag = irq_save();
    adc_flag_clear(g.adc, ADC_FLAG_STRC);
    irq_restore(flag);
}