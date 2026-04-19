// Hardware PWM support on GD32F307
//
// Copyright (C) 2025, 2026  Evil Azrael
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_MACH_STM32H7
#include "board/irq.h" // irq_save
#include "command.h" // shutdown
#include "gpio.h" // gpio_pwm_write
#include "internal.h" // GPIO
#include "sched.h" // sched_shutdown
#include "gd32f30x_timer.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

#define MAX_PWM (256 + 1)
DECL_CONSTANT("PWM_MAX", MAX_PWM);

struct timer_info 
{
    uint32_t timer;
    uint32_t rcu_clock_timer;
    uint32_t rcu_clock_periph; 
};

static const struct timer_info timer_infos[] = {
    {TIMER0, RCU_TIMER0, CK_APB2},
    {TIMER1, RCU_TIMER0, CK_APB1},
    {TIMER2, RCU_TIMER2, CK_APB1},
    {TIMER3, RCU_TIMER3, CK_APB1},
    {TIMER8, RCU_TIMER8, CK_APB2},
    {TIMER9, RCU_TIMER9, CK_APB2},
    {TIMER10, RCU_TIMER10, CK_APB2},
    {TIMER12, RCU_TIMER12, CK_APB1},
    {TIMER13, RCU_TIMER13, CK_APB1}
};

struct gpio_pwm_info {
    uint32_t timer_idx;
    uint32_t remap;
    uint8_t pin_number, channel;
};

static const struct gpio_pwm_info gpio_pwm_pins[] = {
    // Timer 0, Channel 0
    {0,    0,                          GPIO('A', 8),   TIMER_CH_0},
    {0,    GPIO_TIMER0_PARTIAL_REMAP,  GPIO('E', 9),   TIMER_CH_0},
    // Timer 0, Channel 1    
    {0,    0,                          GPIO('A', 9),   TIMER_CH_1},
    {0,    GPIO_TIMER0_PARTIAL_REMAP,  GPIO('E', 11),  TIMER_CH_1},
    // Timer 0, Channel 2
    {0,    0,                          GPIO('A', 10),  TIMER_CH_2},
    {0,    GPIO_TIMER0_PARTIAL_REMAP,  GPIO('E', 13),  TIMER_CH_2},
    // Timer 0, Channel 3
    {0,    0,                          GPIO('A', 11),  TIMER_CH_3},
    {0,    GPIO_TIMER0_PARTIAL_REMAP,  GPIO('E', 14),  TIMER_CH_3},
    // Timer 1, Channel 0    
    {1,    0,                          GPIO('A', 0),   TIMER_CH_0},    
    {1,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('A', 15),  TIMER_CH_0},
    // Timer 1, Channel 1
    {1,    0,                          GPIO('A', 1),   TIMER_CH_1},
    {1,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('B', 3),   TIMER_CH_1},
    // Timer 1, Channel 2    
    {1,    0,                          GPIO('A', 2),   TIMER_CH_2},
    {1,    GPIO_TIMER1_PARTIAL_REMAP1, GPIO('B', 10),  TIMER_CH_2},
    // Timer 1, Channel 3
    {1,    0,                          GPIO('A', 3),   TIMER_CH_3},
    {1,    GPIO_TIMER1_PARTIAL_REMAP1, GPIO('B', 11),  TIMER_CH_3},
    // Timer 2, Channel 0
    {2,    0,                          GPIO('A', 6),   TIMER_CH_0},
    {2,    GPIO_TIMER2_PARTIAL_REMAP, GPIO('B', 4),   TIMER_CH_0},
    {2,    GPIO_TIMER2_FULL_REMAP,     GPIO('C', 6),   TIMER_CH_0},
    // Timer 2, Channel 1
    {2,    0,                          GPIO('A', 7),   TIMER_CH_1},
    {2,    GPIO_TIMER2_PARTIAL_REMAP , GPIO('B', 5),   TIMER_CH_1},
    {2,    GPIO_TIMER2_FULL_REMAP,     GPIO('C', 7),   TIMER_CH_1},
    // Timer 2, Channel 2
    {2,    0,                          GPIO('B', 0),   TIMER_CH_2},
    {2,    GPIO_TIMER2_FULL_REMAP,     GPIO('C', 8),   TIMER_CH_2},
    // Timer 2, Channel 3
    {2,    0,                          GPIO('B', 1),   TIMER_CH_3},
    {2,    GPIO_TIMER2_FULL_REMAP,     GPIO('C', 9),   TIMER_CH_3},
    // Timer 3, Channel 0
    {3,    0,                          GPIO('B', 6),   TIMER_CH_0},    
    {3,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('D', 12),  TIMER_CH_0},
    // Timer 3, Channel 1
    {3,    0,                          GPIO('B', 7),   TIMER_CH_1},    
    {3,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('D', 13),  TIMER_CH_1},
    // Timer 3, Channel 2
    {3,    0,                          GPIO('B', 8),   TIMER_CH_2},    
    {3,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('D', 14),  TIMER_CH_2},
    // Timer 3, Channel 3
    {3,    0,                          GPIO('B', 9),   TIMER_CH_3},    
    {3,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('D', 15),  TIMER_CH_3},
    // Timer 8, Channel 0
    {4,    0,                          GPIO('A', 2),   TIMER_CH_0},    
    {4,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('E', 5),   TIMER_CH_0},
    // Timer 8, Channel 1
    {4,    0,                          GPIO('A', 3),   TIMER_CH_1},    
    {4,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('E', 6),   TIMER_CH_1},
    // Timer 9, Channel 0
    {5,    0,                          GPIO('B', 8),   TIMER_CH_0},    
    {5,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('F', 6),   TIMER_CH_0},
    // Timer 10, Channel 0
    {6,    0,                          GPIO('B', 9),   TIMER_CH_0},    
    {6,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('F', 7),   TIMER_CH_0},
    // Timer 12, Channel 0
    {8,    0,                          GPIO('A', 6),   TIMER_CH_0},    
    {8,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('F', 8),   TIMER_CH_0},
    // Timer 13, Channel 0
    {9,    0,                          GPIO('A', 7),   TIMER_CH_0},    
    {9,    GPIO_TIMER1_PARTIAL_REMAP0, GPIO('F', 9),   TIMER_CH_0},
};

