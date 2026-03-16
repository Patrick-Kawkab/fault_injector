#include <stdint.h>
#include "tm4c123gh6pm.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// ─────────────────────────────────────────────
//  Forward Declarations
// ─────────────────────────────────────────────
void     GPIO_Init(void);
void     ADC_Init(void);
void     PWM_Init(void);
void     LCD_Init(void);
void     LCD_Pulse(void);
void     LCD_Nibble(uint8_t nibble);
void     LCD_Command(uint8_t cmd);
void     LCD_Char(char c);
void     LCD_String(const char *str);
void     LCD_SetCursor(uint8_t row, uint8_t col);
void     LCD_Clear(void);
void     uint32_to_str(uint32_t val, char *buf, uint8_t width);
void     PWM_SetDuty(uint32_t duty);
void     led_set(uint8_t red, uint8_t green, uint8_t blue);
uint8_t  button_pressed(uint32_t port_data, uint8_t pin_mask);
uint32_t ADC_ReadThrottle(void);
void     GPIOD_Handler(void);
void     vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
void     vEncoderTask(void *pvParameters);
void     vCruiseTask(void *pvParameters);
void     vManualTask(void *pvParameters);
void     vWatchdogTask(void *pvParameters);
void     vLCDTask(void *pvParameters);

// ─────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────
#define PWM_PERIOD              1000
#define THROTTLE_ON             80
#define THROTTLE_OFF            0
#define DEADBAND                5

#define SPEED_MIN               0
#define SPEED_MAX               300
#define SPEED_STEP              10

#define ENCODER_PPR             20
#define SAMPLE_PERIOD_MS        100

#define ZERO_RPM_LIMIT          30      // 30 * 100ms = 3 seconds
#define TASK_WATCHDOG_LIMIT     20      // 20 * 100ms = 2 seconds

// ── LCD Pin Definitions ───────────────────────
#define LCD_RS              (1 << 4)
#define LCD_E               (1 << 5)
#define LCD_D4              (1 << 6)
#define LCD_D5              (1 << 7)
#define LCD_D6              (1 << 4)
#define LCD_D7              (1 << 5)

// ─────────────────────────────────────────────
//  Shared State
// ─────────────────────────────────────────────
typedef enum { STATE_OFF, STATE_ACTIVE } CruiseState;

volatile CruiseState    cruise_state        = STATE_OFF;
volatile uint32_t       target_rpm          = 100;
volatile uint32_t       current_rpm         = 0;
volatile uint32_t       encoder_count       = 0;
volatile uint32_t       zero_rpm_count      = 0;

// ── Watchdog kick counters ────────────────────
volatile uint32_t       encoder_task_kick   = 0;
volatile uint32_t       cruise_task_kick    = 0;
volatile uint32_t       manual_task_kick    = 0;

SemaphoreHandle_t       xRPMMutex;
SemaphoreHandle_t       xStateMutex;

// ─────────────────────────────────────────────
//  Delay
// ─────────────────────────────────────────────
void delay_ms(uint32_t ms) {
    uint32_t count = ms * (16000000 / 3000);
    while (count--);
}

// ─────────────────────────────────────────────
//  GPIO Init
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

    // ── Port D (PD0 = Encoder A) ───────────────
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
    NVIC_EN0_R        |=  (1 << 3);

    // ── Port C (RS=PC4, E=PC5, D4=PC6, D5=PC7) ──
    SYSCTL_RCGCGPIO_R |= (1 << 2);
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;
    GPIO_PORTC_DIR_R  |=  (1<<4)|(1<<5)|(1<<6)|(1<<7);
    GPIO_PORTC_DEN_R  |=  (1<<4)|(1<<5)|(1<<6)|(1<<7);
    GPIO_PORTC_DATA_R &= ~((1<<4)|(1<<5)|(1<<6)|(1<<7));

    // ── Port B (PWM=PB6, D6=PB4, D7=PB5) ─────
    SYSCTL_RCGCGPIO_R |= (1 << 1);
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;
    GPIO_PORTB_DIR_R  |=  (1<<4)|(1<<5);
    GPIO_PORTB_DEN_R  |=  (1<<4)|(1<<5);
    GPIO_PORTB_DATA_R &= ~((1<<4)|(1<<5));
}

