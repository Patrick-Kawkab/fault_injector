#include <stdint.h>

// ─────────────────────────────────────────────
//  External references
// ─────────────────────────────────────────────
extern int      main(void);
extern uint32_t _estack;

// ── Data/BSS symbols from linker script ───────
extern uint32_t _sdata, _edata, _la_data;
extern uint32_t _sbss,  _ebss;

// ─────────────────────────────────────────────
//  Handler declarations
// ─────────────────────────────────────────────
void Reset_Handler(void);
void Default_Handler(void) __attribute__((weak));
void SysTick_Handler(void) __attribute__((weak));
void GPIOD_Handler(void)   __attribute__((weak));

// ─────────────────────────────────────────────
//  Vector Table
//  TM4C123 vector table — see datasheet Table 2-9
// ─────────────────────────────────────────────
__attribute__((section(".isr_vector")))
void (*const vectors[])(void) = {
    (void*)&_estack,        //  0: Stack pointer
    Reset_Handler,          //  1: Reset
    Default_Handler,        //  2: NMI
    Default_Handler,        //  3: HardFault
    Default_Handler,        //  4: MemManage
    Default_Handler,        //  5: BusFault
    Default_Handler,        //  6: UsageFault
    0,                      //  7: Reserved
    0,                      //  8: Reserved
    0,                      //  9: Reserved
    0,                      // 10: Reserved
    Default_Handler,        // 11: SVCall
    Default_Handler,        // 12: DebugMon
    0,                      // 13: Reserved
    Default_Handler,        // 14: PendSV
    SysTick_Handler,        // 15: SysTick
    // ── External Interrupts (IRQ0 onwards) ────
    Default_Handler,        // 16: GPIO Port A
    Default_Handler,        // 17: GPIO Port B
    Default_Handler,        // 18: GPIO Port C
    GPIOD_Handler,          // 19: GPIO Port D  ← encoder
    Default_Handler,        // 20: GPIO Port E
    Default_Handler,        // 21: UART0
    Default_Handler,        // 22: UART1
    Default_Handler,        // 23: SSI0
    Default_Handler,        // 24: I2C0
    Default_Handler,        // 25: PWM0 Fault
    Default_Handler,        // 26: PWM0 Gen 0
    Default_Handler,        // 27: PWM0 Gen 1
    Default_Handler,        // 28: PWM0 Gen 2
    Default_Handler,        // 29: QEI0
    Default_Handler,        // 30: ADC0 SS0
    Default_Handler,        // 31: ADC0 SS1
    Default_Handler,        // 32: ADC0 SS2
    Default_Handler,        // 33: ADC0 SS3
    Default_Handler,        // 34: Watchdog
    Default_Handler,        // 35: Timer 0A
    Default_Handler,        // 36: Timer 0B
    Default_Handler,        // 37: Timer 1A
    Default_Handler,        // 38: Timer 1B
    Default_Handler,        // 39: Timer 2A
    Default_Handler,        // 40: Timer 2B
    Default_Handler,        // 41: Analog Comp 0
    Default_Handler,        // 42: Analog Comp 1
    0,                      // 43: Reserved
    Default_Handler,        // 44: System Control
    Default_Handler,        // 45: Flash/EEPROM
    Default_Handler,        // 46: GPIO Port F
};

// ─────────────────────────────────────────────
//  Reset Handler
//  Copies .data to RAM, zeroes .bss, calls main
// ─────────────────────────────────────────────
void Reset_Handler(void) {
    // Copy .data section from FLASH to RAM
    uint32_t *src = &_la_data;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Zero .bss section
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Call main
    main();

    // Should never reach here
    while (1);
}

// ─────────────────────────────────────────────
//  Default Handler — catches unhandled interrupts
// ─────────────────────────────────────────────
void Default_Handler(void) {
    while (1);
}