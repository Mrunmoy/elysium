# Development Tasks

## Completed

- [x] Global error codes (`common/inc/msos/ErrorCode.h`, kernel/HAL/test wiring, status APIs)
- [x] Hardware driver validation framework (uart/spi/i2c/dma smoke tests, host runner, negative paths)
- [x] DMA hardware verification (3/3 PASS, mem-to-mem on STM32F407)
- [x] DMA-driven SPI transfers (`spiTransferDma`) with host tests and board-to-board hardware validation

## TODO: Peripheral Drivers

### DMA + Peripheral Integration
- [x] DMA-driven SPI transfers (`spiTransferDma`)
- [ ] DMA-driven UART TX/RX
- [ ] DMA-driven ADC continuous conversion
- [ ] Host tests
- [ ] Hardware tests

### Timers (TIM) -- DONE
- [x] Design doc (`docs/design/phase-18-timers.md`)
- [x] HAL API: `timerInit`, `timerStart`, `timerStop`, `timerSetPeriod`, `timerSetPrescaler`, PWM
- [x] Basic timer (TIM6/TIM7) -- periodic interrupt, microsecond delay
- [x] General-purpose timer (TIM2-TIM5) -- PWM output (input capture deferred)
- [x] 32 host tests with link-time mocks
- [x] Hardware test on STM32F407 (5/5 PASS + Saleae logic analyzer verification)
- [x] Zynq stub

### RNG (Hardware Random Number Generator) -- DONE
- [x] Design doc (`docs/design/phase-rng.md`)
- [x] HAL API: `rngInit`, `rngRead`, `rngDeinit`
- [x] STM32F4 register-level driver (RNG at 0x50060800, RCC AHB2ENR bit 6)
- [x] RCC: `rccEnableRngClock`, `rccDisableRngClock`
- [x] Zynq stub (returns `kNoSys`)
- [x] 11 host tests with link-time mocks
- [x] Hardware test on STM32F407 (4/4 PASS, rng-demo app)

### RTC (Real-Time Clock) -- DONE
- [x] HAL API: `rtcInit`, `rtcGetTime`, `rtcSetTime`, `rtcSetAlarm`, `rtcSetDate`, `rtcGetDate`, `rtcCancelAlarm`, `rtcIsReady`
- [x] STM32F4 register-level driver (LSI clock source, BCD encoding, alarm A via EXTI line 17)
- [x] 18 host tests with link-time mocks
- [x] Hardware test on STM32F407 (5/5 PASS)
- [x] Zynq stub

### CRC (Hardware CRC Unit)
- [ ] HAL API: `crcInit`, `crcCompute`
- [ ] STM32F4 register-level driver
- [ ] Host tests
- [ ] Hardware test (compare against known CRC32 values)

### ADC
- [ ] Design doc
- [ ] HAL API: `adcInit`, `adcRead` (single-shot), `adcStartContinuous` (with DMA)
- [ ] STM32F4 ADC1/2/3 register-level driver
- [ ] Host tests
- [ ] Hardware test (read known voltage or internal temp sensor)
- [ ] Zynq stub

### DAC
- [ ] HAL API: `dacInit`, `dacWrite`, `dacStartDma`
- [ ] STM32F4 DAC1/2 register-level driver (PA4/PA5)
- [ ] Host tests
- [ ] Hardware test (output known voltage, verify with ADC loopback)

### USB (OTG FS/HS)
- [ ] Design doc
- [ ] HAL API: USB device core (endpoint config, control transfers)
- [ ] CDC ACM class (virtual COM port) as first use case
- [ ] STM32F4 OTG_FS register-level driver
- [ ] Host tests
- [ ] Hardware test (enumerate as CDC device on host PC)
- [ ] Zynq stub

### CAN (bxCAN)
- [ ] Design doc
- [ ] HAL API: `canInit`, `canSend`, `canReceive`, filter configuration
- [ ] STM32F4 CAN1/CAN2 register-level driver
- [ ] Host tests
- [ ] Hardware test (loopback mode, or two-board CAN bus if transceiver available)

### SDIO/SDMMC
- [ ] HAL API: `sdioInit`, `sdioReadBlock`, `sdioWriteBlock`
- [ ] STM32F4 SDIO register-level driver (4-bit bus)
- [ ] Host tests
- [ ] Hardware test (read/write SD card sectors)

### Ethernet MAC
- [ ] Design doc
- [ ] HAL API: `ethInit`, `ethSend`, `ethReceive`, PHY management (MDIO/MDC)
- [ ] STM32F4 ETH register-level driver (MII/RMII)
- [ ] DMA-driven TX/RX with descriptor rings
- [ ] PHY driver (DP83848 or LAN8720 depending on board)
- [ ] Host tests
- [ ] Hardware test (link up, raw frame loopback or ping)
- [ ] Zynq GEM stub

## TODO: Kernel / OS Features

### Timers (Software / Kernel)
- [ ] One-shot and periodic kernel timers (callback-based)
- [ ] Timer wheel or sorted list implementation
- [ ] Integration with SysTick

### User-Space Drivers
- [ ] Move SPI/I2C/UART drivers out of kernel into user-space services
- [ ] IPC-based driver access from unprivileged threads
- [ ] Design doc for driver service model

### File System
- [ ] Design doc
- [ ] FAT16/32 or minimal flash FS
- [ ] SDIO/SDMMC HAL driver (for SD card)
- [ ] Block device abstraction

### Networking
- [ ] Design doc
- [ ] Ethernet MAC HAL driver (STM32F2/F4)
- [ ] Lightweight TCP/IP stack (lwIP integration or minimal custom)
- [ ] DMA-driven Ethernet

### Cortex-A9 Parity
- [ ] SVC/unprivileged mode on PYNQ-Z2
- [ ] MMU configuration (replace MPU stub)

## TODO: Infrastructure

- [ ] Nightly hardware test stage (CI or local scheduled script)
- [ ] Code coverage reporting for host tests
- [ ] Static analysis integration (clang-tidy or cppcheck)