// ─────────────────────────────────────────────
//  ADC Init
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
//  PWM Init
// ─────────────────────────────────────────────
void PWM_Init(void) {
    SYSCTL_RCGCPWM_R  |= (1 << 0);
    SYSCTL_RCGCGPIO_R |= (1 << 1);
    volatile uint32_t dummy; dummy = SYSCTL_RCGCPWM_R; (void)dummy;

    SYSCTL_RCC_R       &= ~(1 << 20);
    GPIO_PORTB_AFSEL_R |=  (1 << 6);
    GPIO_PORTB_PCTL_R  |=  (4 << 24);
    GPIO_PORTB_DEN_R   |=  (1 << 6);

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
//  LCD Driver
// ─────────────────────────────────────────────
void LCD_Pulse(void) {
    GPIO_PORTC_DATA_R |=  LCD_E;
    delay_ms(1);
    GPIO_PORTC_DATA_R &= ~LCD_E;
    delay_ms(1);
}

void LCD_Nibble(uint8_t nibble) {
    if (nibble & 0x01) GPIO_PORTC_DATA_R |=  LCD_D4;
    else               GPIO_PORTC_DATA_R &= ~LCD_D4;
    if (nibble & 0x02) GPIO_PORTC_DATA_R |=  LCD_D5;
    else               GPIO_PORTC_DATA_R &= ~LCD_D5;
    if (nibble & 0x04) GPIO_PORTB_DATA_R |=  LCD_D6;
    else               GPIO_PORTB_DATA_R &= ~LCD_D6;
    if (nibble & 0x08) GPIO_PORTB_DATA_R |=  LCD_D7;
    else               GPIO_PORTB_DATA_R &= ~LCD_D7;
    LCD_Pulse();
}

void LCD_Command(uint8_t cmd) {
    GPIO_PORTC_DATA_R &= ~LCD_RS;
    LCD_Nibble(cmd >> 4);
    LCD_Nibble(cmd & 0x0F);
    delay_ms(2);
}

void LCD_Char(char c) {
    GPIO_PORTC_DATA_R |= LCD_RS;
    LCD_Nibble((uint8_t)c >> 4);
    LCD_Nibble((uint8_t)c & 0x0F);
    delay_ms(1);
}

void LCD_String(const char *str) {
    while (*str) LCD_Char(*str++);
}

void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    LCD_Command(addr);
}

void LCD_Clear(void) {
    LCD_Command(0x01);
    delay_ms(2);
}

void LCD_Init(void) {
    delay_ms(50);
    GPIO_PORTC_DATA_R &= ~LCD_RS;
    LCD_Nibble(0x03); delay_ms(5);
    LCD_Nibble(0x03); delay_ms(1);
    LCD_Nibble(0x03); delay_ms(1);
    LCD_Nibble(0x02); delay_ms(1);
    LCD_Command(0x28);
    LCD_Command(0x0C);
    LCD_Command(0x06);
    LCD_Clear();
}

