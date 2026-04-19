// GPIO functions on GD32F307
//
// Copyright (C) 2024, 2025  Evil Azrael
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include <string.h> // ffs
#include "board/irq.h" // irq_save
#include "command.h" // DECL_ENUMERATION_RANGE
#include "gpio.h" // gpio_out_setup
#include "internal.h" // gpio_peripheral
#include "sched.h" // sched_shutdown
// #include "gd32f30x.h"
#include "gd32f30x_gpio.h"
#include "gpio.h" 

// Terminology note:
// The term "pin" can refer to either:
// - an absolute pin index (e.g. GPIO number in a linear sequence), where the port is pin / 16,
// - or the pin number within a port (0–15).
// The term "bit" refers to a bitmask for a port, where the bit corresponding to the pin is set (e.g. (1 << pin)).
// pinbit = bit in the bitmask of the port

// TODO: usable pins depend on package
DECL_ENUMERATION_RANGE("pin", "PA0", GPIO('A', 0), 16);
DECL_ENUMERATION_RANGE("pin", "PB0", GPIO('B', 0), 16);
DECL_ENUMERATION_RANGE("pin", "PC0", GPIO('C', 0), 16);
DECL_ENUMERATION_RANGE("pin", "PD0", GPIO('D', 0), 16);
DECL_ENUMERATION_RANGE("pin", "PE0", GPIO('E', 0), 16);
// DECL_ENUMERATION_RANGE("pin", "PE0", GPIO('E', 0), 16);
// DECL_ENUMERATION_RANGE("pin", "PF0", GPIO('F', 0), 16);
// #ifdef GPIOE
// DECL_ENUMERATION_RANGE("pin", "PE0", GPIO('E', 0), 16);
// #endif
// #ifdef GPIOF
// DECL_ENUMERATION_RANGE("pin", "PF0", GPIO('F', 0), 16);
// #endif
#define MAX_GPIO_PORTS 5

static const uint32_t PORT_RCU[] = 
{
    RCU_GPIOA,
    RCU_GPIOB,
    RCU_GPIOC,
    RCU_GPIOD,
    RCU_GPIOE /*,
    RCU_GPIOF */
};

// gpio_periph: GPIOx(x = A,B,C,D,E,F,G)
static const uint32_t GPIO_PERIPH[] = 
{
    GPIOA,
    GPIOB,
    GPIOC,
    GPIOD,
    GPIOE /*,
    GPIOF,
    GPIOG */
};

//TODO silly? just take the bit directly
// gpio_pin: GPIO_PIN_x(x=0..15)
// static const uint32_t GPIO_PIN[] =
// {
//     GPIO_PIN_0, 
//     GPIO_PIN_1, 
//     GPIO_PIN_2, 
//     GPIO_PIN_3, 
//     GPIO_PIN_4, 
//     GPIO_PIN_5, 
//     GPIO_PIN_6, 
//     GPIO_PIN_7, 
//     GPIO_PIN_8, 
//     GPIO_PIN_9, 
//     GPIO_PIN_10, 
//     GPIO_PIN_11, 
//     GPIO_PIN_12, 
//     GPIO_PIN_13, 
//     GPIO_PIN_14, 
//     GPIO_PIN_15
// };

// Verify that a gpio is a valid pin
//TODO? support for skipped GPIOs and sizeof() instead of constant?
static void
check_valid_gpio_pin(uint32_t pin_number)
{
    uint32_t port = GPIO2PORT(pin_number);
    if(MAX_GPIO_PORTS <= port)
        shutdown("Not a valid gpio pin");
}

static void enable_gpio_clock(uint32_t pin_number) 
{
    uint32_t rcu_periph_enum = PORT_RCU[GPIO2PORT(pin_number)];
    rcu_periph_clock_enable(rcu_periph_enum); 
}

struct gpio_out gpio_setup_out(uint32_t pin_number) {
    check_valid_gpio_pin(pin_number);
    enable_gpio_clock(pin_number); 

    uint32_t gpio_periph = GPIO_PERIPH[GPIO2PORT(pin_number)];
    uint32_t gpio_pin = GPIO2BIT(pin_number);

    struct gpio_out g = {.gpio_periph = gpio_periph, .gpio_pin = gpio_pin};
    gpio_init(gpio_periph, GPIO_MODE_OUT_PP, GPIO_SPEED_NORMAL, gpio_pin);
    return g;
}


