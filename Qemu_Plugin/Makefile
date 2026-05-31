# =============================================================================
#  Makefile — builds fault_plugin.so (QEMU TCG plugin)
#
#  Prerequisites
#  ─────────────
#  • QEMU source tree with include/qemu/qemu-plugin.h
#  • FaultConfig.h must be reachable via FAULT_INC
#  • glib-2.0  (sudo apt install libglib2.0-dev)
#  • gcc with PIC support (standard on Linux)
#
#  Typical invocation
#  ──────────────────
#    make                        # uses paths below
#    make FAULT_INC=/other/path  # override FaultConfig.h location
#    make clean
#    make info                   # print resolved paths before building
#
#  Output
#  ──────
#    fault_plugin.so  — loaded by QEMU via -plugin ./fault_plugin.so,server=...
# =============================================================================

# ---------- Paths -------------------------------------------------------------

# Directory containing qemu-plugin.h
PLUGIN_INC ?= /home/zyad/qemu/include/plugins

# Directory containing FaultConfig.h (one level up since plugin is in subfolder)
FAULT_INC  ?= ../

# glib-2.0
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0 2>/dev/null)
ifeq ($(GLIB_CFLAGS),)
    GLIB_CFLAGS := -I/usr/include/glib-2.0 \
                   -I/usr/lib/x86_64-linux-gnu/glib-2.0/include
endif

# ---------- Toolchain ---------------------------------------------------------
CC      := gcc
CFLAGS  := -O2 -Wall -Wextra      \
            -fPIC                  \
            -I$(PLUGIN_INC)        \
            -I$(FAULT_INC)         \
            $(GLIB_CFLAGS)         \
            -D_GNU_SOURCE

LDFLAGS := -shared -fPIC

# ---------- Sources -----------------------------------------------------------
TARGET     := fault_plugin.so
SRC        := fault_plugin.c

# Use the resolved path directly so make can find the file
FAULT_CONFIG_H := $(FAULT_INC)FaultConfig.h

# ---------- Rules -------------------------------------------------------------
.PHONY: all clean info

all: $(TARGET)

# Depend on the header via its resolved path, not a bare filename
$(TARGET): $(SRC) $(FAULT_CONFIG_H)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)
	@echo ""
	@echo "  Built  : $(TARGET)"
	@echo "  Load with:"
	@echo "    -plugin ./$(TARGET),server=localhost:9001"
	@echo ""

clean:
	rm -f $(TARGET)

info:
	@echo "PLUGIN_INC     : $(PLUGIN_INC)"
	@echo "FAULT_INC      : $(FAULT_INC)"
	@echo "FAULT_CONFIG_H : $(FAULT_CONFIG_H)"
	@echo "GLIB_CFLAGS    : $(GLIB_CFLAGS)"
	@echo "CC             : $(CC)"
	@echo "CFLAGS         : $(CFLAGS)"