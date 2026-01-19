CC = arm-none-eabi-gcc
CFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft \
         -ffunction-sections -fdata-sections \
         -std=c99 -Wall -Wextra
LDFLAGS = -T tm4c123gh6pm.ld -nostartfiles -Wl,--gc-sections

all: tiva_led.elf

tiva_led.elf: tiva_led.o vectors.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
	arm-none-eabi-size $@

tiva_led.o: tiva_led_corrected.c tm4c123gh6pm.h
	$(CC) $(CFLAGS) -c -o $@ tiva_led.c

vectors.o: vectors.c
	$(CC) $(CFLAGS) -c -o $@ vectors.c

clean:
	rm -f *.o *.elf *.d

flash: tiva_led.elf
	@echo "Use: openocd -f board/ti_ek-tm4c123gxl.cfg"
	@echo "Then in another terminal:"
	@echo "  telnet localhost 4444"
	@echo "  > reset init"
	@echo "  > halt"
	@echo "  > flash write_image erase tiva_led.elf"
	@echo "  > reset"
	@echo "  > resume"

.PHONY: all clean flash
