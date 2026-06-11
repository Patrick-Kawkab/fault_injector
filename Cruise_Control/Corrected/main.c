#include <stdint.h>                // Standard integer types like uint32_t
#include "tm4c123gh6pm.h"          // TM4C123 register definitions
#include "FreeRTOS.h"              // FreeRTOS core definitions
#include "task.h"                  // Task creation / scheduling APIs
#include "semphr.h"                // Mutex / semaphore APIs

// ============================================================
// Forward declarations
// These tell the compiler which functions will appear later.
// ============================================================
void GPIO_Init(void);                                      // Configure GPIO pins used by LEDs, buttons, LCD, encoder, and PWM
void ADC_Init(void);                                       // Configure ADC to read throttle potentiometer
void PWM_Init(void);                                       // Configure PWM hardware on PB6
void LCD_Init(void);                                       // Initialize HD44780 LCD in 4-bit mode
void LCD_Pulse(void);                                      // Toggle LCD enable pin so LCD latches current nibble
void LCD_Nibble(uint8_t nibble);                           // Send 4 bits to LCD data lines
void LCD_Command(uint8_t cmd);                             // Send command byte to LCD
void LCD_Char(char c);                                     // Send one character to LCD
void LCD_String(const char *str);                          // Send string to LCD
void LCD_SetCursor(uint8_t row, uint8_t col);              // Move LCD cursor to row/column
void LCD_Clear(void);                                      // Clear LCD screen
void uint32_to_str(uint32_t val, char *buf, uint8_t width);// Convert integer to fixed-width ASCII string
void PWM_SetDuty(uint32_t duty);                           // Change PWM duty cycle (0..100%)
void led_set(uint8_t red, uint8_t green, uint8_t blue);    // Control onboard RGB LED
uint8_t button_pressed(volatile uint32_t *port_reg, uint8_t pin_mask); // Debounced active-low button read
uint32_t ADC_ReadThrottle(void);                           // Read PE3 potentiometer and scale to 0..100
void GPIOPortD_Handler(void);                              // Encoder interrupt service routine for PD0
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName); // FreeRTOS stack overflow hook
void vApplicationMallocFailedHook(void);                   // FreeRTOS malloc failure hook
void vEncoderTask(void *pvParameters);                     // Computes RPM from encoder pulses
void vCruiseTask(void *pvParameters);                      // Bang-bang cruise controller
void vManualTask(void *pvParameters);                      // Handles buttons and manual throttle
void vWatchdogTask(void *pvParameters);                    // Monitors task health
void vLCDTask(void *pvParameters);                         // Periodically updates LCD

// ============================================================
// Constants
// ============================================================
#define PWM_PERIOD              1000   // PWM counter top value; determines PWM period
#define THROTTLE_ON             80     // Duty cycle used when cruise needs acceleration
#define THROTTLE_OFF            0      // Duty cycle used when throttle should be cut
#define DEADBAND                5      // Allowable RPM error band before switching output

#define SPEED_MIN               0      // Minimum target RPM allowed
#define SPEED_MAX               300    // Maximum target RPM allowed
#define SPEED_STEP              10     // Button increment/decrement for target RPM

#define ENCODER_PPR             11     // Encoder pulses per revolution
#define SAMPLE_PERIOD_MS        100    // Encoder sampling period in ms

#define ZERO_RPM_LIMIT          30     // 30 * 100ms = about 3 seconds before auto-cancel
#define TASK_WATCHDOG_LIMIT     20     // 20 * 100ms = about 2 seconds task stall threshold
#define BUTTON_RELEASE_TIMEOUT  500    // Max wait (ms) for button release before continuing

#define WHEEL_DIAMETER_M        0.065f //Wheel diameter is 6.5 cm
#define WHEEL_CIRCUMFERENCE     (3.14159f * WHEEL_DIAMETER_M)
// ============================================================
// LCD pin definitions
// Note: PC4=RS, PC5=E, PC6=D4, PC7=D5, PB4=D6, PB5=D7
// ============================================================
#define LCD_RS                  (1 << 4) // Register Select on PC4
#define LCD_E                   (1 << 5) // Enable pin on PC5
#define LCD_D4                  (1 << 6) // LCD data bit 4 on PC6
#define LCD_D5                  (1 << 7) // LCD data bit 5 on PC7
#define LCD_D6                  (1 << 4) // LCD data bit 6 on PB4
#define LCD_D7                  (1 << 5) // LCD data bit 7 on PB5

