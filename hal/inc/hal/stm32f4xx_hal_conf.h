// HAL configuration for ms-os / STM32F407VGT6
// Minimal module set for Phase 0.

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

// Module selection (only what we need)
#define HAL_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED

// HSE: 25 MHz external crystal on STM32F407 Discovery / common boards
#if !defined(HSE_VALUE)
    #define HSE_VALUE 25000000U
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
    #define HSE_STARTUP_TIMEOUT 100U
#endif

// HSI: 16 MHz internal RC
#if !defined(HSI_VALUE)
    #define HSI_VALUE 16000000U
#endif

// LSI / LSE
#if !defined(LSI_VALUE)
    #define LSI_VALUE 32000U
#endif

#if !defined(LSE_VALUE)
    #define LSE_VALUE 32768U
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
    #define LSE_STARTUP_TIMEOUT 5000U
#endif

#if !defined(EXTERNAL_CLOCK_VALUE)
    #define EXTERNAL_CLOCK_VALUE 12288000U
#endif

// System configuration
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 0x0FU
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE 1U

// SPI CRC (required by HAL even if SPI is not enabled)
#define USE_SPI_CRC 0U

// Include enabled modules
#ifdef HAL_RCC_MODULE_ENABLED
    #include "stm32f4xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
    #include "stm32f4xx_hal_gpio.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
    #include "stm32f4xx_hal_dma.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
    #include "stm32f4xx_hal_cortex.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
    #include "stm32f4xx_hal_flash.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
    #include "stm32f4xx_hal_pwr.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
    #include "stm32f4xx_hal_uart.h"
#endif

#ifdef __cplusplus
}
#endif
