#include <stdint.h>                 // Standard integer types like uint32_t
#include "tm4c123gh6pm.h"          // TM4C123 register definitions
#include "FreeRTOS.h"              // FreeRTOS base definitions
#include "task.h"                  // FreeRTOS task functions
#include "semphr.h"                // FreeRTOS semaphore / mutex functions

// ============================================================
// Forward declarations
// These function prototypes let the compiler know about all
// functions before their actual definitions appear below.
// ============================================================
void GPIO_Init(void);                                      // Configure GPIO for LEDs, buttons, LCD, encoder, PWM
void ADC_Init(void);                                       // Configure ADC input for potentiometer throttle
void PWM_Init(void);                                       // Configure PWM output on PB6
void LCD_Init(void);                                       // Initialize HD44780 LCD in 4-bit mode
void LCD_Pulse(void);                                      // Generate LCD enable pulse
void LCD_Nibble(uint8_t nibble);                           // Send 4-bit nibble to LCD
void LCD_Command(uint8_t cmd);                             // Send command byte to LCD
void LCD_Char(char c);                                     // Send one character to LCD
void LCD_String(const char *str);                          // Send a full string to LCD
void LCD_SetCursor(uint8_t row, uint8_t col);              // Move LCD cursor
void LCD_Clear(void);                                      // Clear LCD screen
void uint32_to_str(uint32_t val, char *buf, uint8_t width);// Convert integer to fixed-width string
void PWM_SetDuty(uint32_t duty);                           // Change PWM duty cycle (0..100%)
void led_set(uint8_t red, uint8_t green, uint8_t blue);    // Control onboard RGB LED
uint8_t button_pressed(volatile uint32_t *port_reg, uint8_t pin_mask); // Debounced button read
uint32_t ADC_ReadThrottle(void);                           // Read potentiometer and scale to 0..100
void GPIOPortD_Handler(void);                              // Encoder interrupt handler
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName); // FreeRTOS stack overflow hook
void vApplicationMallocFailedHook(void);                   // FreeRTOS heap allocation failure hook
void vEncoderTask(void *pvParameters);                     // Task that computes RPM from encoder counts
void vCruiseTask(void *pvParameters);                      // Cruise control task
void vManualTask(void *pvParameters);                      // Manual control task
void vLCDTask(void *pvParameters);                         // LCD display update task

// ============================================================
// Constants
// ============================================================
#define PWM_PERIOD              1000   // PWM counter top value
#define THROTTLE_ON             80     // Duty cycle used when cruise wants acceleration
#define THROTTLE_OFF            0      // Duty cycle used when throttle should be off
#define DEADBAND                5      // RPM tolerance band around target RPM

#define SPEED_MIN               0      // Minimum allowed target RPM
#define SPEED_MAX               300    // Maximum allowed target RPM
#define SPEED_STEP              10     // Step added/subtracted when buttons are pressed

#define ENCODER_PPR             11     // Encoder pulses per revolution
#define SAMPLE_PERIOD_MS        100    // Encoder sampling period in milliseconds

#define BUTTON_RELEASE_TIMEOUT  500    // Max time to wait for button release before continuing

#define WHEEL_DIAMETER_M        0.065f //Wheel diameter is 6.5 cm
#define WHEEL_CIRCUMFERENCE     (3.14159f * WHEEL_DIAMETER_M)
// ============================================================
// LCD pin definitions used by this exact code
// RS=PC4, E=PC5, D4=PC6, D5=PC7, D6=PB4, D7=PB5
// ============================================================
#define LCD_RS                  (1 << 4) // LCD Register Select on PC4
#define LCD_E                   (1 << 5) // LCD Enable pin on PC5
#define LCD_D4                  (1 << 6) // LCD data line D4 on PC6
#define LCD_D5                  (1 << 7) // LCD data line D5 on PC7
#define LCD_D6                  (1 << 4) // LCD data line D6 on PB4
#define LCD_D7                  (1 << 5) // LCD data line D7 on PB5

// ============================================================
// Shared state
// These variables are shared across multiple tasks.
// Unlike the protected version, this unprotected version has:
//  - NO zero-RPM fault counter
//  - NO watchdog task
//  - NO task heartbeat counters
// ============================================================
typedef enum { STATE_OFF, STATE_ACTIVE } CruiseState; // Two possible operating modes

