#include <stdint.h>
#include "tm4c123gh6pm.h"

// ─────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────
#define PWM_PERIOD          1000
#define THROTTLE_ON         800
#define THROTTLE_OFF        0
#define DEADBAND            5

#define SPEED_MIN           0
#define SPEED_MAX           300
#define SPEED_STEP          10

#define ENCODER_PPR         20
#define SAMPLE_PERIOD_MS    100

#define MAX_ZERO_RPM_COUNT  10      // 10 * 100ms = 1 second with no pulses

// ─────────────────────────────────────────────
//  State
// ─────────────────────────────────────────────
typedef enum { STATE_OFF, STATE_ACTIVE } CruiseState;

volatile CruiseState    cruise_state    = STATE_OFF;
volatile uint32_t       target_rpm      = 100;
volatile uint32_t       encoder_count   = 0;
volatile uint8_t        tick_flag       = 0;
volatile uint32_t       tick_count      = 0;
volatile uint32_t       current_rpm     = 0;
volatile uint32_t       zero_rpm_count  = 0;    // ← new

// ─────────────────────────────────────────────
//  Delay
// ─────────────────────────────────────────────
void delay_ms(uint32_t ms) {
    uint32_t count = ms * (16000000 / 3000);
    while (count--);
}

// ─────────────────────────────────────────────
//  SysTick — fires every 10ms
// ─────────────────────────────────────────────
void SysTick_Init(void) {
    NVIC_ST_RELOAD_R  = (16000000 / 100) - 1;
    NVIC_ST_CURRENT_R = 0;
    NVIC_ST_CTRL_R    = 0x07;
}

void SysTick_Handler(void) {
    tick_flag = 1;
    tick_count++;

    // Every 100ms: calculate RPM
    if (tick_count >= (SAMPLE_PERIOD_MS / 10)) {
        tick_count = 0;

        current_rpm   = (encoder_count * 60000) / (ENCODER_PPR * SAMPLE_PERIOD_MS);
        encoder_count = 0;

        // ── Fault detection ──────────────────
        if (current_rpm == 0 && cruise_state == STATE_ACTIVE) {
            zero_rpm_count++;

            if (zero_rpm_count >= MAX_ZERO_RPM_COUNT) {
                // Encoder silent for 1 second → kill cruise
                cruise_state   = STATE_OFF;
                PWM0_0_CMPA_R  = PWM_PERIOD - 1;   // throttle off directly
                                                    // (cant call PWM_SetDuty
                                                    //  from an ISR safely)
                zero_rpm_count = 0;
            }
        } else {
            zero_rpm_count = 0;     // reset if we get a valid reading
        }
    }
}

// ─────────────────────────────────────────────
//  GPIO
// ─────────────────────────────────────────────
void GPIO_Init(void) {
    // ── Port F (LEDs + SW1 + SW2) ──────────────
    SYSCTL_RCGCGPIO_R |= (1 << 5);
    volatile uint32_t dummy; dummy = SYSCTL_RCGCGPIO_R; (void)dummy;

    GPIO_PORTF_LOCK_R  = 0x4C4F434B;
    GPIO_PORTF_CR_R   |= 0x1F;

    GPIO_PORTF_DIR_R  |=  (1<<1)|(1<<2)|(1<<3);
    GPIO_PORTF_DIR_R  &= ~((1<<0)|(1<<4));
    GPIO_PORTF_DEN_R  |=  (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4);
    GPIO_PORTF_PUR_R  |=  (1<<0)|(1<<4);
    GPIO_PORTF_DATA_R &= ~((1<<1)|(1<<2)|(1<<3));

    // ── Port E (PE0=BTN_DN, PE1=BTN_UP, PE3=Pot) ──
    SYSCTL_RCGCGPIO_R |= (1 << 4);
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;

    GPIO_PORTE_DIR_R  &= ~((1<<0)|(1<<1));
    GPIO_PORTE_DEN_R  |=  (1<<0)|(1<<1);
    GPIO_PORTE_PUR_R  |=  (1<<0)|(1<<1);

    // ── Port D (PD0 = Encoder A only) ─────────
    SYSCTL_RCGCGPIO_R |= (1 << 3);
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;

    GPIO_PORTD_DIR_R  &= ~(1<<0);
    GPIO_PORTD_DEN_R  |=  (1<<0);
    GPIO_PORTD_PUR_R  |=  (1<<0);

    GPIO_PORTD_IS_R   &= ~(1<<0);
    GPIO_PORTD_IBE_R  &= ~(1<<0);
    GPIO_PORTD_IEV_R  |=  (1<<0);
    GPIO_PORTD_ICR_R  |=  (1<<0);
    GPIO_PORTD_IM_R   |=  (1<<0);

    NVIC_EN0_R |= (1 << 3);
}

// ─────────────────────────────────────────────
//  Encoder ISR
// ─────────────────────────────────────────────
void GPIOD_Handler(void) {
    if (GPIO_PORTD_MIS_R & (1 << 0)) {
        encoder_count++;
        GPIO_PORTD_ICR_R |= (1 << 0);
    }
}

// ─────────────────────────────────────────────
//  ADC — Potentiometer on PE3 (gas pedal)
// ─────────────────────────────────────────────
void ADC_Init(void) {
    SYSCTL_RCGCADC_R  |= (1 << 0);
    SYSCTL_RCGCGPIO_R |= (1 << 4);
    volatile uint32_t dummy; dummy = SYSCTL_RCGCADC_R; (void)dummy;

    GPIO_PORTE_AFSEL_R |= (1 << 3);
    GPIO_PORTE_DEN_R   &= ~(1 << 3);
    GPIO_PORTE_AMSEL_R |= (1 << 3);

    ADC0_ACTSS_R  &= ~(1 << 3);
    ADC0_EMUX_R   &= ~0xF000;
    ADC0_SSMUX3_R  = 0;
    ADC0_SSCTL3_R  = 0x06;
    ADC0_ACTSS_R  |=  (1 << 3);
}