void uint32_to_str(uint32_t val, char *buf, uint8_t width) {
    buf[width] = '\0';
    for (int8_t i = width - 1; i >= 0; i--) {
        buf[i] = '0' + (val % 10);
        val   /= 10;
        if (val == 0) {
            for (int8_t j = i - 1; j >= 0; j--)
                buf[j] = ' ';
            break;
        }
    }
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
//  Encoder ISR
// ─────────────────────────────────────────────
void GPIOD_Handler(void) {
    if (GPIO_PORTD_MIS_R & (1 << 0)) {
        encoder_count++;
        GPIO_PORTD_ICR_R |= (1 << 0);
    }
}

// ─────────────────────────────────────────────
//  TASK 1: Encoder Task (Priority 3)
//  - Calculates RPM every 100ms
//  - Kicks watchdog
//  - Detects zero RPM for 3 seconds
// ─────────────────────────────────────────────
void vEncoderTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        encoder_task_kick++;

        uint32_t count;
        taskENTER_CRITICAL();
        count         = encoder_count;
        encoder_count = 0;
        taskEXIT_CRITICAL();

        uint32_t rpm = (count * 60000) / (ENCODER_PPR * SAMPLE_PERIOD_MS);

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_rpm = rpm;
            xSemaphoreGive(xRPMMutex);
        }

        // ── Zero RPM fault detection ──────────
        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (rpm == 0 && cruise_state == STATE_ACTIVE) {
                zero_rpm_count++;
                if (zero_rpm_count >= ZERO_RPM_LIMIT) {
                    cruise_state   = STATE_OFF;
                    zero_rpm_count = 0;
                    PWM0_0_CMPA_R  = PWM_PERIOD - 1;
                    led_set(1, 0, 0);
                }
            } else {
                zero_rpm_count = 0;
            }
            xSemaphoreGive(xStateMutex);
        }
    }
}

// ─────────────────────────────────────────────
//  TASK 2: Cruise Task (Priority 2)
//  - Bang-bang control every 10ms
//  - Kicks watchdog
// ─────────────────────────────────────────────
void vCruiseTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));

        cruise_task_kick++;

        CruiseState state;
        uint32_t    rpm;
        uint32_t    target;

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            state  = cruise_state;
            target = target_rpm;
            xSemaphoreGive(xStateMutex);
        } else { continue; }

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            rpm = current_rpm;
            xSemaphoreGive(xRPMMutex);
        } else { continue; }

        if (state == STATE_ACTIVE) {
            if (rpm < (target - DEADBAND)) {
                PWM_SetDuty(THROTTLE_ON);
                led_set(0, 0, 1);
            } else if (rpm > (target + DEADBAND)) {
                PWM_SetDuty(THROTTLE_OFF);
                led_set(0, 1, 0);
            }
        }
    }
}

// ─────────────────────────────────────────────
//  TASK 3: Manual Task (Priority 1)
//  - Handles buttons and pot
//  - Kicks watchdog
// ─────────────────────────────────────────────
void vManualTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));

        manual_task_kick++;

        CruiseState state;

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            state = cruise_state;
            xSemaphoreGive(xStateMutex);
        } else { continue; }

        if (button_pressed(GPIO_PORTE_DATA_R, (1 << 1))) {
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (target_rpm <= SPEED_MAX - SPEED_STEP)
                    target_rpm += SPEED_STEP;
                else
                    target_rpm = SPEED_MAX;
                xSemaphoreGive(xStateMutex);
            }
            while (!(GPIO_PORTE_DATA_R & (1 << 1)));
        }

        if (button_pressed(GPIO_PORTE_DATA_R, (1 << 0))) {
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (target_rpm >= SPEED_MIN + SPEED_STEP)
                    target_rpm -= SPEED_STEP;
                else
                    target_rpm = SPEED_MIN;
                xSemaphoreGive(xStateMutex);
            }
            while (!(GPIO_PORTE_DATA_R & (1 << 0)));
        }

        if (button_pressed(GPIO_PORTF_DATA_R, (1 << 4))) {
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                cruise_state   = STATE_ACTIVE;
                zero_rpm_count = 0;
                xSemaphoreGive(xStateMutex);
            }
            led_set(0, 1, 0);
            while (!(GPIO_PORTF_DATA_R & (1 << 4)));
        }

        if (button_pressed(GPIO_PORTF_DATA_R, (1 << 0))) {
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                cruise_state = STATE_OFF;
                xSemaphoreGive(xStateMutex);
            }
            PWM_SetDuty(THROTTLE_OFF);
            led_set(1, 0, 0);
            while (!(GPIO_PORTF_DATA_R & (1 << 0)));
        }

        if (state == STATE_OFF) {
            PWM_SetDuty(ADC_ReadThrottle());
        }
    }
}

