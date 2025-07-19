#ifndef __GD32_GPIO_H
#define __GD32_GPIO_H

#include <stdint.h> // uint32_t

struct gpio_out {
     uint32_t gpio_periph; // GPIOx(x = A,B,C,D,E,F,G)
     uint32_t gpio_pin; //  GPIO_PIN_x(x=0..15)
 };

struct gpio_out gpio_out_setup(uint32_t pin, uint32_t val);
void gpio_out_reset(struct gpio_out g, uint32_t val);
void gpio_out_toggle_noirq(struct gpio_out g);
void gpio_out_toggle(struct gpio_out g);
void gpio_out_write(struct gpio_out g, uint32_t val);

struct gpio_in {
    uint32_t gpio_periph; // GPIOx(x = A,B,C,D,E,F,G)
    uint32_t gpio_pin; //  GPIO_PIN_x(x=0..15)
};

struct gpio_in gpio_in_setup(uint32_t pin, int32_t pull_up);
void gpio_in_reset(struct gpio_in g, int32_t pull_up);
uint8_t gpio_in_read(struct gpio_in g);

struct gpio_pwm {
    uint32_t timer;
    uint16_t channel;
};
struct gpio_pwm gpio_pwm_setup(uint8_t pin, uint32_t cycle_time, uint32_t val);
void gpio_pwm_write(struct gpio_pwm g, uint32_t val);

struct gpio_adc {
    uint32_t adc;
    uint32_t channel;
};
struct gpio_adc gpio_adc_setup(uint32_t pin);
uint32_t gpio_adc_sample(struct gpio_adc g);
uint16_t gpio_adc_read(struct gpio_adc g);
void gpio_adc_cancel_sample(struct gpio_adc g);

struct spi_config {
    void *spi;
    union {
        uint32_t spi_cr1;
        struct {
            uint8_t div;
            uint8_t mode;
        };
    };
};
struct spi_config spi_setup(uint32_t bus, uint8_t mode, uint32_t rate);
void spi_prepare(struct spi_config config);
void spi_transfer(struct spi_config config, uint8_t receive_data
                  , uint8_t len, uint8_t *data);

struct i2c_config {
    void *i2c;
    uint8_t addr;
};

struct i2c_config i2c_setup(uint32_t bus, uint32_t rate, uint8_t addr);
int i2c_write(struct i2c_config config, uint8_t write_len, uint8_t *write);
int i2c_read(struct i2c_config config, uint8_t reg_len, uint8_t *reg
             , uint8_t read_len, uint8_t *read);

//Not sure what to set
#define GPIO_SPEED_HIGH GPIO_OSPEED_MAX
#define GPIO_SPEED_NORMAL GPIO_OSPEED_50MHZ

// internal functions:
struct gpio_out gpio_setup_af(uint32_t pin_number, uint32_t speed, uint32_t open_drain);
struct gpio_in gpio_setup_in(uint32_t pin_number, uint32_t mode, uint32_t speed);
struct gpio_out gpio_setup_out(uint32_t pin_number);
struct gpio_in gpio_setup_adc(uint32_t pin_number, uint32_t speed);

#endif // gpio.h
