#ifndef UART_H
#define UART_H

/* =============================================================================
   uart.h  —  minimal UART0 driver for lm3s6965evb (Cortex-M3)

   QEMU emulates the PL011 UART at 0x4000C000.
   No baud-rate setup needed under QEMU — writes go straight through.
   ============================================================================= */

#include <stdint.h>

/* PL011 UART0 base address on lm3s6965evb */
#define UART0_BASE   0x4000C000UL

/* PL011 register offsets */
#define UART_DR      (*(volatile uint32_t *)(UART0_BASE + 0x000))  /* data        */
#define UART_FR      (*(volatile uint32_t *)(UART0_BASE + 0x018))  /* flag        */

#ifndef UART_FR_TXFF
#define UART_FR_TXFF (1u << 5)                                     /* TX FIFO full */
#endif

/* ── Send one character ── */
static inline void uart_putc(char c) {
    while (UART_FR & UART_FR_TXFF);
    UART_DR = (uint32_t)c;
}

/* ── Send a null-terminated string ── */
static inline void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

/* ── Send a newline ── */
static inline void uart_nl(void) {
    uart_putc('\r');
    uart_putc('\n');
}

/* ── Print an 8-bit hex value  e.g. "0x1F" ── */
static inline void uart_hex8(uint8_t v) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc('0');
    uart_putc('x');
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[ v       & 0xF]);
}

/* ── Print a 32-bit hex value  e.g. "0x20000010" ── */
static inline void uart_hex32(uint32_t v) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc('0');
    uart_putc('x');
    for (int i = 28; i >= 0; i -= 4)
        uart_putc(hex[(v >> i) & 0xF]);
}

/* ── Print an unsigned decimal ── */
static inline void uart_udec(uint32_t v) {
    if (v == 0) { uart_putc('0'); return; }
    char buf[10];
    int  i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) uart_putc(buf[i]);
}

#endif /* UART_H */