volatile CruiseState cruise_state  = STATE_OFF; // Current cruise mode: OFF or ACTIVE
volatile uint32_t    target_rpm    = 100;       // Target RPM selected by user buttons
volatile uint32_t    current_rpm   = 0;         // Measured RPM calculated from encoder
volatile uint32_t    encoder_count = 0;         // Raw pulse count incremented by encoder ISR

SemaphoreHandle_t xRPMMutex;                    // Protects current_rpm
SemaphoreHandle_t xStateMutex;                  // Protects cruise_state and target_rpm

// ============================================================
// Busy-wait delay
// Used mainly for LCD timing. This blocks the CPU, but it is
// acceptable for short LCD pulses / init timing in this design.
// ============================================================
void delay_ms(uint32_t ms) {
    volatile uint32_t count = ms * (80000000 / 3000); // Approximate loop count for ms delay at 80MHz
    while (count--);                                   // Wait until counter finishes
}

// ============================================================
// GPIO initialization
// Configures:
//  - Port F: onboard LEDs + SW1/SW2 buttons
//  - Port E: up/down buttons + analog throttle input
//  - Port D: encoder input with interrupt
//  - Port C/B: LCD connections
//  - Port B: PWM output pin PB6
// ============================================================
void GPIO_Init(void) {
    SYSCTL_RCGCGPIO_R |= (1 << 5);                 // Enable clock for Port F
    volatile uint32_t dummy; dummy = SYSCTL_RCGCGPIO_R; (void)dummy; // Small delay after clock enable

    GPIO_PORTF_LOCK_R  = 0x4C4F434B;               // Unlock PF0 so it can be configured as GPIO
    GPIO_PORTF_CR_R   |= 0x1F;                     // Allow configuration changes on PF0..PF4
    GPIO_PORTF_DIR_R  |=  (1<<1)|(1<<2)|(1<<3);    // PF1,PF2,PF3 as outputs for RGB LED
    GPIO_PORTF_DIR_R  &= ~((1<<0)|(1<<4));         // PF0,PF4 as inputs for buttons
    GPIO_PORTF_DEN_R  |=  (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4); // Digital enable PF0..PF4
    GPIO_PORTF_PUR_R  |=  (1<<0)|(1<<4);           // Enable pull-up resistors on PF0 and PF4 buttons
    GPIO_PORTF_DATA_R &= ~((1<<1)|(1<<2)|(1<<3));  // Turn LEDs off initially

    SYSCTL_RCGCGPIO_R |= (1 << 4);                 // Enable clock for Port E
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Short delay after clock enable
    GPIO_PORTE_DIR_R  &= ~((1<<0)|(1<<1));         // PE0 and PE1 as inputs (buttons)
    GPIO_PORTE_DEN_R  |=  (1<<0)|(1<<1);           // Enable digital mode for PE0 and PE1
    GPIO_PORTE_PUR_R  |=  (1<<0)|(1<<1);           // Enable pull-ups for PE0 and PE1 buttons

    SYSCTL_RCGCGPIO_R |= (1 << 3);                 // Enable clock for Port D
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Delay after clock enable
    GPIO_PORTD_DIR_R  &= ~(1<<0);                  // PD0 as input for encoder pulses
    GPIO_PORTD_DEN_R  |=  (1<<0);                  // Enable digital function on PD0
    GPIO_PORTD_PUR_R  |=  (1<<0);                  // Enable pull-up on PD0
    GPIO_PORTD_IS_R   &= ~(1<<0);                  // Configure PD0 interrupt as edge-sensitive
    GPIO_PORTD_IBE_R  &= ~(1<<0);                  // Single-edge interrupt, not both edges
    GPIO_PORTD_IEV_R  |=  (1<<0);                  // Rising-edge interrupt on PD0
    GPIO_PORTD_ICR_R  |=  (1<<0);                  // Clear any pending interrupt on PD0
    GPIO_PORTD_IM_R   |=  (1<<0);                  // Unmask interrupt on PD0
    NVIC_EN0_R        |=  (1 << 3);                // Enable Port D interrupt in NVIC

    SYSCTL_RCGCGPIO_R |= (1 << 2);                 // Enable clock for Port C
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Delay after enable
    GPIO_PORTC_DIR_R  |=  (1<<4)|(1<<5)|(1<<6)|(1<<7); // PC4..PC7 outputs for LCD
    GPIO_PORTC_DEN_R  |=  (1<<4)|(1<<5)|(1<<6)|(1<<7); // Enable digital mode on PC4..PC7
    GPIO_PORTC_DATA_R &= ~((1<<4)|(1<<5)|(1<<6)|(1<<7)); // Clear LCD pins initially

    SYSCTL_RCGCGPIO_R |= (1 << 1);                 // Enable clock for Port B
    dummy = SYSCTL_RCGCGPIO_R; (void)dummy;        // Delay after enable
    GPIO_PORTB_DIR_R  |=  (1<<4)|(1<<5);           // PB4 and PB5 outputs for LCD D6/D7
    GPIO_PORTB_DEN_R  |=  (1<<4)|(1<<5)|(1<<6);    // Enable digital mode on PB4, PB5, PB6
    GPIO_PORTB_DATA_R &= ~((1<<4)|(1<<5)|(1<<6));  // Clear PB outputs initially
}

