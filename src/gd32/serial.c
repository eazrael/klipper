// STM32 serial
//
// Copyright (C) 2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h" // CONFIG_SERIAL_BAUD
#include "board/armcm_boot.h" // armcm_enable_irq
#include "board/serial_irq.h" // serial_rx_byte
#include "command.h" // DECL_CONSTANT_STR
#include "internal.h" // enable_pclock
#include "sched.h" // DECL_INIT
#include "gd32f30x_rcu.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_usart.h"
#include "gpio.h"

// Select the configured serial port
#if CONFIG_GD32_SERIAL_USART0
#define GPIO_Tx GPIO('A', 9)
#define GPIO_Rx GPIO('A', 10)
#define RCU_GPIO_Tx RCU_GPIOA
#define RCU_GPIO_Rx RCU_GPIOA
#define USARTx USART0
#define USARTx_IRQn USART0_IRQn
#define RCU_USARTx RCU_USART0

#elif CONFIG_GD32_SERIAL_USART0_ALT_PB7_PB6
#define GPIO_Tx GPIO('B', 6)
#define GPIO_Rx GPIO('B', 7)
#define RCU_GPIO_Tx RCU_GPIOB
#define RCU_GPIO_Rx RCU_GPIOB
#define GPIO_USART_REMAP GPIO_USART0_REMAP
#define USARTx USART0
#define USARTx_IRQn USART0_IRQn
#define RCU_USARTx RCU_USART0

#elif CONFIG_GD32_SERIAL_USART1
#define GPIO_Tx GPIO('A', 2)
#define GPIO_Rx GPIO('A', 3)
#define RCU_GPIO_Tx RCU_GPIOA
#define RCU_GPIO_Rx RCU_GPIOA
#define USARTx USART1
#define USARTx_IRQn USART1_IRQn
#define RCU_USARTx RCU_USART1

#elif CONFIG_GD32_SERIAL_USART1_ALT_PD6_PD5
#define GPIO_Tx GPIO('D', 5)
#define GPIO_Rx GPIO('D', 6)
#define RCU_GPIO_Tx RCU_GPIOD
#define RCU_GPIO_Rx RCU_GPIOD
#define GPIO_USART_REMAP GPIO_USART1_REMAP
#define USARTx USART1
#define USARTx_IRQn USART1_IRQn
#define RCU_USARTx RCU_USART1

#elif CONFIG_GD32_SERIAL_USART2
#define GPIO_Tx GPIO('B', 10)
#define GPIO_Rx GPIO('B', 11)
#define RCU_GPIO_Tx RCU_GPIOB
#define RCU_GPIO_Rx RCU_GPIOB
#define USARTx USART2
#define USARTx_IRQn USART2_IRQn
#define RCU_USARTx RCU_USART2

#elif CONFIG_GD32_SERIAL_USART2_ALT_PC11_PC10
#define GPIO_Tx GPIO('C', 10)
#define GPIO_Rx GPIO('C', 11)
#define RCU_GPIO_Tx RCU_GPIOC
#define RCU_GPIO_Rx RCU_GPIOC
#define GPIO_USART_REMAP GPIO_USART2_PARTIAL_REMAP
#define USARTx USART2
#define USARTx_IRQn USART2_IRQn
#define RCU_USARTx RCU_USART2

#elif CONFIG_GD32_SERIAL_USART2_ALT_PD9_PD8
#define GPIO_Tx GPIO('D', 8)
#define GPIO_Rx GPIO('D', 9)
#define RCU_GPIO_Tx RCU_GPIOD
#define RCU_GPIO_Rx RCU_GPIOD
#define GPIO_USART_REMAP GPIO_USART2_FULL_REMAP
#define USARTx USART2
#define USARTx_IRQn USART2_IRQn
#define RCU_USARTx RCU_USART2

#else 
#error no serial port defined!
#endif

#if CONFIG_STM32_SERIAL_USART1
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PA10,PA9");
  #define GPIO_Rx GPIO('A', 10)
  #define GPIO_Tx GPIO('A', 9)
  #define GPIO_AF_MODE 7
  #define USARTx USART1
  #define USARTx_IRQn USART1_IRQn
#elif CONFIG_STM32_SERIAL_USART1_ALT_PB7_PB6
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PB7,PB6");
  #define GPIO_Rx GPIO('B', 7)
  #define GPIO_Tx GPIO('B', 6)
  #define GPIO_AF_MODE 7
  #define USARTx USART1
  #define USARTx_IRQn USART1_IRQn
