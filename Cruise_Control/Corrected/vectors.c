#include <stdint.h>

// External references
extern int main(void);
extern void SysTick_Handler(void);
extern uint32_t _estack;  // Add this declaration

// Reset handler - يجب أن تكون أول دالة بعد الـ vector table
__attribute__((noreturn)) 
void Reset_Handler(void);

void Reset_Handler(void) {
    // Initialize .data section
    extern uint32_t _sdata, _edata, _sidata;
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
    
    // Zero .bss section
    extern uint32_t _sbss, _ebss;
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }
    
    // Call main
    main();
    
    // Should never reach here
    while(1);
}

// Weak default handlers
void Default_Handler(void) {
    while(1);
}

// Define weak aliases for all handlers
#define WEAK_ALIAS __attribute__((weak, alias("Default_Handler")))

void NMI_Handler(void) WEAK_ALIAS;
void HardFault_Handler(void) WEAK_ALIAS;
void MemManage_Handler(void) WEAK_ALIAS;
void BusFault_Handler(void) WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void) WEAK_ALIAS;
void DebugMon_Handler(void) WEAK_ALIAS;
void PendSV_Handler(void) WEAK_ALIAS;

// Vector table - بدون casting للمؤشرات
__attribute__((section(".vectors"), used))
void (* const vector_table[])(void) = {
    (void (*)(void))((uint32_t)&_estack),  // Stack pointer
    Reset_Handler,                         // Reset handler
    NMI_Handler,                           // NMI
    HardFault_Handler,                     // HardFault
    MemManage_Handler,                     // MemManage
    BusFault_Handler,                      // BusFault
    UsageFault_Handler,                    // UsageFault
    0, 0, 0, 0,                            // Reserved
    SVC_Handler,                           // SVCall
    DebugMon_Handler,                      // Debug
    0,                                     // Reserved
    PendSV_Handler,                        // PendSV
    SysTick_Handler                        // SysTick
};
