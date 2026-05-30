# =============================================================================
#  Makefile — builds fault_plugin.so (QEMU TCG plugin)
#
#  Prerequisites
#  ─────────────
#  • QEMU source tree with include/qemu/qemu-plugin.h
#    Set QEMU_SRC to your QEMU source root, e.g.:
#       make QEMU_SRC=/opt/qemu-8.2.0
#
#  • gcc with position-independent code support (standard on Linux/macOS)
#
#  Typical invocation
#  ──────────────────
#    make                          # uses QEMU_SRC from environment or default
#    make QEMU_SRC=/path/to/qemu   # explicit path
#    make clean
#
#  Output
#  ──────
#    fault_plugin.so   — loaded by QEMU via  -plugin ./fault_plugin.so,server=...
# =============================================================================



# ---------- Paths -------------------------------------------------------------
PLUGIN_INC := /home/zyad/qemu/include/plugins
GLIB_INC   := /usr/include/glib-2.0
GLIB_CFG   := /usr/lib/x86_64-linux-gnu/glib-2.0/include

# ---------- Toolchain ---------------------------------------------------------
CC      := gcc
CFLAGS  := -O2 -Wall -Wextra \
            -fPIC             \
            -I$(PLUGIN_INC)  \
            -I$(GLIB_INC)    \
            -I$(GLIB_CFG)    \
            -D_GNU_SOURCE

# QEMU plugin ABI requires shared library, no undefined symbols resolved at
# link time (QEMU resolves them at dlopen).
LDFLAGS := -shared -fPIC

# ---------- Sources -----------------------------------------------------------
TARGET  := fault_plugin.so
SRC     := fault_plugin.c

# ---------- Rules -------------------------------------------------------------
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@echo ""
	@echo "  Built: $(TARGET)"
	@echo "  Load with:  -plugin ./$(TARGET),server=localhost:9001"
	@echo ""

clean:
	rm -f $(TARGET)