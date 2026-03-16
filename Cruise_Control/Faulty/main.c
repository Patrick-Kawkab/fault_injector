#include <stdint.h>
#include "tm4c123gh6pm.h"

// ─────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────
#define PWM_PERIOD 1000 // PWM period ticks
#define THROTTLE_ON 800 // 80% duty during cruise
#define THROTTLE_OFF 0
#define DEADBAND 5 // RPM deadband to avoid hunting

#define SPEED_MIN 0   // minimum cruise target (RPM)
#define SPEED_MAX 300 // maximum cruise target (RPM)
#define SPEED_STEP 10 // RPM per button press

#define ENCODER_PPR 20       // pulses per revolution of your encoder
#define SAMPLE_PERIOD_MS 100 // measure speed every 100ms

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
typedef enum
{
    STATE_OFF,
    STATE_ACTIVE
} CruiseState;

volatile CruiseState cruise_state = STATE_OFF;
volatile uint32_t target_rpm = 100;  // default cruise target
volatile uint32_t encoder_count = 0; // pulses counted by interrupt
volatile uint8_t tick_flag = 0;      // 10ms SysTick flag
volatile uint32_t tick_count = 0;    // counts 10ms ticks
volatile uint32_t current_rpm = 0;   // calculated from encoder

// ─────────────────────────────────────────────
//  Delay
// ─────────────────────────────────────────────
void delay_ms(uint32_t ms)
{
    uint32_t count = ms * (16000000 / 3000);
    while (count--)
        ;
}

// ─────────────────────────────────────────────
//  SysTick — fires every 10ms
// ─────────────────────────────────────────────
void SysTick_Init(void)
{
    NVIC_ST_RELOAD_R = (16000000 / 100) - 1; // 10ms at 16MHz
    NVIC_ST_CURRENT_R = 0;
    NVIC_ST_CTRL_R = 0x07;
}

void SysTick_Handler(void)
{
    tick_flag = 1;
    tick_count++;

    // Every 100ms: calculate RPM from encoder pulses
    if (tick_count >= (SAMPLE_PERIOD_MS / 10))
    {
        tick_count = 0;

        // RPM = (pulses / PPR) * (60000 / sample_period_ms)
        current_rpm = (encoder_count * 60000) / (ENCODER_PPR * SAMPLE_PERIOD_MS);
        encoder_count = 0; // reset counter for next window
    }
}