// ============================================================
// Shared state
// These are visible to multiple tasks, so access is protected
// by mutexes where appropriate.
// ============================================================
typedef enum { STATE_OFF, STATE_ACTIVE } CruiseState; // Cruise control mode

volatile CruiseState cruise_state      = STATE_OFF; // Current cruise mode: OFF or ACTIVE
volatile uint32_t    target_rpm        = 100;       // Desired speed when cruise is active
volatile uint32_t    current_rpm       = 0;         // Measured speed from encoder
volatile uint32_t    encoder_count     = 0;         // Raw pulse count collected by ISR
volatile uint32_t    zero_rpm_count    = 0;         // Counts consecutive zero-RPM samples during cruise
volatile uint32_t    encoder_task_kick = 0;         // Heartbeat counter for encoder task
volatile uint32_t    cruise_task_kick  = 0;         // Heartbeat counter for cruise task
volatile uint32_t    manual_task_kick  = 0;         // Heartbeat counter for manual task

SemaphoreHandle_t xRPMMutex;                        // Protects current_rpm
SemaphoreHandle_t xStateMutex;                      // Protects cruise_state / target_rpm / zero_rpm_count

// ============================================================
// Busy-wait delay
// Used mainly for LCD timing. In RTOS tasks, vTaskDelay() is
// better for scheduler friendliness, but LCD init often uses
// simple blocking delays.
// ============================================================
void delay_ms(uint32_t ms) {
    volatile uint32_t count = ms * (80000000 / 3000); // Rough loop count for ~ms delay at 80MHz
    while (count--);                                   // Burn CPU cycles until count reaches zero
}

// ============================================================
// GPIO initialization
// Sets up:
//  - Port F: LEDs + onboard switches
//  - Port E: up/down buttons + ADC pot input
//  - Port D: encoder interrupt input
//  - Port C/B: LCD lines
//  - Port B: PWM output on PB6
// ============================================================
void GPIO_Init(void) {
    SYSCTL_RCGCGPIO_R |= (1 << 5);                 // Enable Port F clock
    volatile uint32_t dummy; dummy = SYSCTL_RCGCGPIO_R; (void)dummy; // Small delay after clock enable

    GPIO_PORTF_LOCK_R  = 0x4C4F434B;               // Unlock PF0 so it can be used as GPIO
    GPIO_PORTF_CR_R   |= 0x1F;                     // Allow changes to PF0..PF4
    GPIO_PORTF_DIR_R  |=  (1<<1)|(1<<2)|(1<<3);    // PF1,PF2,PF3 as outputs for RGB LED
    GPIO_PORTF_DIR_R  &= ~((1<<0)|(1<<4));         // PF0,PF4 as inputs for buttons
    GPIO_PORTF_DEN_R  |=  (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4); // Digital enable PF0..PF4
    GPIO_PORTF_PUR_R  |=  (1<<0)|(1<<4);           // Pull-ups on buttons PF0 and PF4
    GPIO_PORTF_DATA_R &= ~((1<<1)|(1<<2)|(1<<3));  // LEDs initially off

    SYSCTL_RCGCGPIO_R |= (1 << 4);                 // Enable Port E clock
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Delay after enable
    GPIO_PORTE_DIR_R  &= ~((1<<0)|(1<<1));         // PE0, PE1 as inputs (buttons)
    GPIO_PORTE_DEN_R  |=  (1<<0)|(1<<1);           // Digital enable PE0, PE1
    GPIO_PORTE_PUR_R  |=  (1<<0)|(1<<1);           // Pull-ups on PE0, PE1 buttons

    SYSCTL_RCGCGPIO_R |= (1 << 3);                 // Enable Port D clock
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Delay after enable
    GPIO_PORTD_DIR_R  &= ~(1<<0);                  // PD0 as input for encoder pulse
    GPIO_PORTD_DEN_R  |=  (1<<0);                  // Digital enable PD0
    GPIO_PORTD_PUR_R  |=  (1<<0);                  // Pull-up on PD0 in case encoder is open-collector / idle high
    GPIO_PORTD_IS_R   &= ~(1<<0);                  // Edge-sensitive interrupt (not level-sensitive)
    GPIO_PORTD_IBE_R  &= ~(1<<0);                  // Single edge, not both edges
    GPIO_PORTD_IEV_R  |=  (1<<0);                  // Rising-edge interrupt on PD0
    GPIO_PORTD_ICR_R  |=  (1<<0);                  // Clear any pending interrupt on PD0
    GPIO_PORTD_IM_R   |=  (1<<0);                  // Unmask interrupt for PD0
    NVIC_EN0_R        |=  (1 << 3);                // Enable Port D interrupt in NVIC

    SYSCTL_RCGCGPIO_R |= (1 << 2);                 // Enable Port C clock
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Delay after enable
    GPIO_PORTC_DIR_R  |=  (1<<4)|(1<<5)|(1<<6)|(1<<7); // PC4..PC7 outputs for LCD control/data
    GPIO_PORTC_DEN_R  |=  (1<<4)|(1<<5)|(1<<6)|(1<<7); // Digital enable PC4..PC7
    GPIO_PORTC_DATA_R &= ~((1<<4)|(1<<5)|(1<<6)|(1<<7)); // Clear LCD pins initially

    SYSCTL_RCGCGPIO_R |= (1 << 1);                 // Enable Port B clock
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Delay after enable
    GPIO_PORTB_DIR_R  |=  (1<<4)|(1<<5);           // PB4,PB5 outputs for LCD D6,D7
    GPIO_PORTB_DEN_R  |=  (1<<4)|(1<<5)|(1<<6);    // Digital enable PB4,PB5,PB6 (PB6 = PWM output)
    GPIO_PORTB_DATA_R &= ~((1<<4)|(1<<5)|(1<<6));  // Clear output bits initially
}