// ============================================================
// ADC initialization
// Configures ADC0 sequencer 3 to read analog channel on PE3.
// The potentiometer acts as manual throttle input.
// ============================================================
void ADC_Init(void) {
    SYSCTL_RCGCADC_R  |= (1 << 0);                 // Enable ADC0 clock
    SYSCTL_RCGCGPIO_R |= (1 << 4);                 // Make sure Port E clock is enabled
    volatile uint32_t dummy; dummy = SYSCTL_RCGCADC_R; (void)dummy; // Delay after clock enable

    GPIO_PORTE_AFSEL_R |= (1 << 3);                // Enable alternate function on PE3
    GPIO_PORTE_DEN_R   &= ~(1 << 3);               // Disable digital mode on PE3
    GPIO_PORTE_AMSEL_R |= (1 << 3);                // Enable analog mode on PE3

    ADC0_ACTSS_R  &= ~(1 << 3);                    // Disable sample sequencer 3 before configuring
    ADC0_EMUX_R   &= ~0xF000;                      // Select software trigger for SS3
    ADC0_SSMUX3_R  = 0;                            // Select channel AIN0 (PE3)
    ADC0_SSCTL3_R  = 0x06;                         // End of sequence + set interrupt status after sample
    ADC0_ACTSS_R  |=  (1 << 3);                    // Re-enable sample sequencer 3
}

// ============================================================
// ADC read helper
// Starts a conversion, waits for it to finish, reads the value,
// and scales it from 0..4095 down to 0..100.
// ============================================================
uint32_t ADC_ReadThrottle(void) {
    ADC0_PSSI_R = (1 << 3);                        // Start conversion on sequencer 3
    while (!(ADC0_RIS_R & (1 << 3)));              // Wait until conversion completes
    uint32_t val = ADC0_SSFIFO3_R & 0xFFF;         // Read 12-bit ADC result
    ADC0_ISC_R   = (1 << 3);                       // Clear ADC completion flag
    return (val * 100) / 4095;                     // Return percentage value 0..100
}

// ============================================================
// PWM initialization
// Configures PB6 as PWM output M0PWM0.
// This prepares the control signal that drives the motor driver.
// ============================================================
void PWM_Init(void) {
    SYSCTL_RCGCPWM_R  |= (1 << 0);                 // Enable clock for PWM module 0
    SYSCTL_RCGCGPIO_R |= (1 << 1);                 // Ensure Port B clock is enabled
    volatile uint32_t dummy; dummy = SYSCTL_RCGCPWM_R; (void)dummy; // Delay after enable

    SYSCTL_RCC_R &= ~(1 << 20);                    // Use PWM clock based on system clock config
    GPIO_PORTB_AFSEL_R |= (1 << 6);                // Put PB6 in alternate function mode
    GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R & ~(0xF << 24)) | (4 << 24); // Select PWM function on PB6
    GPIO_PORTB_DEN_R  |= (1 << 6);                 // Enable digital mode on PB6

    PWM0_0_CTL_R   = 0;                            // Disable PWM generator while configuring
    PWM0_0_GENA_R  = 0x8C;                         // Configure output A actions
    PWM0_0_LOAD_R  = PWM_PERIOD - 1;               // Set PWM period
    PWM0_0_CMPA_R  = 0;                            // Initial duty cycle = 0%
    PWM0_0_CTL_R   = 1;                            // Enable PWM generator 0
    PWM0_ENABLE_R |= (1 << 0);                     // Enable PWM output channel M0PWM0
}

