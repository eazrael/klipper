// Clock setup functions on GD32F307
//
// Copyright (C) 2025, 2026  Evil Azrael <evilazrael@evilazrael.de>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_CLOCK_REF_FREQ
#include "board/armcm_boot.h" // VectorTable
#include "board/armcm_reset.h" // try_request_canboot
#include "board/irq.h" // irq_disable
#include "board/misc.h" // bootloader_request
#include "command.h" // DECL_CONSTANT_STR
#include "internal.h" // enable_pclock
#include "sched.h" // sched_main
#include "system_gd32f30x.h"
#include "gd32f30x_rcu.h"
#include "j1_bootloader.h"

/****************************************************************
 * Clock setup
 ****************************************************************/

// Return the frequency of the given peripheral clock
uint32_t
get_pclock_frequency(uint32_t periph_base)
{
    if(periph_base >= CK_AHB) 
        return rcu_clock_freq_get(CK_AHB); 
    else if(periph_base >= CK_APB2) 
        return rcu_clock_freq_get(CK_APB2); 
    else if (periph_base >= CK_APB1)
        return rcu_clock_freq_get(CK_APB1);
        
    return rcu_clock_freq_get(CK_APB2);
}

// Enable a GPIO peripheral clock
void
gpio_clock_enable(rcu_periph_enum periph)
{
    rcu_periph_clock_enable(periph); 
}

#if !CONFIG_GD32_CLOCK_SRC_INTERNAL
DECL_CONSTANT_STR("RESERVE_PINS_crystal", "PC13,PC14");
#endif

/****************************************************************
 * Startup
 ****************************************************************/

// Main entry point - called from armcm_boot.c:ResetHandler()
void
armcm_main(void)
{
    SCB->VTOR = (uint32_t)VectorTable;
    SystemInit();

    #if CONFIG_HAVE_BOOTLOADER_REQUEST
    bootloader_handle_confirm_request();
    #endif

    sched_main();
}

/****************************************************************
 * Reset
 ****************************************************************/

void command_reset(uint32_t *__ignored)
{
    irq_disable();
    __DSB();
    NVIC_SystemReset();
}
DECL_COMMAND_FLAGS(command_reset, HF_IN_SHUTDOWN, "reset");