// ─────────────────────────────────────────────
//  TASK 4: Watchdog Task (Priority 4)
//  - Checks all kick counters every 100ms
//  - Deactivates cruise if any task stalls
//    for more than 2 seconds
// ─────────────────────────────────────────────
void vWatchdogTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    uint32_t last_encoder  = 0, last_cruise  = 0, last_manual  = 0;
    uint32_t stall_encoder = 0, stall_cruise = 0, stall_manual = 0;

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));

        if (encoder_task_kick == last_encoder) stall_encoder++;
        else                                   stall_encoder = 0;
        last_encoder = encoder_task_kick;

        if (cruise_task_kick == last_cruise)   stall_cruise++;
        else                                   stall_cruise = 0;
        last_cruise = cruise_task_kick;

        if (manual_task_kick == last_manual)   stall_manual++;
        else                                   stall_manual = 0;
        last_manual = manual_task_kick;

        if (stall_encoder >= TASK_WATCHDOG_LIMIT ||
            stall_cruise  >= TASK_WATCHDOG_LIMIT ||
            stall_manual  >= TASK_WATCHDOG_LIMIT) {

            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                cruise_state = STATE_OFF;
                xSemaphoreGive(xStateMutex);
            }
            PWM_SetDuty(THROTTLE_OFF);
            led_set(1, 0, 0);

            stall_encoder = 0;
            stall_cruise  = 0;
            stall_manual  = 0;
        }
    }
}

// ─────────────────────────────────────────────
//  TASK 5: LCD Task (Priority 1)
//  Updates display every 200ms
// ─────────────────────────────────────────────
void vLCDTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    char num_buf[4];

    LCD_SetCursor(0, 0); LCD_String(" Speed:");
    LCD_SetCursor(1, 0); LCD_String("Target:");

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));

        uint32_t    rpm;
        uint32_t    target;
        CruiseState state;

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            rpm = current_rpm;
            xSemaphoreGive(xRPMMutex);
        } else { continue; }

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            target = target_rpm;
            state  = cruise_state;
            xSemaphoreGive(xStateMutex);
        } else { continue; }

        LCD_SetCursor(0, 7);
        uint32_to_str(rpm, num_buf, 4);
        LCD_String(num_buf);
        LCD_String(" RPM");

        LCD_SetCursor(1, 7);
        uint32_to_str(target, num_buf, 4);
        LCD_String(num_buf);
        LCD_String(state == STATE_ACTIVE ? " ACT" : " OFF");
    }
}

// ─────────────────────────────────────────────
//  Stack Overflow Hook
// ─────────────────────────────────────────────
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;
    led_set(1, 0, 0);
    while (1);
}

// ─────────────────────────────────────────────
//  Main
// ─────────────────────────────────────────────
int main(void) {
    GPIO_Init();
    ADC_Init();
    PWM_Init();
    LCD_Init();

    led_set(1, 0, 0);

    xRPMMutex   = xSemaphoreCreateMutex();
    xStateMutex = xSemaphoreCreateMutex();

    xTaskCreate(vEncoderTask,  "Encoder",  128, NULL, 3, NULL);
    xTaskCreate(vCruiseTask,   "Cruise",   128, NULL, 2, NULL);
    xTaskCreate(vManualTask,   "Manual",   128, NULL, 1, NULL);
    xTaskCreate(vWatchdogTask, "Watchdog", 128, NULL, 4, NULL);
    xTaskCreate(vLCDTask,      "LCD",      128, NULL, 1, NULL);

    vTaskStartScheduler();
    while (1);
}