// ============================================================
// PWM duty setter
// This is the function that directly changes motor power.
// Larger duty cycle = more average power delivered to driver.
// ============================================================
void PWM_SetDuty(uint32_t duty) {
    if (duty > 100) duty = 100;                    // Limit duty cycle to 100%
    uint32_t ticks = (duty * PWM_PERIOD) / 100;    // Convert percentage to timer ticks
    if (ticks >= PWM_PERIOD) ticks = PWM_PERIOD - 1; // Prevent invalid compare value
    PWM0_0_CMPA_R = PWM_PERIOD - 1 - ticks;        // Update compare register for PWM output
}

// ============================================================
// LCD low-level enable pulse
// LCD latches incoming nibble when E transitions.
// ============================================================
void LCD_Pulse(void) {
    GPIO_PORTC_DATA_R |=  LCD_E;                   // Set LCD enable high
    delay_ms(1);                                   // Hold briefly
    GPIO_PORTC_DATA_R &= ~LCD_E;                   // Set LCD enable low -> data latched
    delay_ms(1);                                   // Wait for LCD to settle
}

// ============================================================
// Send a 4-bit nibble to LCD data lines D4..D7.
// ============================================================
void LCD_Nibble(uint8_t nibble) {
    if (nibble & 0x01) GPIO_PORTC_DATA_R |= LCD_D4; else GPIO_PORTC_DATA_R &= ~LCD_D4; // Write bit0 to D4
    if (nibble & 0x02) GPIO_PORTC_DATA_R |= LCD_D5; else GPIO_PORTC_DATA_R &= ~LCD_D5; // Write bit1 to D5
    if (nibble & 0x04) GPIO_PORTB_DATA_R |= LCD_D6; else GPIO_PORTB_DATA_R &= ~LCD_D6; // Write bit2 to D6
    if (nibble & 0x08) GPIO_PORTB_DATA_R |= LCD_D7; else GPIO_PORTB_DATA_R &= ~LCD_D7; // Write bit3 to D7
    LCD_Pulse();                                   // Latch nibble into LCD
}

// ============================================================
// Send command byte to LCD
// RS = 0 tells LCD this is a command, not display data.
// ============================================================
void LCD_Command(uint8_t cmd) {
    GPIO_PORTC_DATA_R &= ~LCD_RS;                  // RS = 0 -> command mode
    LCD_Nibble(cmd >> 4);                          // Send high nibble first
    LCD_Nibble(cmd & 0x0F);                        // Send low nibble second
    delay_ms(2);                                   // Let LCD execute the command
}

// ============================================================
// Send one character byte to LCD
// RS = 1 tells LCD this is display data.
// ============================================================
void LCD_Char(char c) {
    GPIO_PORTC_DATA_R |= LCD_RS;                   // RS = 1 -> character/data mode
    LCD_Nibble((uint8_t)c >> 4);                   // Send high nibble
    LCD_Nibble((uint8_t)c & 0x0F);                 // Send low nibble
    delay_ms(1);                                   // Small LCD processing delay
}

// ============================================================
// Send a null-terminated string to LCD
// ============================================================
void LCD_String(const char *str) {
    while (*str) LCD_Char(*str++);                 // Print characters until null terminator reached
}

// ============================================================
// Move LCD cursor to desired row/column
// ============================================================
void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col); // Compute DDRAM address for 16x2 LCD
    LCD_Command(addr);                              // Send set-cursor command
}

// ============================================================
// Clear LCD
// ============================================================
void LCD_Clear(void) {
    LCD_Command(0x01);                             // LCD clear display command
    delay_ms(2);                                   // Clear command needs longer delay
}

// ============================================================
// LCD initialization for 4-bit mode
// ============================================================
void LCD_Init(void) {
    delay_ms(50);                                  // Wait for LCD power-up stabilization
    GPIO_PORTC_DATA_R &= ~LCD_RS;                  // Ensure command mode
    LCD_Nibble(0x03); delay_ms(5);                 // Initialization sequence step 1
    LCD_Nibble(0x03); delay_ms(1);                 // Initialization sequence step 2
    LCD_Nibble(0x03); delay_ms(1);                 // Initialization sequence step 3
    LCD_Nibble(0x02); delay_ms(1);                 // Switch to 4-bit mode
    LCD_Command(0x28);                             // Function set: 4-bit, 2-line, 5x8 font
    LCD_Command(0x0C);                             // Display ON, cursor OFF
    LCD_Command(0x06);                             // Entry mode: increment cursor after write
    LCD_Clear();                                   // Clear LCD after init
}

