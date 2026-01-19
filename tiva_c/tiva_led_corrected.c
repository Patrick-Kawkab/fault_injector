#include <stdint.h>
#include "tm4c123gh6pm.h"

/* ================= Global Fault-Sensitive Variables ================= */
volatile uint32_t heartbeat = 0;
volatile uint32_t control_var = 1000;
volatile uint8_t  system_state = 0;

/* ================= Function Prototypes ================= */
void GPIO_Init(void);
void SysTick_Init(void);

/* ================= SysTick Interrupt Handler ================= */
void SysTick_Handler(void)
{
    heartbeat++;

    if ((heartbeat % 500) == 0) {
        control_var += 50;
    }

    if (control_var > 2000) {
        system_state = 1;
    }
    if (control_var > 3000) {
        system_state = 2;
    }
    if (control_var >= 4000) {
        system_state = 0;
        control_var =1000;
        
    }
}

/* ================= Main ================= */
int main(void)
{
    GPIO_Init();
    SysTick_Init();

    while (1)
    {
        switch (system_state)
        {
        case 0:                     // Normal
            GPIO_PORTF_DATA_R = 0x08; // Green LED
            break;

        case 1:                     
            GPIO_PORTF_DATA_R = 0x04; // Blue LED
            break;

        case 2:                     
            GPIO_PORTF_DATA_R = 0x02; // Red LED
            break;

        default:                    // Corrupted state
            GPIO_PORTF_DATA_R ^= 0x0E; // Toggle all LEDs
            break;
        }
    }
    
    // Never reached
    return 0;
}

/* ================= GPIO Init ================= */
void GPIO_Init(void)
{
    SYSCTL_RCGCGPIO_R |= 0x20;                 // Enable Port F clock
    while ((SYSCTL_PRGPIO_R & 0x20) == 0);     // Wait until ready

    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R   = 0x0E;
    GPIO_PORTF_DIR_R  = 0x0E;                  // PF1–PF3 output
    GPIO_PORTF_DEN_R  = 0x0E;
}

/* ================= SysTick Init ================= */
void SysTick_Init(void)
{
    NVIC_ST_CTRL_R = 0;               // Disable SysTick
    NVIC_ST_RELOAD_R = 8000- 1;    // 1 ms @ 16 MHz
    NVIC_ST_CURRENT_R = 0;
    NVIC_ST_CTRL_R = 0x07;            // Enable SysTick with interrupt
}