// ============================================================
// ADC initialization
// Configures ADC0 sequencer 3 to read PE3 (analog input AIN0)
// ============================================================
void ADC_Init(void) {
    SYSCTL_RCGCADC_R  |= (1 << 0);                 // Enable ADC0 clock
    SYSCTL_RCGCGPIO_R |= (1 << 4);                 // Ensure Port E clock is enabled
    volatile uint32_t dummy; dummy = SYSCTL_RCGCADC_R; (void)dummy; // Delay after clock enable

    GPIO_PORTE_AFSEL_R |= (1 << 3);                // Enable alternate function on PE3
    GPIO_PORTE_DEN_R   &= ~(1 << 3);               // Disable digital function on PE3
    GPIO_PORTE_AMSEL_R |= (1 << 3);                // Enable analog mode on PE3

    ADC0_ACTSS_R  &= ~(1 << 3);                    // Disable sample sequencer 3 before config
    ADC0_EMUX_R   &= ~0xF000;                      // Set software trigger for SS3
    ADC0_SSMUX3_R  = 0;                            // Use channel AIN0 (PE3)
    ADC0_SSCTL3_R  = 0x06;                         // One sample, end of sequence, set interrupt flag
    ADC0_ACTSS_R  |=  (1 << 3);                    // Re-enable sample sequencer 3
}

// ============================================================
// ADC read helper
// Reads potentiometer and scales raw 12-bit ADC value to 0..100
// ============================================================
uint32_t ADC_ReadThrottle(void) {
    ADC0_PSSI_R = (1 << 3);                        // Start conversion on SS3
    while (!(ADC0_RIS_R & (1 << 3)));              // Wait until conversion complete
    uint32_t val = ADC0_SSFIFO3_R & 0xFFF;         // Read 12-bit ADC result
    ADC0_ISC_R   = (1 << 3);                       // Clear completion flag
    return (val * 100) / 4095;                     // Scale to percentage 0..100
}