// ============================================================
// Integer-to-string helper for LCD display
// Produces fixed-width right-aligned numbers.
// ============================================================
void uint32_to_str(uint32_t val, char *buf, uint8_t width) {
    buf[width] = '\0';                             // Null-terminate string
    for (int8_t i = width - 1; i >= 0; i--) {      // Fill digits from right to left
        buf[i] = '0' + (val % 10);                 // Extract least significant digit
        val /= 10;                                 // Remove last digit
        if (val == 0) {                            // If all digits used up
            for (int8_t j = i - 1; j >= 0; j--) buf[j] = ' '; // Fill left side with spaces
            break;
        }
    }
}

// ============================================================
// Debounced button press reader
// Buttons are active-low because pull-up resistors are used.
// ============================================================
uint8_t button_pressed(volatile uint32_t *port_reg, uint8_t pin_mask) {
    if (((*port_reg) & pin_mask) == 0) {           // If pin reads low, button is pressed
        vTaskDelay(pdMS_TO_TICKS(20));             // Debounce delay to ignore bouncing
        if (((*port_reg) & pin_mask) == 0) return 1; // Recheck using live register value
    }
    return 0;                                      // No valid button press detected
}

// ============================================================
// RGB LED helper
// PF1=red, PF2=blue, PF3=green
// ============================================================
void led_set(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t val = GPIO_PORTF_DATA_R & ~((1<<1)|(1<<2)|(1<<3)); // Clear existing LED bits
    if (red)   val |= (1<<1);                      // Turn red on if requested
    if (blue)  val |= (1<<2);                      // Turn blue on if requested
    if (green) val |= (1<<3);                      // Turn green on if requested
    GPIO_PORTF_DATA_R = val;                       // Write final LED state
}

// ============================================================
// Encoder ISR
// Every rising edge on PD0 increments encoder_count.
// ============================================================
void GPIOPortD_Handler(void) {
    if (GPIO_PORTD_MIS_R & (1 << 0)) {             // Check if PD0 caused the interrupt
        encoder_count++;                           // Count one encoder pulse
        GPIO_PORTD_ICR_R |= (1 << 0);              // Clear interrupt flag
    }
}

// ============================================================
// Encoder Task
// Runs every 100 ms:
//  - Copies and clears encoder_count safely
//  - Converts pulse count into RPM
//  - Publishes RPM to shared variable
// IMPORTANT: In this unprotected version:
//  - NO zero-RPM detection
//  - If RPM becomes zero, system accepts it silently
// ============================================================
void vEncoderTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Store initial tick for periodic scheduling

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SAMPLE_PERIOD_MS)); // Run every 100 ms exactly

        uint32_t count;
        taskENTER_CRITICAL();                       // Prevent race while copying shared pulse counter
        count = encoder_count;                      // Snapshot encoder pulses collected by ISR
        encoder_count = 0;                          // Reset pulse count for next sampling window
        taskEXIT_CRITICAL();                        // End critical section

        uint32_t rpm = (count * 60000UL) / (ENCODER_PPR * SAMPLE_PERIOD_MS); // Convert pulse count to RPM

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(10)) == pdTRUE) { // Lock RPM shared variable
            current_rpm = rpm;                      // Publish new measured RPM
            xSemaphoreGive(xRPMMutex);              // Unlock RPM mutex
        }
        // No zero-RPM safety logic here
    }
}

