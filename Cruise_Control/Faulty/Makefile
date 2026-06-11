CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

# ── Paths ──────────────────────────────────────
FREERTOS_DIR = FreeRTOS
PORT_DIR     = $(FREERTOS_DIR)/portable/GCC/ARM_CM4F
HEAP_DIR     = $(FREERTOS_DIR)/portable/MemMang

# ── Source Files ───────────────────────────────
SRCS = main.c \
       startup.c \
       $(FREERTOS_DIR)/tasks.c \
       $(FREERTOS_DIR)/queue.c \
       $(FREERTOS_DIR)/list.c \
       $(FREERTOS_DIR)/timers.c \
       $(PORT_DIR)/port.c \
       $(HEAP_DIR)/heap_4.c

# ── Include Paths ──────────────────────────────
INCLUDES = -I. \
           -I$(FREERTOS_DIR)/include \
           -I$(PORT_DIR)

# ── Compiler Flags ─────────────────────────────
CFLAGS  = -mthumb \
          -mcpu=cortex-m4 \
          -mfpu=fpv4-sp-d16 \
          -mfloat-abi=hard \
          -O0 \
          -std=c99 \
          -g \
          -Wall \
          -ffunction-sections \
          -fdata-sections \
          $(INCLUDES)

# ── Linker Flags ───────────────────────────────
LDFLAGS = -mthumb \
          -mcpu=cortex-m4 \
          -mfpu=fpv4-sp-d16 \
          -mfloat-abi=hard \
          -T tm4c123.ld \
          -specs=nosys.specs \
          -Wl,--gc-sections \
          -Wl,-Map=main.map

# ── Targets ────────────────────────────────────
all: main.bin

main.elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
	arm-none-eabi-size $@

main.bin: main.elf
	$(OBJCOPY) -O binary $< $@

flash: main.bin
	sudo lm4flash main.bin

clean:
	rm -f *.elf *.bin *.map

.PHONY: all clean flash