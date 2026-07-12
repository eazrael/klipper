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
#include "gd32f30x_dma.h"
#include "gpio.h"

// Select the configured serial port
#if CONFIG_GD32_SERIAL_USART0
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PA9,PA10");
#define GPIO_Tx GPIO('A', 9)
#define GPIO_Rx GPIO('A', 10)
#define RCU_GPIO_Tx RCU_GPIOA
#define RCU_GPIO_Rx RCU_GPIOA
#define USARTx USART0
#define USARTx_IRQn USART0_IRQn
#define RCU_USARTx RCU_USART0

#elif CONFIG_GD32_SERIAL_USART0_ALT_PB7_PB6
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PB6,PB7");
#define GPIO_Tx GPIO('B', 6)
#define GPIO_Rx GPIO('B', 7)
#define RCU_GPIO_Tx RCU_GPIOB
#define RCU_GPIO_Rx RCU_GPIOB
#define GPIO_USART_REMAP GPIO_USART0_REMAP
#define USARTx USART0
#define USARTx_IRQn USART0_IRQn
#define RCU_USARTx RCU_USART0

#elif CONFIG_GD32_SERIAL_USART1
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PA2,PA3");
#define GPIO_Tx GPIO('A', 2)
#define GPIO_Rx GPIO('A', 3)
#define RCU_GPIO_Tx RCU_GPIOA
#define RCU_GPIO_Rx RCU_GPIOA
#define USARTx USART1
#define USARTx_IRQn USART1_IRQn
#define RCU_USARTx RCU_USART1

#elif CONFIG_GD32_SERIAL_USART1_ALT_PD6_PD5
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PD5,PD6");
#define GPIO_Tx GPIO('D', 5)
#define GPIO_Rx GPIO('D', 6)
#define RCU_GPIO_Tx RCU_GPIOD
#define RCU_GPIO_Rx RCU_GPIOD
#define GPIO_USART_REMAP GPIO_USART1_REMAP
#define USARTx USART1
#define USARTx_IRQn USART1_IRQn
#define RCU_USARTx RCU_USART1

#elif CONFIG_GD32_SERIAL_USART2
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PB10,PB11");
#define GPIO_Tx GPIO('B', 10)
#define GPIO_Rx GPIO('B', 11)
#define RCU_GPIO_Tx RCU_GPIOB
#define RCU_GPIO_Rx RCU_GPIOB
#define USARTx USART2
#define USARTx_IRQn USART2_IRQn
#define RCU_USARTx RCU_USART2

#elif CONFIG_GD32_SERIAL_USART2_ALT_PC11_PC10
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PC10,PC11");
#define GPIO_Tx GPIO('C', 10)
#define GPIO_Rx GPIO('C', 11)
#define RCU_GPIO_Tx RCU_GPIOC
#define RCU_GPIO_Rx RCU_GPIOC
#define GPIO_USART_REMAP GPIO_USART2_PARTIAL_REMAP
#define USARTx USART2
#define USARTx_IRQn USART2_IRQn
#define RCU_USARTx RCU_USART2

#elif CONFIG_GD32_SERIAL_USART2_ALT_PD9_PD8
DECL_CONSTANT_STR("RESERVE_PINS_serial", "PD8,PD9");
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

#if CONFIG_GD32_SERIAL_DMA

// DMA0 channel mapping for the selected USART
#if CONFIG_GD32_SERIAL_USART0 || CONFIG_GD32_SERIAL_USART0_ALT_PB7_PB6
#define DMA_CH_RX 4
#define DMA_CH_TX 3
#define DMA_RX_IRQn DMA0_Channel4_IRQn
#define DMA_TX_IRQn DMA0_Channel3_IRQn
#elif CONFIG_GD32_SERIAL_USART1 || CONFIG_GD32_SERIAL_USART1_ALT_PD6_PD5
#define DMA_CH_RX 5
#define DMA_CH_TX 6
#define DMA_RX_IRQn DMA0_Channel5_IRQn
#define DMA_TX_IRQn DMA0_Channel6_IRQn
#else
#define DMA_CH_RX 2
#define DMA_CH_TX 1
#define DMA_RX_IRQn DMA0_Channel2_IRQn
#define DMA_TX_IRQn DMA0_Channel1_IRQn
#endif

#define RX_DMA_BUF_SIZE 128 // must be a power of two

static uint8_t rx_dma_buf[RX_DMA_BUF_SIZE];
static uint32_t rx_dma_pos;
static uint8_t tx_dma_buf[64];

// Feed bytes the RX DMA channel stored in rx_dma_buf to klipper. Runs
// from the USART (idle line) and RX DMA (buffer wrap) irqs - both use
// the same priority, so they can not preempt each other.
static void
rx_dma_drain(void)
{
  uint32_t rpos = rx_dma_pos;
  uint32_t wpos = ((RX_DMA_BUF_SIZE - DMA_CHCNT(DMA0, DMA_CH_RX))
                   & (RX_DMA_BUF_SIZE - 1));
  while (rpos != wpos) {
    serial_rx_byte(rx_dma_buf[rpos]);
    rpos = (rpos + 1) & (RX_DMA_BUF_SIZE - 1);
  }
  rx_dma_pos = rpos;
}

