#pragma once

#if (UI_LED_ENABLE)
#define	GPIO_LED_RED			GPIO_PC6
#define	GPIO_LED_GREEN			GPIO_PC4
#define	GPIO_LED_BLUE			GPIO_PC5

#define PC4_FUNC				AS_GPIO
#define PC5_FUNC				AS_GPIO
#define PC6_FUNC				AS_GPIO

#define PULL_WAKEUP_SRC_PC4 PM_PIN_PULLUP_1M
#define PULL_WAKEUP_SRC_PC5 PM_PIN_PULLUP_1M
#define PULL_WAKEUP_SRC_PC6 PM_PIN_PULLUP_1M

#define	PC4_OUTPUT_ENABLE		1
#define	PC5_OUTPUT_ENABLE		1
#define	PC6_OUTPUT_ENABLE		1

#define LED_ON_LEVEL 0 // active low
#endif


/**
 *  @brief  Battery_check Configuration
 */
//Telink device: you must choose one gpio with adc function to output high level(voltage will equal to vbat),
//then use adc to measure high level voltage
//use PB7 output high level, then adc measure this high level voltage
#define GPIO_VBAT_DETECT				GPIO_PB7
#define PB7_FUNC						AS_GPIO
#define PB7_INPUT_ENABLE				0
#define ADC_INPUT_PCHN					B7P    //corresponding  ADC_InputPchTypeDef in adc.h

// Blozi 213 panel pinout. Other panels would need a separate board header.
#define EPD_RESET GPIO_PB6
#define EPD_DC GPIO_PB5
#define EPD_BUSY GPIO_PB7
#define EPD_CS GPIO_PB4
#define EPD_CLK GPIO_PA1
#define EPD_MOSI GPIO_PA0