struct gpio_out
gpio_out_setup(uint32_t pin_number, uint32_t val)
{
    output("Starting output port pin=%u val=%u", pin_number, val); 
    struct gpio_out g = gpio_setup_out(pin_number); 
    gpio_out_reset(g, val);
    return g;
}

void
gpio_out_reset(struct gpio_out g, uint32_t val)
{
    irqstatus_t flag = irq_save();
    if (val)
        gpio_bit_set(g.gpio_periph, g.gpio_pin); 
    else
        gpio_bit_reset(g.gpio_periph, g.gpio_pin);
    output("Pin port=%u pin=%u", g.gpio_periph, g.gpio_pin);
    irq_restore(flag);
}

//TODO: safer to use SDK functions?
void
gpio_out_toggle_noirq(struct gpio_out g)
{
    GPIO_OCTL(g.gpio_periph) ^= g.gpio_pin;
}

void
gpio_out_toggle(struct gpio_out g)
{
    irqstatus_t flag = irq_save();
    gpio_out_toggle_noirq(g);
    irq_restore(flag);
}

void
gpio_out_write(struct gpio_out g, uint32_t val)
{
    gpio_bit_write(g.gpio_periph, g.gpio_pin, val ? SET : RESET); 
}

struct gpio_in
gpio_setup_in(uint32_t pin_number, uint32_t mode, uint32_t speed)
{
    output("gpio_setup_in pin_number=%u, mode=%u, speed=%u", pin_number, mode, speed); 
    check_valid_gpio_pin(pin_number);
    enable_gpio_clock(pin_number); 

    uint32_t gpio_periph = GPIO_PERIPH[GPIO2PORT(pin_number)];
    uint32_t gpio_pin = GPIO2BIT(pin_number);

    struct gpio_in g = {.gpio_periph = gpio_periph, .gpio_pin = gpio_pin};
    gpio_init(gpio_periph, mode, speed, gpio_pin); 
    return g; 
}

struct gpio_in 
gpio_setup_adc(uint32_t pin_number, uint32_t speed) 
{
    output("gpio_setup_adc pin_number=%u, speed=%u", pin_number, speed); 
    check_valid_gpio_pin(pin_number); 
    enable_gpio_clock(pin_number); 

    uint32_t gpio_periph = GPIO_PERIPH[GPIO2PORT(pin_number)];
    uint32_t gpio_pin = GPIO2BIT(pin_number);

    struct gpio_in g = {.gpio_periph = gpio_periph, .gpio_pin = gpio_pin};
    gpio_init(gpio_periph, GPIO_MODE_AIN, speed, gpio_pin); 
    return g; 
}


struct gpio_out 
gpio_setup_af(uint32_t pin_number, uint32_t speed, uint32_t open_drain)
{
    output("gpio_setup_af pin_number=%u, speed=%u, open_drain=%u", pin_number, speed, open_drain); 
    check_valid_gpio_pin(pin_number); 
    enable_gpio_clock(pin_number);
    rcu_periph_clock_enable(RCU_AF); 

    uint32_t gpio_periph = GPIO_PERIPH[GPIO2PORT(pin_number)];
    uint32_t gpio_pin = GPIO2BIT(pin_number);
    uint32_t mode = open_drain ? GPIO_MODE_AF_OD : GPIO_MODE_AF_PP; 

    struct gpio_out g = {.gpio_periph = gpio_periph, .gpio_pin = gpio_pin};
    gpio_init(gpio_periph, mode, speed, gpio_pin); 
    return g; 
}
// gpio_setup_adc(); 

static uint32_t input_mode_by_pull_up(int32_t pull_up) 
{
    if(pull_up == 0)
        return GPIO_MODE_IN_FLOATING;
    return pull_up < 0 ? GPIO_MODE_IPD : GPIO_MODE_IPU; 
}

struct gpio_in
gpio_in_setup(uint32_t pin_number, int32_t pull_up)
{
    uint32_t mode = input_mode_by_pull_up(pull_up);
    struct gpio_in g = gpio_setup_in(pin_number, mode, GPIO_SPEED_NORMAL); 
    gpio_in_reset(g, pull_up);
    return g;
}

void
gpio_in_reset(struct gpio_in g, int32_t pull_up)
{
    uint32_t mode = input_mode_by_pull_up(pull_up);
    irqstatus_t flag = irq_save();
    gpio_init(g.gpio_periph, mode,  GPIO_SPEED_NORMAL, g.gpio_pin);
    irq_restore(flag);
}

uint8_t
gpio_in_read(struct gpio_in g)
{
    return gpio_input_bit_get(g.gpio_periph, g.gpio_pin) == SET; 
}