// ============================================================
// Cruise Task
// Runs every 10 ms and implements bang-bang control:
//  - If RPM < target-deadband -> throttle ON
//  - If RPM > target+deadband -> throttle OFF
// IMPORTANT: In this unprotected version:
//  - No watchdog monitoring
//  - No safety shutdown if RPM is zero
// ============================================================
void vCruiseTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Save starting tick for periodic scheduling

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10)); // Run every 10 ms

        CruiseState state;
        uint32_t    rpm;
        uint32_t    target;

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Safely read current state and target
            state  = cruise_state;                  // Snapshot cruise mode
            target = target_rpm;                    // Snapshot target RPM
            xSemaphoreGive(xStateMutex);            // Release state mutex
        } else { continue; }                        // Skip this cycle if mutex unavailable

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Safely read measured RPM
            rpm = current_rpm;                      // Snapshot RPM
            xSemaphoreGive(xRPMMutex);              // Release RPM mutex
        } else { continue; }                        // Skip this cycle if mutex unavailable

        if (state == STATE_ACTIVE) {               // Only run automatic control if cruise is active
            if (rpm < (target - DEADBAND)) {       // Speed is below desired lower threshold
                PWM_SetDuty(THROTTLE_ON);           // Apply throttle to motor driver
                led_set(0, 0, 1);                  // Blue LED = accelerating / below target
            } else if (rpm > (target + DEADBAND)) {// Speed is above desired upper threshold
                PWM_SetDuty(THROTTLE_OFF);          // Cut throttle
                led_set(0, 1, 0);                  // Green LED = above target
            }
        }
    }
}

// ============================================================
// Manual Task
// Runs every 20 ms and handles:
//  - PE1: increase target RPM
//  - PE0: decrease target RPM
//  - PF4: activate cruise
//  - PF0: cancel cruise
//  - Potentiometer controls throttle directly when cruise is OFF
// Note: This version still keeps button debounce and bounded
// button-release waits, but has NO watchdog protection.
// ============================================================
void vManualTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Save starting tick for periodic scheduling

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20)); // Run every 20 ms

        CruiseState state;
        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Safely read cruise state
            state = cruise_state;                   // Snapshot current mode
            xSemaphoreGive(xStateMutex);            // Release state mutex
        } else { continue; }                        // Skip cycle if mutex unavailable

        if (button_pressed(&GPIO_PORTE_DATA_R, (1 << 1))) { // If PE1 pressed -> increase target RPM
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                if (target_rpm <= SPEED_MAX - SPEED_STEP) target_rpm += SPEED_STEP; // Increase if still below max
                else target_rpm = SPEED_MAX;         // Otherwise clamp to max
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            uint32_t t = 0;                          // Release wait timeout counter
            while (!(GPIO_PORTE_DATA_R & (1 << 1)) && t++ < BUTTON_RELEASE_TIMEOUT) // Wait until release or timeout
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield 1 ms each loop
        }

        if (button_pressed(&GPIO_PORTE_DATA_R, (1 << 0))) { // If PE0 pressed -> decrease target RPM
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                if (target_rpm >= SPEED_MIN + SPEED_STEP) target_rpm -= SPEED_STEP; // Decrease if above min
                else target_rpm = SPEED_MIN;         // Otherwise clamp to min
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            uint32_t t = 0;                          // Release wait timeout counter
            while (!(GPIO_PORTE_DATA_R & (1 << 0)) && t++ < BUTTON_RELEASE_TIMEOUT) // Wait until release or timeout
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield during wait
        }

        if (button_pressed(&GPIO_PORTF_DATA_R, (1 << 4))) { // If PF4 pressed -> enable cruise mode
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                cruise_state = STATE_ACTIVE;         // Turn cruise on
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            led_set(0, 1, 0);                        // Green LED to indicate cruise enabled
            uint32_t t = 0;                          // Release wait timeout counter
            while (!(GPIO_PORTF_DATA_R & (1 << 4)) && t++ < BUTTON_RELEASE_TIMEOUT) // Wait for button release or timeout
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield during wait
        }

        if (button_pressed(&GPIO_PORTF_DATA_R, (1 << 0))) { // If PF0 pressed -> disable cruise mode
            if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) { // Lock shared state
                cruise_state = STATE_OFF;            // Turn cruise off
                xSemaphoreGive(xStateMutex);         // Release mutex
            }
            PWM_SetDuty(THROTTLE_OFF);               // Immediately cut motor throttle
            led_set(1, 0, 0);                        // Red LED indicates OFF state
            uint32_t t = 0;                          // Release wait timeout counter
            while (!(GPIO_PORTF_DATA_R & (1 << 0)) && t++ < BUTTON_RELEASE_TIMEOUT) // Wait until release or timeout
                vTaskDelay(pdMS_TO_TICKS(1));        // Yield during wait
        }

        if (state == STATE_OFF) {                    // Manual throttle only works when cruise is off
            PWM_SetDuty(ADC_ReadThrottle());         // Use potentiometer reading to directly set motor power
        }
    }
}