// ============================================================
// PWM initialization
// Sets PB6 to M0PWM0 and configures PWM generator 0 output A.
// This prepares the signal that will drive the motor driver.
// ============================================================
void PWM_Init(void) {
    SYSCTL_RCGCPWM_R  |= (1 << 0);                 // Enable PWM module 0 clock
    SYSCTL_RCGCGPIO_R |= (1 << 1);                 // Ensure Port B clock enabled
    volatile uint32_t dummy; dummy = SYSCTL_RCGCPWM_R; (void)dummy; // Delay after enable

    SYSCTL_RCC_R &= ~(1 << 20);                    // Use PWM clock derived from system clock configuration
    GPIO_PORTB_AFSEL_R |= (1 << 6);                // PB6 uses alternate function instead of plain GPIO
    GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R & ~(0xF << 24)) | (4 << 24); // Select PWM function on PB6
    GPIO_PORTB_DEN_R  |= (1 << 6);                 // Enable digital function on PB6

    PWM0_0_CTL_R   = 0;                            // Disable PWM generator 0 while configuring
    PWM0_0_GENA_R  = 0x8C;                         // Define output actions for PWM output A
    PWM0_0_LOAD_R  = PWM_PERIOD - 1;               // Set PWM period (timer counts from LOAD down)
    PWM0_0_CMPA_R  = 0;                            // Initial compare value
    PWM0_0_CTL_R   = 1;                            // Enable PWM generator 0
    PWM0_ENABLE_R |= (1 << 0);                     // Enable PWM output channel M0PWM0
}

// ============================================================
// PWM duty update
// This is the function that actually changes motor power.
// If duty is 0 -> motor command off.
// If duty is larger -> motor driver receives more average power.
// ============================================================
void PWM_SetDuty(uint32_t duty) {
    if (duty > 100) duty = 100;                    // Clamp to valid percentage range
    uint32_t ticks = (duty * PWM_PERIOD) / 100;    // Convert percentage into counter ticks
    if (ticks >= PWM_PERIOD) ticks = PWM_PERIOD - 1; // Prevent invalid compare value
    PWM0_0_CMPA_R = PWM_PERIOD - 1 - ticks;        // Update PWM compare register for chosen mode
}

// ============================================================
// LCD low-level pulse
// Generates an enable pulse so the LCD latches the nibble.
// ============================================================
void LCD_Pulse(void) {
    GPIO_PORTC_DATA_R |= LCD_E;                    // Set E high
    delay_ms(1);                                   // Small hold time
    GPIO_PORTC_DATA_R &= ~LCD_E;                   // Set E low; LCD latches data here
    delay_ms(1);                                   // Wait for LCD to process nibble
}

// ============================================================
// Send 4-bit nibble to LCD data lines
// D4,D5 are on Port C, D6,D7 are on Port B.
// ============================================================
void LCD_Nibble(uint8_t nibble) {
    if (nibble & 0x01) GPIO_PORTC_DATA_R |= LCD_D4; else GPIO_PORTC_DATA_R &= ~LCD_D4; // Drive LCD D4
    if (nibble & 0x02) GPIO_PORTC_DATA_R |= LCD_D5; else GPIO_PORTC_DATA_R &= ~LCD_D5; // Drive LCD D5
    if (nibble & 0x04) GPIO_PORTB_DATA_R |= LCD_D6; else GPIO_PORTB_DATA_R &= ~LCD_D6; // Drive LCD D6
    if (nibble & 0x08) GPIO_PORTB_DATA_R |= LCD_D7; else GPIO_PORTB_DATA_R &= ~LCD_D7; // Drive LCD D7
    LCD_Pulse();                                   // Tell LCD to latch the nibble
}

// ============================================================
// Send command byte to LCD
// RS=0 means command register.
// ============================================================
void LCD_Command(uint8_t cmd) {
    GPIO_PORTC_DATA_R &= ~LCD_RS;                  // RS=0 -> command mode
    LCD_Nibble(cmd >> 4);                          // Send high nibble first
    LCD_Nibble(cmd & 0x0F);                        // Then low nibble
    delay_ms(2);                                   // Give LCD time to execute command
}

// ============================================================
// Send one character to LCD
// RS=1 means data register.
// ============================================================
void LCD_Char(char c) {
    GPIO_PORTC_DATA_R |= LCD_RS;                   // RS=1 -> data mode
    LCD_Nibble((uint8_t)c >> 4);                   // Send high nibble
    LCD_Nibble((uint8_t)c & 0x0F);                 // Send low nibble
    delay_ms(1);                                   // Small processing delay
}

// ============================================================
// Send null-terminated string to LCD
// ============================================================
void LCD_String(const char *str) {
    while (*str) LCD_Char(*str++);                 // Print characters until null terminator
}

