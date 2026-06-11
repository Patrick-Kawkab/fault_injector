/* =============================================================================
   startup.c  —  minimal Cortex-M3 startup for lm3s6965evb
   
   - Vector table (stack pointer + reset handler only; others default to hang)
   - Reset handler: copies .data from flash to RAM, zeroes .bss, calls main()
   ============================================================================= */

#include <stdint.h>

/* Symbols from linker script */
extern uint32_t _stack_top;
extern uint32_t _sdata, _edata, _sidata;
extern uint32_t _sbss,  _ebss;

/* Forward declaration */
int main(void);

/* ── Default handler — infinite loop so we can see a hang in the plugin ── */
static void default_handler(void) {
    while (1);
}


/* ── Reset handler ── */
void reset_handler(void) {
    /* Copy initialised data from flash to RAM */
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Zero-initialise BSS */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    /* Call application */
    main();

    /* Should never return — hang if it does */
    while (1);
}

/* ── Vector table ── */
__attribute__((section(".vectors")))
void (* const vector_table[])(void) = {
    (void (*)(void))&_stack_top,   /* 0: initial stack pointer */
    reset_handler,                  /* 1: reset                 */
    default_handler,                /* 2: NMI                   */
    default_handler,                /* 3: HardFault             */
    default_handler,                /* 4: MemManage             */
    default_handler,                /* 5: BusFault              */
    default_handler,                /* 6: UsageFault            */
    0, 0, 0, 0,                    /* 7-10: reserved           */
    default_handler,                /* 11: SVCall               */
    default_handler,                /* 12: DebugMon             */
    0,                              /* 13: reserved             */
    default_handler,                /* 14: PendSV               */
    default_handler,                /* 15: SysTick              */
};
