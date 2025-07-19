# Additional GD32 build rules
# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/gd32 src/generic lib/fast-hash
dirs-$(CONFIG_MACH_GD32F3) += lib/gd32f3


MCU := $(shell echo $(CONFIG_MCU))
MCU_UPPER := $(shell echo $(CONFIG_MCU) | tr a-z A-Z | tr X x)

CFLAGS-$(CONFIG_HAVE_LIMITED_CODE_SIZE) += -Os -Wfatal-errors
CFLAGS-$(CONFIG_MACH_GD32F3) += -mcpu=cortex-m4 -Ilib/gd32f3/include -DGD32F30X_CL
CFLAGS += $(CFLAGS-y) -D$(MCU_UPPER) -mthumb -Ilib/cmsis-core -Ilib/fast-hash
CFLAGS += -D$(CONFIG_GD32_CLOCK_SRC)

CFLAGS_klipper.elf += -nostdlib -lgcc -lc_nano
CFLAGS_klipper.elf += -T $(OUT)src/generic/armcm_link.ld
$(OUT)klipper.elf: $(OUT)src/generic/armcm_link.ld

# Add source files
src-y += gd32/watchdog.c
src-y += generic/crc16_ccitt.c
src-y += generic/armcm_boot.c generic/armcm_irq.c
src-y += ../lib/gd32f3/gd32f30x_rcu.c ../lib/gd32f3/gd32f30x_gpio.c
src-y += ../lib/gd32f3/gd32f30x_fwdgt.c
src-$(CONFIG_MACH_GD32F3) += gd32/gd32f3.c ../lib/gd32f3/system_gd32f30x.c
src-$(CONFIG_HAVE_BOOTLOADER_REQUEST) += gd32/j1_bootloader.c ../lib/gd32f3/gd32f30x_fmc.c
timer-src-y := generic/armcm_timer.c
gpio-src-y := gd32/gpio.c
#gpio-src-$(CONFIG_MACH_GD32F3) := gd32/gpio.c
src-y += $(timer-src-y) $(gpio-src-y)
adc-src-y := gd32/adc.c
adc-src-$(CONFIG_MACH_GD32F3) += ../lib/gd32f3/gd32f30x_adc.c
src-$(CONFIG_WANT_ADC) += $(adc-src-y)
# spi-src-y := stm32/spi.c
# spi-src-$(CONFIG_MACH_STM32H7) := stm32/stm32h7_spi.c
# src-$(CONFIG_WANT_SPI) += $(spi-src-y)
# i2c-src-y := stm32/stm32f0_i2c.c
# i2c-src-$(CONFIG_MACH_STM32F1) := stm32/i2c.c
# i2c-src-$(CONFIG_MACH_STM32F2) := stm32/i2c.c
# i2c-src-$(CONFIG_MACH_STM32F4) := stm32/i2c.c
src-$(CONFIG_WANT_I2C) += $(i2c-src-y)
serial-src-y := gd32/serial.c ../lib/gd32f3/gd32f30x_usart.c
src-$(CONFIG_SERIAL) += $(serial-src-y) generic/serial_irq.c
# usb-src-$(CONFIG_HAVE_STM32_USBFS) := stm32/usbfs.c
# usb-src-$(CONFIG_HAVE_STM32_USBOTG) := stm32/usbotg.c
# src-$(CONFIG_USBSERIAL) += $(usb-src-y) stm32/chipid.c generic/usb_cdc.c
# canbus-src-y := generic/canserial.c ../lib/fast-hash/fasthash.c
# canbus-src-$(CONFIG_HAVE_STM32_CANBUS) += stm32/can.c
# canbus-src-$(CONFIG_HAVE_STM32_FDCANBUS) += stm32/fdcan.c
# src-$(CONFIG_CANSERIAL) += $(canbus-src-y) generic/canbus.c stm32/chipid.c
# src-$(CONFIG_USBCANBUS) += $(usb-src-y) $(canbus-src-y)
# src-$(CONFIG_USBCANBUS) += stm32/chipid.c generic/usb_canbus.c
src-$(CONFIG_WANT_HARD_PWM) += gd32/hard_pwm.c ../lib/gd32f3/gd32f30x_timer.c
# src-$(CONFIG_HAVE_GPIO_SDIO) += stm32/sdio.c

# Binary output file rules
target-y += $(OUT)klipper.bin

$(OUT)klipper.bin: $(OUT)klipper.elf
	@echo "  Creating hex file $@"
	$(Q)$(OBJCOPY) -O binary $< $@

# Flash rules
# lib/hidflash/hid-flash:
# 	@echo "  Building hid-flash"
# 	$(Q)make -C lib/hidflash

# flash: $(OUT)klipper.bin lib/hidflash/hid-flash
# 	@echo "  Flashing $< to $(FLASH_DEVICE)"
# 	$(Q)$(PYTHON) ./scripts/flash_usb.py -t $(CONFIG_MCU) -d "$(FLASH_DEVICE)" -s "$(CONFIG_FLASH_APPLICATION_ADDRESS)" $(if $(NOSUDO),--no-sudo) $(OUT)klipper.bin

# serialflash: $(OUT)klipper.bin
# 	@echo "  Flashing $< to $(FLASH_DEVICE) via stm32flash"
# 	$(Q)stm32flash -w $< -v -g 0 $(FLASH_DEVICE)

openocdflash: $(OUT)klipper.bin
	@echo "  Flashing $< to $(FLASH_DEVICE) via openocd"
	openocd -f ~/openocd/stlink.cfg \
		-f ~/openocd/stm32f1x.cfg \
		-c "adapter speed 50" \
		-c init \
		-c "reset halt"  \
		-c "echo Flashing..." \
		-c "flash write_image erase $< 0x08000000" \
		-c "echo Verifying..." \
		-c "verify_image $< 0x08000000" \
		-c "echo Resetting" \
		-c "reset run" \
		-c "exit"

j1flash: $(OUT)klipper.bin
	@echo "  Flashing $< to $(FLASH_DEVICE) from klipper via SACP"
	python3 scripts/j1_flash_firmware.py --klipper --port /dev/ttyMSM1 $<
	@echo "  Flashing complete, restart klipper"

j1flash-initial: $(OUT)klipper.bin
	@echo "  Flashing $< to $(FLASH_DEVICE) via SACP"
	python3 scripts/j1_flash_firmware.py --port /dev/ttyMSM1 $<
	@echo "  Flashing complete, restart klipper"


	