// ============================================================
// LCD Task
// Runs every 200 ms and updates the display with:
//  - Current RPM on row 1
//  - Target RPM and cruise state on row 2
// ============================================================
void vLCDTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount(); // Save starting tick for periodic scheduling
    char num_buf[5];                                // 4 digits + null terminator

    LCD_SetCursor(0, 0); LCD_String(" Speed:");    // Static label on first line
    LCD_SetCursor(1, 0); LCD_String("Target:");    // Static label on second line

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200)); // Refresh LCD every 200 ms

        uint32_t    rpm;
        uint32_t    target;
        CruiseState state;

        if (xSemaphoreTake(xRPMMutex, pdMS_TO_TICKS(10)) == pdTRUE) { // Read current RPM safely
            rpm = current_rpm;                      // Snapshot measured RPM
            xSemaphoreGive(xRPMMutex);              // Release RPM mutex
        } else { continue; }                        // Skip update if mutex unavailable

        if (xSemaphoreTake(xStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) { // Read target/state safely
            target = target_rpm;                    // Snapshot target RPM
            state  = cruise_state;                  // Snapshot cruise state
            xSemaphoreGive(xStateMutex);            // Release state mutex
        } else { continue; }                        // Skip update if mutex unavailable

        uint32_t kph = (rpm * WHEEL_CIRCUMFERENCE * WHEEL_DIAMETER_M) / 11;

        LCD_SetCursor(0, 7);                        // Move after " Speed:"
        uint32_to_str(kph, num_buf, 4);             // Format RPM number
        LCD_String(num_buf);                        // Print RPM value
        LCD_String(" KPH");                        // Print RPM label

        LCD_SetCursor(1, 7);                        // Move after "Target:"
        uint32_to_str(target, num_buf, 4);          // Format target RPM
        LCD_String(num_buf);                        // Print target RPM
        LCD_String(state == STATE_ACTIVE ? " ACT" : " OFF"); // Print mode text
    }
}

// ============================================================
// FreeRTOS stack overflow hook
// Called if a task overflows its stack.
// ============================================================
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;                                    // Avoid unused parameter warning
    (void)pcTaskName;                               // Avoid unused parameter warning
    led_set(1, 0, 0);                               // Red LED = fatal fault
    while (1);                                      // Halt forever for debugging
}

// ============================================================
// FreeRTOS malloc failure hook
// Called if heap allocation fails.
// ============================================================
void vApplicationMallocFailedHook(void) {
    led_set(1, 0, 1);                               // Purple-ish LED = heap failure indicator
    while (1);                                      // Halt forever for debugging
}

// ============================================================
// Main function
// Initializes peripherals, creates mutexes and tasks, then
// starts FreeRTOS scheduler.
// Note: This version intentionally has NO watchdog task and
// NO zero-RPM protection logic.
// ============================================================
int main(void) {
    GPIO_Init();                                    // Initialize GPIO pins
    ADC_Init();                                     // Initialize ADC for throttle potentiometer
    PWM_Init();                                     // Initialize PWM for motor control
    LCD_Init();                                     // Initialize LCD before scheduler starts

    led_set(1, 0, 0);                               // Red LED at startup = system initially OFF

    xRPMMutex   = xSemaphoreCreateMutex();          // Create mutex for RPM shared variable
    xStateMutex = xSemaphoreCreateMutex();          // Create mutex for state shared variables

    configASSERT(xRPMMutex   != NULL);              // Catch RPM mutex creation failure in debug
    configASSERT(xStateMutex != NULL);              // Catch state mutex creation failure in debug

    configASSERT(xTaskCreate(vEncoderTask, "Encoder", 256, NULL, 3, NULL) == pdPASS); // Create encoder task
    configASSERT(xTaskCreate(vCruiseTask,  "Cruise",  256, NULL, 2, NULL) == pdPASS); // Create cruise task
    configASSERT(xTaskCreate(vManualTask,  "Manual",  256, NULL, 1, NULL) == pdPASS); // Create manual input task
    configASSERT(xTaskCreate(vLCDTask,     "LCD",     384, NULL, 1, NULL) == pdPASS); // Create LCD display task

    vTaskStartScheduler();                          // Start FreeRTOS scheduler; tasks begin running
    while (1);                                      // Should never get here unless scheduler fails
}