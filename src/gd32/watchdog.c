// Watchdog handler on STM32
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_MACH_STM32H7
#include "internal.h" // IWDG
#include "sched.h" // DECL_TASK
#include "gd32f30x_fwdgt.h"

#if 0
void
watchdog_reset(void)
{
    fwdgt_counter_reload();
}
DECL_TASK(watchdog_reset);

void
watchdog_init(void)
{
    fwdgt_config(0x0FFF, 0);
    fwdgt_enable(); 
}
DECL_INIT(watchdog_init);
#endif