uint32_t 
get_gpio_pin_index(uint32_t pin) 
{
    uint32_t pin_count = ARRAY_SIZE(gpio_pwm_pins); 
    for(int i = 0; i < pin_count; i++)
        if(gpio_pwm_pins[i].pin_number == pin)
            return i;
    shutdown("Not a valid PWM pin"); 
}

void check_existing_prescaler(uint32_t timer, uint16_t prescaler)
{
    if(!(TIMER_CTL0(timer) & (uint32_t)TIMER_CTL0_CEN))
        return; 
    if(prescaler != timer_prescaler_read(timer))
        shutdown("PWM already programmed at different speed");
}

uint32_t calc_timer_freq(rcu_clock_freq_enum rcu_clock_periph)
{
    //get CK_AHB freq
    uint32_t ahb_clock = rcu_clock_freq_get(CK_AHB);
    uint32_t apb_x_freq = rcu_clock_freq_get(rcu_clock_periph);

    //if CK_APBx != CK_AHB, then prescaler on CK_ABPx is > 1, and CK_TIMERx is double than CK_APBx
    if(ahb_clock != apb_x_freq)
        apb_x_freq *= 2;
    return apb_x_freq;
}

struct gpio_pwm
gpio_pwm_setup(uint8_t pin, uint32_t cycle_time, uint32_t val) 
{
    struct gpio_pwm_info pin_config = gpio_pwm_pins[get_gpio_pin_index(pin)];
    struct timer_info timer = timer_infos[pin_config.timer_idx];
    rcu_periph_clock_enable(timer.rcu_clock_timer);

    uint32_t timer_clock_freq = calc_timer_freq(timer.rcu_clock_periph);
    uint32_t pclock_div = CONFIG_CLOCK_FREQ / timer_clock_freq;
    uint32_t prescaler = cycle_time / (pclock_div * (MAX_PWM - 1));
    if(prescaler > UINT16_MAX)
        prescaler = UINT16_MAX;
    else if(prescaler > 0)
        prescaler -= 1;
    
    gpio_setup_af(pin_config.pin_number, GPIO_SPEED_NORMAL, 0);
    if(pin_config.remap != 0)
        gpio_pin_remap_config(pin_config.remap, ENABLE); 

    check_existing_prescaler(timer.timer, prescaler);
    
    //TODO use timer_init?
    timer_autoreload_value_config(timer.timer, MAX_PWM - 1); 
    timer_prescaler_config(timer.timer, prescaler, TIMER_PSC_RELOAD_NOW);
    // timer_prescaler_config(timer.timer, 1199, TIMER_PSC_RELOAD_NOW);
    // timer_autoreload_value_config(timer.timer, 999); 
    timer_oc_parameter_struct timer_ocintpara;
    timer_ocintpara.outputstate  = TIMER_CCX_ENABLE;
    timer_ocintpara.outputnstate = TIMER_CCXN_DISABLE;
    timer_ocintpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_ocintpara.ocnpolarity  = TIMER_OCN_POLARITY_HIGH;
    timer_ocintpara.ocidlestate  = TIMER_OC_IDLE_STATE_LOW;
    timer_ocintpara.ocnidlestate = TIMER_OCN_IDLE_STATE_LOW;
    timer_channel_output_config(timer.timer, pin_config.channel, &timer_ocintpara);
    
    
    timer_channel_output_mode_config(timer.timer, pin_config.channel, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer.timer, pin_config.channel, TIMER_OC_SHADOW_ENABLE);
    timer_channel_output_pulse_value_config(timer.timer, pin_config.channel, val);
    timer_event_software_generate(timer.timer, TIMER_EVENT_SRC_UPG);

    timer_enable(timer.timer);

    struct gpio_pwm g = {.timer = timer.timer, .channel = pin_config.channel};
    return g;
};

void
gpio_pwm_write(struct gpio_pwm g, uint32_t val) {
    unsigned prescaler = timer_prescaler_read(g.timer);
    timer_channel_output_pulse_value_config(g.timer, g.channel, val);
    timer_event_software_generate(g.timer, TIMER_EVENT_SRC_UPG);
}