// ============================================================
// Move LCD cursor
// Row 0 starts at 0x80, row 1 starts at 0xC0 for 16x2 LCDs.
// ============================================================
void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col); // Compute DDRAM address
    LCD_Command(addr);                              // Send set-cursor command
}

// ============================================================
// Clear LCD
// ============================================================
void LCD_Clear(void) {
    LCD_Command(0x01);                             // Clear display command
    delay_ms(2);                                   // LCD clear needs extra time
}

// ============================================================
// LCD initialization sequence for HD44780 in 4-bit mode
// ============================================================
void LCD_Init(void) {
    delay_ms(50);                                  // Wait for LCD power-up stabilization
    GPIO_PORTC_DATA_R &= ~LCD_RS;                  // RS low for command mode
    LCD_Nibble(0x03); delay_ms(5);                 // Wake-up step 1
    LCD_Nibble(0x03); delay_ms(1);                 // Wake-up step 2
    LCD_Nibble(0x03); delay_ms(1);                 // Wake-up step 3
    LCD_Nibble(0x02); delay_ms(1);                 // Switch to 4-bit mode
    LCD_Command(0x28);                             // Function set: 4-bit, 2-line, 5x8 dots
    LCD_Command(0x0C);                             // Display ON, cursor OFF, blink OFF
    LCD_Command(0x06);                             // Entry mode: increment cursor
    LCD_Clear();                                   // Clear screen
}

// ============================================================
// Convert unsigned integer to fixed-width string
// Example width=4, value=25 -> "  25"
// ============================================================
void uint32_to_str(uint32_t val, char *buf, uint8_t width) {
    buf[width] = '\0';                             // Null-terminate string at desired width
    for (int8_t i = width - 1; i >= 0; i--) {      // Fill from right to left
        buf[i] = '0' + (val % 10);                 // Extract least significant digit
        val /= 10;                                 // Remove last digit
        if (val == 0) {                            // If no more digits remain
            for (int8_t j = i - 1; j >= 0; j--)    // Fill remaining left positions with spaces
                buf[j] = ' ';
            break;
        }
    }
}

// ============================================================
// Debounced button press detection
// Buttons are active-low because pull-ups are enabled.
// This function rereads the LIVE register after debounce delay.
// ============================================================
uint8_t button_pressed(volatile uint32_t *port_reg, uint8_t pin_mask) {
    if (((*port_reg) & pin_mask) == 0) {           // Check if button currently reads pressed (low)
        vTaskDelay(pdMS_TO_TICKS(20));             // Debounce delay; lets bouncing settle
        if (((*port_reg) & pin_mask) == 0)         // Read again from actual register after debounce
            return 1;                              // Confirmed valid press
    }
    return 0;                                      // Not pressed or bounce only
}

// ============================================================
// Set RGB LED color on Port F
// PF1=red, PF2=blue, PF3=green
// ============================================================
void led_set(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t val = GPIO_PORTF_DATA_R & ~((1<<1)|(1<<2)|(1<<3)); // Clear LED bits while keeping other PF bits unchanged
    if (red)   val |= (1<<1);                      // Add red if requested
    if (blue)  val |= (1<<2);                      // Add blue if requested
    if (green) val |= (1<<3);                      // Add green if requested
    GPIO_PORTF_DATA_R = val;                       // Write new LED state
}

// ============================================================
// Encoder interrupt handler
// Each rising edge on PD0 increments pulse count.
// ============================================================
void GPIOPortD_Handler(void) {
    if (GPIO_PORTD_MIS_R & (1 << 0)) {             // Check if PD0 caused the interrupt
        encoder_count++;                           // Count one encoder pulse
        GPIO_PORTD_ICR_R |= (1 << 0);              // Clear interrupt flag so next pulse can be detected
    }
}

