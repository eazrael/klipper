#ifndef __GD32_INTERNAL_H
#define __GD32_INTERNAL_H
// Local definitions for STM32 code

#include "autoconf.h" // CONFIG_MACH_STM32F1
#include <stdint.h>

#if CONFIG_MACH_GD32F3
#include "gd32f30x.h"
#include "gd32f30x_rcu.h"
#endif

// gpio.c
//extern GPIO_TypeDef * const digital_regs[];
#define GPIO(PORT, NUM) (((PORT)-'A') * 16 + (NUM))
#define GPIO2PORT(PIN) ((PIN) / 16)
#define GPIO2BIT(PIN) (1<<((PIN) % 16))

// gpioperiph.c
#define GPIO_INPUT 0
#define GPIO_OUTPUT 1
#define GPIO_OPEN_DRAIN 0x100
#define GPIO_HIGH_SPEED 0x200
#define GPIO_FUNCTION(fn) (2 | ((fn) << 4))
#define GPIO_ANALOG 3
void gpio_peripheral(uint32_t gpio, uint32_t mode, int pullup);

// clockline.c
void enable_pclock(uint32_t periph_base);
int is_enabled_pclock(uint32_t periph_base);

// stm32??.c
uint32_t get_pclock_frequency(uint32_t periph_base);
void gpio_clock_enable(rcu_periph_enum periph);

// reset the MCU
void command_reset(uint32_t*); 

#endif // internal.h