#elif CONFIG_STM32_SERIAL_USART2
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PA3,PA2");
  #define GPIO_Rx GPIO('A', 3)
  #define GPIO_Tx GPIO('A', 2)
  #define GPIO_AF_MODE 7
  #define USARTx USART2
  #define USARTx_IRQn USART2_IRQn
#elif CONFIG_STM32_SERIAL_USART2_ALT_PD6_PD5
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PD6,PD5");
  #define GPIO_Rx GPIO('D', 6)
  #define GPIO_Tx GPIO('D', 5)
  #define GPIO_AF_MODE 7
  #define USARTx USART2
  #define USARTx_IRQn USART2_IRQn
#elif CONFIG_STM32_SERIAL_USART3
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PB11,PB10");
  #define GPIO_Rx GPIO('B', 11)
  #define GPIO_Tx GPIO('B', 10)
  #define GPIO_AF_MODE 7
  #define USARTx USART3
  #define USARTx_IRQn USART3_IRQn
#elif CONFIG_STM32_SERIAL_USART3_ALT_PD9_PD8
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PD9,PD8");
  #define GPIO_Rx GPIO('D', 9)
  #define GPIO_Tx GPIO('D', 8)
  #define GPIO_AF_MODE 7
  #define USARTx USART3
  #define USARTx_IRQn USART3_IRQn
#elif CONFIG_STM32_SERIAL_USART6
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PA12,PA11");
  #define GPIO_Rx GPIO('A', 12)
  #define GPIO_Tx GPIO('A', 11)
  #define GPIO_AF_MODE 8
  #define USARTx USART6
  #define USARTx_IRQn USART6_IRQn
#elif CONFIG_STM32_SERIAL_USART6_ALT_PC7_PC6
  DECL_CONSTANT_STR("RESERVE_PINS_serial", "PC7,PC6");
  #define GPIO_Rx GPIO('C', 7)
  #define GPIO_Tx GPIO('C', 6)
  #define GPIO_AF_MODE 8
  #define USARTx USART6
  #define USARTx_IRQn USART6_IRQn
#endif

void USARTx_IRQHandler(void)
{
  if(usart_flag_get(USARTx, USART_FLAG_RBNE) || usart_flag_get(USARTx, USART_FLAG_ORERR))
    serial_rx_byte(usart_data_receive(USARTx));

  if(usart_flag_get(USARTx, USART_FLAG_TBE) && usart_interrupt_flag_get(USARTx, USART_INT_FLAG_TBE))
  {
    uint8_t data;
    int ret = serial_get_tx_byte(&data);
    if (ret)
      usart_interrupt_disable(USARTx, USART_INT_TBE); 
    else
      usart_data_transmit(USARTx, data);
  }
}

void
serial_enable_tx_irq(void)
{
    usart_interrupt_enable(USARTx, USART_INT_TBE);
}

void
serial_init(void)
{
  /* enable USART clock */
  // probably always the same port, but be sure
  rcu_periph_clock_enable(RCU_GPIO_Tx);
  rcu_periph_clock_enable(RCU_GPIO_Rx);
  rcu_periph_clock_enable(RCU_USARTx);
  rcu_periph_clock_enable(RCU_AF);

#ifdef GPIO_USART_REMAP
  gpio_pin_remap_config(GPIO_USART_REMAP, ENABLE);
#endif

  /* connect port to USARTx_Tx */
  //gpio_init(gpio_periph_tx, GPIO_MODE_AF_PP, GPIO_SPEED_NORMAL, GPIO2BIT(GPIO_Tx));
  gpio_setup_af(GPIO_Tx, GPIO_SPEED_NORMAL, 0); 

  /* connect port to USARTx_Rx */
  //TODO: hide GPIO_MODE_IN_FLOATING
  gpio_setup_in(GPIO_Rx, GPIO_MODE_IN_FLOATING, GPIO_SPEED_NORMAL); 

  /* USART configure */
  usart_deinit(USARTx);
  usart_baudrate_set(USARTx, CONFIG_SERIAL_BAUD);
  usart_word_length_set(USARTx, USART_WL_8BIT);
  usart_stop_bit_set(USARTx, USART_STB_1BIT);
  usart_parity_config(USARTx, USART_PM_NONE);

  usart_receive_config(USARTx, USART_RECEIVE_ENABLE);
  usart_transmit_config(USARTx, USART_TRANSMIT_ENABLE);
  usart_interrupt_enable(USARTx, USART_INT_RBNE);

  usart_enable(USARTx);
  armcm_enable_irq(USARTx_IRQHandler, USARTx_IRQn, 0);
}
DECL_INIT(serial_init);