void USARTx_IRQHandler(void)
{
  if (usart_flag_get(USARTx, USART_FLAG_IDLE)
      || usart_flag_get(USARTx, USART_FLAG_ORERR)) {
    // Reading STAT0 (usart_flag_get) followed by DATA clears the flags
    usart_data_receive(USARTx);
    rx_dma_drain();
  }
}

void DMA_RX_IRQHandler(void)
{
  DMA_INTC(DMA0) = DMA_FLAG_ADD(DMA_INTC_GIFC | DMA_INTC_FTFIFC
                                | DMA_INTC_HTFIFC, DMA_CH_RX);
  rx_dma_drain();
}

// Start or continue transmission - runs on DMA transfer completion and
// when made pending by serial_enable_tx_irq()
void DMA_TX_IRQHandler(void)
{
  uint32_t ctl = DMA_CHCTL(DMA0, DMA_CH_TX);
  if (ctl & DMA_CHXCTL_CHEN) {
    if (DMA_CHCNT(DMA0, DMA_CH_TX))
      // Transfer still in progress - transfer complete irq will follow
      return;
    DMA_CHCTL(DMA0, DMA_CH_TX) = ctl & ~DMA_CHXCTL_CHEN;
  }
  DMA_INTC(DMA0) = DMA_FLAG_ADD(DMA_INTC_GIFC | DMA_INTC_FTFIFC, DMA_CH_TX);
  uint32_t count = 0;
  while (count < sizeof(tx_dma_buf)
         && !serial_get_tx_byte(&tx_dma_buf[count]))
    count++;
  if (count) {
    DMA_CHMADDR(DMA0, DMA_CH_TX) = (uint32_t)tx_dma_buf;
    DMA_CHCNT(DMA0, DMA_CH_TX) = count;
    DMA_CHCTL(DMA0, DMA_CH_TX) |= DMA_CHXCTL_CHEN;
  }
}

void
serial_enable_tx_irq(void)
{
    NVIC_SetPendingIRQ(DMA_TX_IRQn);
}

#else // !CONFIG_GD32_SERIAL_DMA

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

#endif // CONFIG_GD32_SERIAL_DMA

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

#if CONFIG_GD32_SERIAL_DMA
  rcu_periph_clock_enable(RCU_DMA0);

  /* receive: circular DMA transfers into rx_dma_buf */
  DMA_CHCTL(DMA0, DMA_CH_RX) = DMA_CHCTL_RESET_VALUE;
  DMA_CHPADDR(DMA0, DMA_CH_RX) = (uint32_t)&USART_DATA(USARTx);
  DMA_CHMADDR(DMA0, DMA_CH_RX) = (uint32_t)rx_dma_buf;
  DMA_CHCNT(DMA0, DMA_CH_RX) = RX_DMA_BUF_SIZE;
  DMA_CHCTL(DMA0, DMA_CH_RX) = (DMA_PRIORITY_HIGH | DMA_CHXCTL_MNAGA
                                | DMA_CHXCTL_CMEN | DMA_CHXCTL_HTFIE
                                | DMA_CHXCTL_FTFIE | DMA_CHXCTL_CHEN);

  /* transmit: one-shot DMA transfers from tx_dma_buf, enabled on demand */
  DMA_CHCTL(DMA0, DMA_CH_TX) = DMA_CHCTL_RESET_VALUE;
  DMA_CHPADDR(DMA0, DMA_CH_TX) = (uint32_t)&USART_DATA(USARTx);
  DMA_CHCTL(DMA0, DMA_CH_TX) = (DMA_PRIORITY_MEDIUM | DMA_CHXCTL_MNAGA
                                | DMA_CHXCTL_DIR | DMA_CHXCTL_FTFIE);

  usart_dma_receive_config(USARTx, USART_RECEIVE_DMA_ENABLE);
  usart_dma_transmit_config(USARTx, USART_TRANSMIT_DMA_ENABLE);
  usart_interrupt_enable(USARTx, USART_INT_IDLE);

  usart_enable(USARTx);
  armcm_enable_irq(USARTx_IRQHandler, USARTx_IRQn, 0);
  armcm_enable_irq(DMA_RX_IRQHandler, DMA_RX_IRQn, 0);
  armcm_enable_irq(DMA_TX_IRQHandler, DMA_TX_IRQn, 0);
#else
  usart_interrupt_enable(USARTx, USART_INT_RBNE);

  usart_enable(USARTx);
  armcm_enable_irq(USARTx_IRQHandler, USARTx_IRQn, 0);
#endif
}
DECL_INIT(serial_init);