// ============================================================
// Encoder task
// Runs every 100 ms:
//  1) Captures and clears encoder pulse count
//  2) Converts pulses to RPM
//  3) Stores RPM safely
//  4) If cruise is active and RPM stays zero for ~3 seconds,
//     cruise is cancelled and motor command is shut off.
// ============================================================
void vEncoderTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Save current tick so vTaskDelayUntil is periodic
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SAMPLE_PERIOD_MS)); // Wait until next 100ms boundary
        encoder_task_kick++;                        // Heartbeat for software watchdog

        uint32_t count;
        taskENTER_CRITICAL();                       // Prevent ISR/task race while copying and clearing count
        count = encoder_count;                      // Copy current pulse count accumulated by ISR
        encoder_count = 0;                          // Reset counter for next sample window
        taskEXIT_CRITICAL();                        // Re-enable interrupts / leave critical section

        uint32_t rpm = (count * 60000UL) / (ENCODER_PPR * SAMPLE_PERIOD_MS); // pulses/100ms -> pulses/min -> revolutions/min

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(10)) == pdTRUE) { // Lock RPM shared variable
            current_rpm = rpm;                      // Publish new measured RPM
            xSemaphoreGive(xRPMMutex);              // Unlock RPM mutex
        }

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock cruise state variables
            if (rpm == 0 && cruise_state == STATE_ACTIVE) { // Only care about zero-RPM if cruise is active
                zero_rpm_count++;                   // Count another zero-RPM sample
                if (zero_rpm_count >= ZERO_RPM_LIMIT) { // If zero RPM persisted for about 3 seconds
                    cruise_state = STATE_OFF;       // Cancel cruise control
                    zero_rpm_count = 0;             // Reset fault counter
                    PWM_SetDuty(THROTTLE_OFF);      // Remove motor command
                    led_set(1, 0, 0);               // Red LED indicates OFF/fault state
                }
            } else {
                zero_rpm_count = 0;                 // RPM is non-zero or cruise is off -> clear counter
            }
            xSemaphoreGive(xStateMutex);            // Unlock state mutex
        }
    }
}

// ============================================================
// Cruise task
// Runs every 10 ms and performs simple bang-bang control:
//  - If RPM is below target minus deadband -> throttle ON
//  - If RPM is above target plus deadband -> throttle OFF
// ============================================================
void vCruiseTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Establish periodic reference
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10)); // Execute every 10 ms
        cruise_task_kick++;                         // Heartbeat for watchdog

        CruiseState state;
        uint32_t rpm;
        uint32_t target;

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Read shared state safely
            state  = cruise_state;                  // Snapshot current cruise mode
            target = target_rpm;                    // Snapshot current target speed
            xSemaphoreGive(xStateMutex);            // Release state mutex
        } else {
            continue;                               // Skip this cycle if mutex unavailable
        }

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Read shared RPM safely
            rpm = current_rpm;                      // Snapshot measured RPM
            xSemaphoreGive(xRPMMutex);              // Release RPM mutex
        } else {
            continue;                               // Skip this cycle if mutex unavailable
        }

        if (state == STATE_ACTIVE) {               // Only control motor automatically when cruise is active
            if (rpm < (target - DEADBAND)) {       // Vehicle slower than desired speed window
                PWM_SetDuty(THROTTLE_ON);           // Apply throttle / power to motor driver
                led_set(0, 0, 1);                  // Blue LED = accelerating / adding throttle
            } else if (rpm > (target + DEADBAND)) {// Vehicle faster than desired speed window
                PWM_SetDuty(THROTTLE_OFF);          // Cut throttle
                led_set(0, 1, 0);                  // Green LED = above target / backing off
            }
        }
    }
}

