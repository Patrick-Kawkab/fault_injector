# =============================================================================
#  Makefile — builds temperature_monitor.elf for lm3s6965evb (Cortex-M3)
#
#  Requirements
#  ────────────
#    sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi
#
#  Targets
#  ───────
#    make              — build temperature_monitor.elf
#    make clean        — remove build artifacts
#    make info         — print section addresses (use for Input.json)
#    make run          — run under QEMU without fault injection (sanity check)
# =============================================================================

CC      := arm-none-eabi-gcc
OBJDUMP := arm-none-eabi-objdump
NM      := arm-none-eabi-nm

TARGET  := temperature_monitor.elf
SRCS    := startup.c main.c
LD      := lm3s6965evb.ld


CFLAGS  := -mcpu=cortex-m3        \
            -mthumb                \
            -O1                    \
            -Wall -Wextra          \
            -ffreestanding         \
            -fno-builtin           \
            -fno-omit-frame-pointer \
            -g                     \
            -T $(LD)               \
            -nostartfiles          \
            -nostdlib

# -O1 not -O2: keeps evaluate_safety() as a real call site so
# instruction_skip has a clear, stable PC target to patch

.PHONY: all clean info run

all: $(TARGET)

$(TARGET): $(SRCS) $(LD)
	$(CC) $(CFLAGS) -o $@ $(SRCS)
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Run 'make info' to get symbol addresses for Input.json"
	@echo ""

# Print symbol addresses — copy these into your Input.json
info: $(TARGET)
	@echo ""
	@echo "========================================"
	@echo "  Symbol addresses (for Input.json)"
	@echo "========================================"
	@$(NM) --demangle -n $(TARGET) | grep -E \
	    "g_sensor_temp|g_status|g_cooling|g_cycle|evaluate_safety"
	@echo ""
	@echo "  Fault type         target_addr / inject_addr"
	@echo "  ─────────────────────────────────────────────"
	@echo "  memory_corruption  inject_addr  = g_sensor_temp address"
	@echo "  bit_flip           inject_addr  = g_sensor_temp address"
	@echo "  instruction_skip   target_addr  = evaluate_safety address"
	@echo "  set_pc             new_pc       = any address (e.g. 0x00000000)"
	@echo "  sensor_corruption  sensor_addr  = g_sensor_temp address"
	@echo ""

clean:
	rm -f $(TARGET)

# Sanity check — run without fault injection, UART output should be normal
run: $(TARGET)
	qemu-system-arm          \
	    -machine lm3s6965evb \
	    -cpu cortex-m3       \
	    -kernel $(TARGET)    \
	    -nographic           \
	    -semihosting