// ─────────────────────────────────────────────
//  GPIO — Port F (LEDs + SW1 + SW2)
//          Port E (BTN_UP, BTN_DN, Pot)
//          Port D (Encoder A = PD0, B = PD1)
// ─────────────────────────────────────────────
void GPIO_Init(void)
{
    // ── Port F ──────────────────────────────────
    SYSCTL_RCGCGPIO_R |= (1 << 5);
    volatile uint32_t dummy; dummy = SYSCTL_RCGCGPIO_R; (void)dummy;

    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R |= 0x1F;

    GPIO_PORTF_DIR_R |= (1 << 1) | (1 << 2) | (1 << 3); // LEDs = output
    GPIO_PORTF_DIR_R &= ~((1 << 0) | (1 << 4));         // SW1, SW2 = input
    GPIO_PORTF_DEN_R |= (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);
    GPIO_PORTF_PUR_R |= (1 << 0) | (1 << 4);
    GPIO_PORTF_DATA_R &= ~((1 << 1) | (1 << 2) | (1 << 3));

    // ── Port E (PE0=BTN_DN, PE1=BTN_UP, PE3=Pot) ──
    SYSCTL_RCGCGPIO_R |= (1 << 4);
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;

    GPIO_PORTE_DIR_R &= ~((1 << 0) | (1 << 1)); // PE0, PE1 = input
    GPIO_PORTE_DEN_R |= (1 << 0) | (1 << 1);
    GPIO_PORTE_PUR_R |= (1 << 0) | (1 << 1);
    // PE3 configured as analog in ADC_Init()

    // ── Port D (PD0 = Encoder A only) ─────────────
    SYSCTL_RCGCGPIO_R |= (1 << 3);
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;

    GPIO_PORTD_DIR_R &= ~(1 << 0); // PD0 input only
    GPIO_PORTD_DEN_R |= (1 << 0);
    GPIO_PORTD_PUR_R |= (1 << 0);

    // Encoder interrupt on PD0 (rising edge)
    GPIO_PORTD_IS_R &= ~(1 << 0);
    GPIO_PORTD_IBE_R &= ~(1 << 0);
    GPIO_PORTD_IEV_R |= (1 << 0);
    GPIO_PORTD_ICR_R |= (1 << 0);
    GPIO_PORTD_IM_R |= (1 << 0);

    NVIC_EN0_R |= (1 << 3);
    }
    // ─────────────────────────────────────────────
    //  Encoder ISR — Port D interrupt
    //  Fires on every rising edge of Encoder A
    // ─────────────────────────────────────────────
    void GPIOD_Handler(void)
    {
        if (GPIO_PORTD_MIS_R & (1 << 0))
        {
            encoder_count++;
            GPIO_PORTD_ICR_R |= (1 << 0); // clear interrupt
        }
    }

    // ─────────────────────────────────────────────
    //  ADC — Potentiometer on PE3 (gas pedal)
    // ─────────────────────────────────────────────
    void ADC_Init(void)
    {
        SYSCTL_RCGCADC_R |= (1 << 0);
        SYSCTL_RCGCGPIO_R |= (1 << 4);
        volatile uint32_t dummy; dummy = SYSCTL_RCGCADC_R; (void)dummy;

        GPIO_PORTE_AFSEL_R |= (1 << 3);
        GPIO_PORTE_DEN_R &= ~(1 << 3);
        GPIO_PORTE_AMSEL_R |= (1 << 3);

        ADC0_ACTSS_R &= ~(1 << 3);
        ADC0_EMUX_R &= ~0xF000;
        ADC0_SSMUX3_R = 0; // AIN0 = PE3
        ADC0_SSCTL3_R = 0x06;
        ADC0_ACTSS_R |= (1 << 3);
    }

    // Returns 0–100 (maps 0–4095 ADC to 0–100% throttle)
    uint32_t ADC_ReadThrottle(void)
    {
        ADC0_PSSI_R = (1 << 3);
        while (!(ADC0_RIS_R & (1 << 3)))
            ;
        uint32_t val = ADC0_SSFIFO3_R & 0xFFF;
        ADC0_ISC_R = (1 << 3);
        return (val * 100) / 4095;
    }

    // ─────────────────────────────────────────────
    //  PWM — Motor throttle on PB6 (M0PWM0)
    // ─────────────────────────────────────────────
    void PWM_Init(void)
    {
        SYSCTL_RCGCPWM_R |= (1 << 0);
        SYSCTL_RCGCGPIO_R |= (1 << 1);
        volatile uint32_t dummy; dummy = SYSCTL_RCGCPWM_R; (void) dummy;

        SYSCTL_RCC_R &= ~(1 << 20);

        GPIO_PORTB_AFSEL_R |= (1 << 6);
        GPIO_PORTB_PCTL_R |= (4 << 24);
        GPIO_PORTB_DEN_R |= (1 << 6);

        PWM0_0_CTL_R = 0;
        PWM0_0_GENA_R = 0x8C;
        PWM0_0_LOAD_R = PWM_PERIOD - 1;
        PWM0_0_CMPA_R = 0;
        PWM0_0_CTL_R = 1;
        PWM0_ENABLE_R |= (1 << 0);
    }

    // duty: 0–100 (percentage)
    void PWM_SetDuty(uint32_t duty)
    {
        if (duty > 100)
            duty = 100;
        uint32_t ticks = (duty * PWM_PERIOD) / 100;
        if (ticks >= PWM_PERIOD)
            ticks = PWM_PERIOD - 1;
        PWM0_0_CMPA_R = PWM_PERIOD - 1 - ticks;
    }

    // ─────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────
    uint8_t button_pressed(uint32_t port_data, uint8_t pin_mask)
    {
        if (!(port_data & pin_mask))
        {
            delay_ms(20);
            if (!(port_data & pin_mask))
                return 1;
        }
        return 0;
    }

    void led_set(uint8_t red, uint8_t green, uint8_t blue)
    {
        uint32_t val = GPIO_PORTF_DATA_R & ~((1 << 1) | (1 << 2) | (1 << 3));
        if (red)
            val |= (1 << 1);
        if (blue)
            val |= (1 << 2);
        if (green)
            val |= (1 << 3);
        GPIO_PORTF_DATA_R = val;
    }

    // ─────────────────────────────────────────────
    //  Bang-Bang Control (uses RPM not ADC)
    // ─────────────────────────────────────────────
    void bangbang_control(void)
    {
        if (current_rpm < (target_rpm - DEADBAND))
        {
            PWM_SetDuty(THROTTLE_ON); // too slow → throttle on
            led_set(0, 0, 1);         // blue = accelerating
        }
        else if (current_rpm > (target_rpm + DEADBAND))
        {
            PWM_SetDuty(THROTTLE_OFF); // too fast → coast
            led_set(0, 1, 0);          // green = coasting
        }
        // inside deadband → hold current state
    }

    // ─────────────────────────────────────────────
    //  Main
    // ─────────────────────────────────────────────
    int main(void)
    {
        GPIO_Init();
        ADC_Init();
        PWM_Init();
        SysTick_Init();

        led_set(1, 0, 0); // red = standby

        while (1)
        {

            // ── BTN_UP (PE1) — increase cruise target ───
            if (button_pressed(GPIO_PORTE_DATA_R, (1 << 1)))
            {
                if (target_rpm <= SPEED_MAX - SPEED_STEP)
                    target_rpm += SPEED_STEP;
                else
                    target_rpm = SPEED_MAX;
                while (!(GPIO_PORTE_DATA_R & (1 << 1)))
                    ;
            }

            // ── BTN_DN (PE0) — decrease cruise target ───
            if (button_pressed(GPIO_PORTE_DATA_R, (1 << 0)))
            {
                if (target_rpm >= SPEED_MIN + SPEED_STEP)
                    target_rpm -= SPEED_STEP;
                else
                    target_rpm = SPEED_MIN;
                while (!(GPIO_PORTE_DATA_R & (1 << 0)))
                    ;
            }

            // ── SW1 (PF4) — activate cruise ─────────────
            if (button_pressed(GPIO_PORTF_DATA_R, (1 << 4)))
            {
                cruise_state = STATE_ACTIVE;
                led_set(0, 1, 0); // green = cruise on
                while (!(GPIO_PORTF_DATA_R & (1 << 4)))
                    ;
            }

            // ── SW2 (PF0) — cancel cruise ───────────────
            if (button_pressed(GPIO_PORTF_DATA_R, (1 << 0)))
            {
                cruise_state = STATE_OFF;
                PWM_SetDuty(THROTTLE_OFF);
                led_set(1, 0, 0); // red = off
                while (!(GPIO_PORTF_DATA_R & (1 << 0)))
                    ;
            }

            // ── Every 10ms tick ─────────────────────────
            if (tick_flag)
            {
                tick_flag = 0;

                if (cruise_state == STATE_ACTIVE)
                {
                    bangbang_control(); // uses encoder RPM
                }
                else
                {
                    // Manual mode: pot drives motor directly
                    uint32_t throttle = ADC_ReadThrottle();
                    PWM_SetDuty(throttle);
                }
            }
        }
    }