// ============================================================
// Manual task
// Runs every 20 ms:
//  - PE1 increases target RPM
//  - PE0 decreases target RPM
//  - PF4 enables cruise
//  - PF0 cancels cruise
//  - If cruise is OFF, potentiometer controls throttle directly
// Release waits have timeouts so a stuck button does not lock task.
// ============================================================
void vManualTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Establish periodic reference
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20)); // Run every 20 ms
        manual_task_kick++;                         // Heartbeat for watchdog

        CruiseState state;
        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Read current cruise state safely
            state = cruise_state;                   // Snapshot whether cruise is ON or OFF
            xSemaphoreGive(xStateMutex);            // Release state mutex
        } else {
            continue;                               // Skip cycle if mutex busy
        }

        if (button_pressed(&GPIO_PORTE_DATA_R, (1 << 1))) { // PE1 pressed -> increase target RPM
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                if (target_rpm <= SPEED_MAX - SPEED_STEP) target_rpm += SPEED_STEP; // Increase if not near max
                else target_rpm = SPEED_MAX;         // Otherwise clamp to max
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            uint32_t t = 0;                          // Release timeout counter
            while (!(GPIO_PORTE_DATA_R & (1 << 1)) && t++ < BUTTON_RELEASE_TIMEOUT) { // Wait until button released or timeout expires
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield for 1 ms during wait
            }
        }

        if (button_pressed(&GPIO_PORTE_DATA_R, (1 << 0))) { // PE0 pressed -> decrease target RPM
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                if (target_rpm >= SPEED_MIN + SPEED_STEP) target_rpm -= SPEED_STEP; // Decrease if above min
                else target_rpm = SPEED_MIN;         // Otherwise clamp to min
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            uint32_t t = 0;                          // Release timeout counter
            while (!(GPIO_PORTE_DATA_R & (1 << 0)) && t++ < BUTTON_RELEASE_TIMEOUT) { // Wait for release or timeout
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield during wait
            }
        }

        if (button_pressed(&GPIO_PORTF_DATA_R, (1 << 4))) { // PF4 pressed -> activate cruise
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                cruise_state = STATE_ACTIVE;         // Turn cruise mode on
                zero_rpm_count = 0;                  // Clear zero-RPM fault counter
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            led_set(0, 1, 0);                        // Green LED indicates cruise engaged state
            uint32_t t = 0;                          // Release timeout counter
            while (!(GPIO_PORTF_DATA_R & (1 << 4)) && t++ < BUTTON_RELEASE_TIMEOUT) { // Wait for release or timeout
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield during wait
            }
        }

        if (button_pressed(&GPIO_PORTF_DATA_R, (1 << 0))) { // PF0 pressed -> cancel cruise
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                cruise_state = STATE_OFF;            // Turn cruise mode off
                zero_rpm_count = 0;                  // Clear zero-RPM fault counter
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            PWM_SetDuty(THROTTLE_OFF);               // Remove motor throttle immediately
            led_set(1, 0, 0);                        // Red LED = off/cancelled
            uint32_t t = 0;                          // Release timeout counter
            while (!(GPIO_PORTF_DATA_R & (1 << 0)) && t++ < BUTTON_RELEASE_TIMEOUT) { // Wait for release or timeout
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield during wait
            }
        }

        if (state == STATE_OFF) {                    // Manual throttle should only work when cruise is OFF
            PWM_SetDuty(ADC_ReadThrottle());         // Set motor command directly from potentiometer value
        }
    }
}

// ============================================================
// Watchdog task
// Runs every 100 ms and checks whether critical tasks are still
// incrementing their heartbeat counters.
// If any task appears stalled for too long, cruise is cancelled.
// ============================================================
void vWatchdogTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Establish periodic reference
    uint32_t last_encoder = 0, last_cruise = 0, last_manual = 0; // Previous heartbeat snapshots
    uint32_t stall_encoder = 0, stall_cruise = 0, stall_manual = 0; // Consecutive stall counters

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100)); // Check every 100 ms

        if (encoder_task_kick == last_encoder) stall_encoder++; else stall_encoder = 0; // Detect encoder task stall
        last_encoder = encoder_task_kick;        // Update last-seen encoder heartbeat

        if (cruise_task_kick == last_cruise) stall_cruise++; else stall_cruise = 0; // Detect cruise task stall
        last_cruise = cruise_task_kick;          // Update last-seen cruise heartbeat

        if (manual_task_kick == last_manual) stall_manual++; else stall_manual = 0; // Detect manual task stall
        last_manual = manual_task_kick;          // Update last-seen manual heartbeat

        if (stall_encoder >= TASK_WATCHDOG_LIMIT || stall_cruise >= TASK_WATCHDOG_LIMIT || stall_manual >= TASK_WATCHDOG_LIMIT) { // If any task stalled too long
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                cruise_state = STATE_OFF;         // Force cruise off as fail-safe
                zero_rpm_count = 0;               // Clear zero-RPM fault counter
                xSemaphoreGive(xStateMutex);      // Release mutex
            }
            PWM_SetDuty(THROTTLE_OFF);            // Remove motor command as fail-safe
            led_set(1, 0, 0);                     // Red LED indicates fail-safe/off
            stall_encoder = stall_cruise = stall_manual = 0; // Reset counters after action
        }
    }
}

