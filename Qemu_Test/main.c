/* =============================================================================
   main.c  —  Temperature Safety Monitor
              bare-metal test application for the fault injection framework

              
   Purpose
   ───────
   A simple safety monitor that:
     1. Reads a temperature sensor value from a fixed RAM address
     2. Validates it against LOW / HIGH thresholds
     3. Sets a system status flag (SAFE / WARNING / CRITICAL)
     4. Activates a cooling output if temperature is too high
     5. Reports every cycle over UART
     6. Loops forever (10 cycles then halts so QEMU exits cleanly)

   Memory layout  (all addresses logged at startup for use in Input.json)
   ─────────────────────────────────────────────────────────────────────────
     0x20000000  g_sensor_temp    uint8_t   — sensor reading (0-100 °C)
     0x20000001  g_status         uint8_t   — 0=SAFE 1=WARNING 2=CRITICAL
     0x20000002  g_cooling_active uint8_t   — 0=OFF  1=ON
     0x20000003  g_cycle_count    uint8_t   — loop iteration counter

   Fault type mapping
   ──────────────────
   memory_corruption  : overwrite g_sensor_temp  → wrong status decision
   bit_flip           : flip a bit in g_sensor_temp or g_status
   instruction_skip   : skip the threshold comparison → wrong status
   set_pc             : redirect execution → undefined behaviour / hang
   sensor_corruption  : intercept the memory read of g_sensor_temp

   Normal output (UART, one line per cycle)
   ─────────────────────────────────────────
   [MONITOR] cycle=1  temp=0x28 (40)  status=SAFE      cooling=OFF
   [MONITOR] cycle=2  temp=0x28 (40)  status=SAFE      cooling=OFF
   ...

   Faulted output example (memory_corruption injected_value=0x5F = 95°C)
   ───────────────────────────────────────────────────────────────────────
   [MONITOR] cycle=1  temp=0x28 (40)  status=SAFE      cooling=OFF
   [MONITOR] cycle=2  temp=0x5F (95)  status=CRITICAL  cooling=ON   <-- fault
   ============================================================================= */

#include <stdint.h>
#include "uart.h"

/* ── Safety thresholds ───────────────────────────────────────────────────── */
#define TEMP_LOW_THRESHOLD   30u   /* below this  → SAFE     */
#define TEMP_HIGH_THRESHOLD  75u   /* above this  → CRITICAL */
                                   /* between     → WARNING  */

/* ── Status codes ────────────────────────────────────────────────────────── */
#define STATUS_SAFE     0u
#define STATUS_WARNING  1u
#define STATUS_CRITICAL 2u

/* ── Sensor simulation: 10 readings (normal operating range 38-42°C) ─────── */
static const uint8_t sensor_readings[10] = {
    38, 39, 40, 41, 40, 39, 42, 40, 38, 41
};

/* ── RAM variables — addresses printed at startup for use in Input.json ─── */
/* NOTE: volatile prevents the compiler optimising away reads/writes that    */
/*       the fault plugin modifies directly in RAM.                          */
volatile uint8_t g_sensor_temp    = 0;   /* 0x20000000 */
volatile uint8_t g_status         = 0;   /* 0x20000001 */
volatile uint8_t g_cooling_active = 0;   /* 0x20000002 */
volatile uint8_t g_cycle_count    = 0;   /* 0x20000003 */

/* ── Simple busy-wait delay (gives the plugin time between injections) ───── */
static void delay(volatile uint32_t n) {
    while (n--);
}

/* ── Evaluate temperature and set status + cooling output ────────────────── */
/* NOTE: this function is the primary injection target for instruction_skip   */
static void __attribute__((noinline)) evaluate_safety(void)
{
    if (g_sensor_temp > TEMP_HIGH_THRESHOLD) {
        g_status         = STATUS_CRITICAL;
        g_cooling_active = 1u;
    } else if (g_sensor_temp >= TEMP_LOW_THRESHOLD) {
        g_status         = STATUS_WARNING;
        g_cooling_active = 0u;
    } else {
        g_status         = STATUS_SAFE;
        g_cooling_active = 0u;
    }
}

/* ── Print one monitoring line over UART ─────────────────────────────────── */
static void report_cycle(void)
{
    uart_puts("[MONITOR] cycle=");
    uart_udec(g_cycle_count);

    uart_puts("  temp=");
    uart_hex8(g_sensor_temp);
    uart_putc(' ');
    uart_putc('(');
    uart_udec(g_sensor_temp);
    uart_putc(')');

    uart_puts("  status=");
    switch (g_status) {
        case STATUS_SAFE:     uart_puts("SAFE    ");  break;
        case STATUS_WARNING:  uart_puts("WARNING ");  break;
        case STATUS_CRITICAL: uart_puts("CRITICAL"); break;
        default:              uart_puts("UNKNOWN ");  break;
    }

    uart_puts("  cooling=");
    uart_puts(g_cooling_active ? "ON " : "OFF");

    uart_nl();
}

/* ── Print memory map at startup so addresses can be copied to Input.json ── */
static void print_memory_map(void)
{
    uart_puts("========================================");
    uart_nl();
    uart_puts("[MONITOR] Temperature Safety Monitor");
    uart_nl();
    uart_puts("[MONITOR] Memory map:");
    uart_nl();

    uart_puts("[MONITOR]   g_sensor_temp    @ ");
    uart_hex32((uint32_t)(uintptr_t)&g_sensor_temp);
    uart_nl();

    uart_puts("[MONITOR]   g_status         @ ");
    uart_hex32((uint32_t)(uintptr_t)&g_status);
    uart_nl();

    uart_puts("[MONITOR]   g_cooling_active @ ");
    uart_hex32((uint32_t)(uintptr_t)&g_cooling_active);
    uart_nl();

    uart_puts("[MONITOR]   g_cycle_count    @ ");
    uart_hex32((uint32_t)(uintptr_t)&g_cycle_count);
    uart_nl();

    uart_puts("[MONITOR]   evaluate_safety  @ ");
    uart_hex32((uint32_t)(uintptr_t)evaluate_safety);
    uart_nl();

    uart_puts("[MONITOR] Thresholds: LOW=");
    uart_udec(TEMP_LOW_THRESHOLD);
    uart_puts("  HIGH=");
    uart_udec(TEMP_HIGH_THRESHOLD);
    uart_nl();
    uart_puts("========================================");
    uart_nl();
}

/* ── Main loop ───────────────────────────────────────────────────────────── */
int main(void)
{
    print_memory_map();

    uart_puts("[MONITOR] Starting monitoring loop (10 cycles)");
    uart_nl();

    for (uint8_t i = 0; i < 10; i++) {
        g_cycle_count = i + 1u;

        /* Read sensor — this access is the trigger for sensor_corruption */
        g_sensor_temp = sensor_readings[i];

        /* Evaluate — PC of first instruction is target for instruction_skip */
        evaluate_safety();

        /* Report results over UART */
        report_cycle();

        delay(10000);
    }

    uart_puts("[MONITOR] Done.");
    uart_nl();
    uart_puts("========================================");
    uart_nl();

    /* Exit QEMU cleanly via semihosting SYS_EXIT */
    /* Angel semihosting: T=0xAB, op=0x18 (SYS_EXIT), param=0x20026 (exit 0) */
    register uint32_t r0 __asm__("r0") = 0x18;
    register uint32_t r1 __asm__("r1") = 0x20026;
    __asm__ volatile ("bkpt 0xAB" :: "r"(r0), "r"(r1));

    while (1);   /* should not reach here */
    return 0;
}
