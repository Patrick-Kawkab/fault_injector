CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

CFLAGS  = -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -O0 -std=c99 -g -Wall

LDFLAGS = -mthumb -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -T tm4c123.ld --specs=nosys.specs -Wl,--gc-sections

all: main.bin

main.elf: main.c startup.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

main.bin: main.elf
	$(OBJCOPY) -O binary $< $@

flash: main.bin
	lm4flash $

clean:
	rm -f *.elf *.bin *.d