// ============================================================
// LCD task
// Runs every 200 ms and updates displayed speed, target RPM,
// and cruise state.
// ============================================================
void vLCDTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Establish periodic reference
    char num_buf[5];                                // 4 chars + null terminator for formatted numbers

    LCD_SetCursor(0, 0); LCD_String(" Speed:");    // Static label on first row
    LCD_SetCursor(1, 0); LCD_String("Target:");    // Static label on second row

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200)); // Refresh every 200 ms

        uint32_t rpm;
        uint32_t target;
        CruiseState state;
        
        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            rpm = current_rpm;
            xSemaphoreGive(xRPMMutex);
        } else {
            continue;
        }

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) { // Read shared state safely
            target = target_rpm;                    // Snapshot target RPM
            state  = cruise_state;                  // Snapshot cruise state
            xSemaphoreGive(xStateMutex);            // Release state mutex
        } else {
            continue;                               // Skip update if mutex unavailable
        }
        uint32_t kph = (rpm * WHEEL_CIRCUMFERENCE * WHEEL_DIAMETER_M) / 11;

        LCD_SetCursor(0, 7);                        // Move cursor after " Speed:"
        uint32_to_str(kph, num_buf, 4);             // Format RPM into 4-character field
        LCD_String(num_buf);                        // Print current RPM
        LCD_String(" KPH");                         // Print unit label

        LCD_SetCursor(1, 7);                        // Move cursor after "Target:"
        uint32_to_str(target, num_buf, 4);          // Format target RPM into 4-character field
        LCD_String(num_buf);                        // Print target RPM
        LCD_String(state == STATE_ACTIVE ? " ACT" : " OFF"); // Show cruise state text
    }
}

// ============================================================
// FreeRTOS hook: stack overflow
// If any task overflows its stack, this hook runs.
// Here we show red LED and halt forever.
// ============================================================
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;                                    // Suppress unused parameter warning
    (void)pcTaskName;                               // Suppress unused parameter warning
    led_set(1, 0, 0);                               // Red LED indicates fatal fault
    while (1);                                      // Stop system here for debugging
}

// ============================================================
// FreeRTOS hook: malloc failure
// If heap allocation fails, this hook runs.
// Here we show red+blue LED and halt forever.
// ============================================================
void vApplicationMallocFailedHook(void) {
    led_set(1, 0, 1);                               // Purple/magenta style indicator for heap failure
    while (1);                                      // Stop system here for debugging
}

// ============================================================
// main
// Initializes all peripherals, creates mutexes and tasks,
// then starts the FreeRTOS scheduler.
// ============================================================
int main(void) {
    GPIO_Init();                                    // Set up buttons, LEDs, LCD pins, encoder pin, and PWM pin
    ADC_Init();                                     // Set up potentiometer analog input
    PWM_Init();                                     // Set up PWM hardware for motor control
    LCD_Init();                                     // Initialize LCD before scheduler starts

    led_set(1, 0, 0);                               // Red LED at startup (initial OFF state)

    xRPMMutex   = xSemaphoreCreateMutex();          // Create mutex for current_rpm shared variable
    xStateMutex = xSemaphoreCreateMutex();          // Create mutex for cruise state shared variables

    configASSERT(xRPMMutex   != NULL);              // Halt in debug if RPM mutex creation failed
    configASSERT(xStateMutex != NULL);              // Halt in debug if state mutex creation failed

    configASSERT(xTaskCreate(vEncoderTask,  "Encoder",  256, NULL, 3, NULL) == pdPASS);  // Create encoder task
    configASSERT(xTaskCreate(vCruiseTask,   "Cruise",   256, NULL, 2, NULL) == pdPASS);  // Create cruise control task
    configASSERT(xTaskCreate(vManualTask,   "Manual",   256, NULL, 1, NULL) == pdPASS);  // Create manual input task
    configASSERT(xTaskCreate(vWatchdogTask, "Watchdog", 256, NULL, 4, NULL) == pdPASS);  // Create software watchdog task
    configASSERT(xTaskCreate(vLCDTask,      "LCD",      384, NULL, 1, NULL) == pdPASS);  // Create LCD display task

    vTaskStartScheduler();                          // Start FreeRTOS scheduler; tasks take over from here
    while (1);                                      // Should never get here unless scheduler fails
}