uint32_t ADC_ReadThrottle(void) {
    ADC0_PSSI_R = (1 << 3);
    while (!(ADC0_RIS_R & (1 << 3)));
    uint32_t val = ADC0_SSFIFO3_R & 0xFFF;
    ADC0_ISC_R   = (1 << 3);
    return (val * 100) / 4095;
}

// ─────────────────────────────────────────────
//  PWM — Motor on PB6
// ─────────────────────────────────────────────
void PWM_Init(void) {
    SYSCTL_RCGCPWM_R  |= (1 << 0);
    SYSCTL_RCGCGPIO_R |= (1 << 1);
    volatile uint32_t dummy; dummy = SYSCTL_RCGCPWM_R; (void)dummy;

    SYSCTL_RCC_R &= ~(1 << 20);

    GPIO_PORTB_AFSEL_R |= (1 << 6);
    GPIO_PORTB_PCTL_R  |= (4 << 24);
    GPIO_PORTB_DEN_R   |= (1 << 6);

    PWM0_0_CTL_R   = 0;
    PWM0_0_GENA_R  = 0x8C;
    PWM0_0_LOAD_R  = PWM_PERIOD - 1;
    PWM0_0_CMPA_R  = 0;
    PWM0_0_CTL_R   = 1;
    PWM0_ENABLE_R |= (1 << 0);
}

void PWM_SetDuty(uint32_t duty) {
    if (duty > 100) duty = 100;
    uint32_t ticks = (duty * PWM_PERIOD) / 100;
    if (ticks >= PWM_PERIOD) ticks = PWM_PERIOD - 1;
    PWM0_0_CMPA_R = PWM_PERIOD - 1 - ticks;
}

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────
uint8_t button_pressed(uint32_t port_data, uint8_t pin_mask) {
    if (!(port_data & pin_mask)) {
        delay_ms(20);
        if (!(port_data & pin_mask)) return 1;
    }
    return 0;
}

void led_set(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t val = GPIO_PORTF_DATA_R & ~((1<<1)|(1<<2)|(1<<3));
    if (red)   val |= (1<<1);
    if (blue)  val |= (1<<2);
    if (green) val |= (1<<3);
    GPIO_PORTF_DATA_R = val;
}

// ─────────────────────────────────────────────
//  Bang-Bang Control
// ─────────────────────────────────────────────
void bangbang_control(void) {
    if (current_rpm < (target_rpm - DEADBAND)) {
        PWM_SetDuty(THROTTLE_ON);
        led_set(0, 0, 1);           // blue = accelerating
    } else if (current_rpm > (target_rpm + DEADBAND)) {
        PWM_SetDuty(THROTTLE_OFF);
        led_set(0, 1, 0);           // green = coasting
    }
    // inside deadband → hold current state
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────
int main(void) {
    GPIO_Init();
    ADC_Init();
    PWM_Init();
    SysTick_Init();

    led_set(1, 0, 0);   // red = standby

    while (1) {

        // ── BTN_UP (PE1) — increase cruise target ───
        if (button_pressed(GPIO_PORTE_DATA_R, (1 << 1))) {
            if (target_rpm <= SPEED_MAX - SPEED_STEP)
                target_rpm += SPEED_STEP;
            else
                target_rpm = SPEED_MAX;
            while (!(GPIO_PORTE_DATA_R & (1 << 1)));
        }

        // ── BTN_DN (PE0) — decrease cruise target ───
        if (button_pressed(GPIO_PORTE_DATA_R, (1 << 0))) {
            if (target_rpm >= SPEED_MIN + SPEED_STEP)
                target_rpm -= SPEED_STEP;
            else
                target_rpm = SPEED_MIN;
            while (!(GPIO_PORTE_DATA_R & (1 << 0)));
        }

        // ── SW1 (PF4) — activate cruise ─────────────
        if (button_pressed(GPIO_PORTF_DATA_R, (1 << 4))) {
            cruise_state   = STATE_ACTIVE;
            zero_rpm_count = 0;             // ← reset fault counter on activation
            led_set(0, 1, 0);
            while (!(GPIO_PORTF_DATA_R & (1 << 4)));
        }

        // ── SW2 (PF0) — cancel cruise ───────────────
        if (button_pressed(GPIO_PORTF_DATA_R, (1 << 0))) {
            cruise_state = STATE_OFF;
            PWM_SetDuty(THROTTLE_OFF);
            led_set(1, 0, 0);
            while (!(GPIO_PORTF_DATA_R & (1 << 0)));
        }

        // ── Every 10ms tick ─────────────────────────
        if (tick_flag) {
            tick_flag = 0;

            if (cruise_state == STATE_ACTIVE) {
                bangbang_control();
            } else {
                // Manual mode: pot drives motor directly
                uint32_t throttle = ADC_ReadThrottle();
                PWM_SetDuty(throttle);
            }

            // ── Reflect fault shutdown in LED ────────
            // SysTick may have set cruise to OFF due to fault
            // check here so LED updates on next tick
            if (cruise_state == STATE_OFF) {
                // only update LED if it was previously active
                // red is already set by SysTick indirectly
                // via next manual mode cycle
            }
        }
    }
}