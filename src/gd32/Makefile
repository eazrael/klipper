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
src-y += generic/crc16_ccitt.c
src-y += generic/armcm_boot.c generic/armcm_irq.c
src-y += ../lib/gd32f3/gd32f30x_rcu.c ../lib/gd32f3/gd32f30x_gpio.c
src-y += ../lib/gd32f3/gd32f30x_fwdgt.c
src-$(CONFIG_MACH_GD32F3) += gd32/gd32f3.c ../lib/gd32f3/system_gd32f30x.c
src-$(CONFIG_HAVE_BOOTLOADER_REQUEST) += gd32/j1_bootloader.c ../lib/gd32f3/gd32f30x_fmc.c
timer-src-y := generic/armcm_timer.c
gpio-src-y := gd32/gpio.c
src-y += $(timer-src-y) $(gpio-src-y)
adc-src-y := gd32/adc.c
adc-src-$(CONFIG_MACH_GD32F3) += ../lib/gd32f3/gd32f30x_adc.c
src-$(CONFIG_WANT_ADC) += $(adc-src-y)
src-$(CONFIG_WANT_I2C) += $(i2c-src-y)
serial-src-y := gd32/serial.c ../lib/gd32f3/gd32f30x_usart.c
src-$(CONFIG_SERIAL) += $(serial-src-y) generic/serial_irq.c
src-$(CONFIG_WANT_HARD_PWM) += gd32/hard_pwm.c ../lib/gd32f3/gd32f30x_timer.c

# Binary output file rules
target-y += $(OUT)klipper.bin

$(OUT)klipper.bin: $(OUT)klipper.elf
	@echo "  Creating hex file $@"
	$(Q)$(OBJCOPY) -O binary $< $@

# This will kill the bootloeader.. 
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


	




