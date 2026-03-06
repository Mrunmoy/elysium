// STM32F4 RTC register-level implementation.
// Reference: RM0090 Section 26 (Real-Time Clock)

#include "hal/Rtc.h"

#include <cstdint>

namespace
{
    // ---- Register base addresses ----
    constexpr std::uint32_t kRtcBase = 0x40002800;
    constexpr std::uint32_t kPwrBase = 0x40007000;
    constexpr std::uint32_t kRccBase = 0x40023800;
    constexpr std::uint32_t kExtiBase = 0x40013C00;

    // RTC registers
    volatile std::uint32_t &rtcTr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRtcBase + 0x00);
    }
    volatile std::uint32_t &rtcDr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRtcBase + 0x04);
    }
    volatile std::uint32_t &rtcCr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRtcBase + 0x08);
    }
    volatile std::uint32_t &rtcIsr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRtcBase + 0x0C);
    }
    volatile std::uint32_t &rtcPrer()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRtcBase + 0x10);
    }
    volatile std::uint32_t &rtcAlrmar()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRtcBase + 0x1C);
    }
    volatile std::uint32_t &rtcWpr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRtcBase + 0x24);
    }

    // PWR registers
    volatile std::uint32_t &pwrCr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kPwrBase + 0x00);
    }

    // RCC registers
    volatile std::uint32_t &rccApb1Enr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRccBase + 0x40);
    }
    volatile std::uint32_t &rccBdcr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRccBase + 0x70);
    }
    volatile std::uint32_t &rccCsr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kRccBase + 0x74);
    }

    // EXTI registers
    volatile std::uint32_t &extiImr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kExtiBase + 0x00);
    }
    volatile std::uint32_t &extiRtsr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kExtiBase + 0x08);
    }
    volatile std::uint32_t &extiPr()
    {
        return *reinterpret_cast<volatile std::uint32_t *>(kExtiBase + 0x14);
    }

    // NVIC registers
    volatile std::uint32_t &nvicIser(std::uint32_t n)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(0xE000E100 + 4 * n);
    }
    volatile std::uint32_t &nvicIcer(std::uint32_t n)
    {
        return *reinterpret_cast<volatile std::uint32_t *>(0xE000E180 + 4 * n);
    }

    constexpr std::uint32_t kRtcAlarmIrqn = 41;

    // BCD helpers
    std::uint8_t toBcd(std::uint8_t val)
    {
        return static_cast<std::uint8_t>(((val / 10) << 4) | (val % 10));
    }

    std::uint8_t fromBcd(std::uint8_t bcd)
    {
        return static_cast<std::uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F));
    }

    void unlockWpr()
    {
        rtcWpr() = 0xCA;
        rtcWpr() = 0x53;
    }

    void lockWpr()
    {
        rtcWpr() = 0xFF;
    }

    void enterInitMode()
    {
        rtcIsr() |= (1u << 7);  // INIT bit
        while ((rtcIsr() & (1u << 6)) == 0)
        {
            // Wait for INITF
        }
    }

    void exitInitMode()
    {
        rtcIsr() &= ~(1u << 7);  // Clear INIT
    }

    // Alarm callback state
    hal::RtcAlarmCallbackFn g_alarmCallback = nullptr;
    void *g_alarmArg = nullptr;

}  // namespace

namespace hal
{
    void rtcInit(RtcClockSource clockSource)
    {
        // 1. Enable PWR clock
        rccApb1Enr() |= (1u << 28);

        // 2. Enable backup domain access
        pwrCr() |= (1u << 8);  // DBP

        if (clockSource == RtcClockSource::Lsi)
        {
            // Enable LSI via RCC_CSR
            rccCsr() |= (1u << 0);  // LSION
            while ((rccCsr() & (1u << 1)) == 0)
            {
                // Wait for LSIRDY
            }

            // Select LSI as RTC clock source (RTCSEL = 10)
            std::uint32_t bdcr = rccBdcr();
            bdcr &= ~(3u << 8);    // Clear RTCSEL
            bdcr |= (2u << 8);     // RTCSEL = 10 (LSI)
            bdcr |= (1u << 15);    // RTCEN
            rccBdcr() = bdcr;
        }
        else
        {
            // Enable LSE via RCC_BDCR
            rccBdcr() |= (1u << 0);  // LSEON
            while ((rccBdcr() & (1u << 1)) == 0)
            {
                // Wait for LSERDY
            }

            // Select LSE as RTC clock source (RTCSEL = 01)
            std::uint32_t bdcr = rccBdcr();
            bdcr &= ~(3u << 8);    // Clear RTCSEL
            bdcr |= (1u << 8);     // RTCSEL = 01 (LSE)
            bdcr |= (1u << 15);    // RTCEN
            rccBdcr() = bdcr;
        }

        // Configure prescaler for 1 Hz
        unlockWpr();
        enterInitMode();

        if (clockSource == RtcClockSource::Lsi)
        {
            // LSI ~32 kHz: PREDIV_A=127, PREDIV_S=249 -> 32000 / (128*250) = 1 Hz
            rtcPrer() = (127u << 16) | 249u;
        }
        else
        {
            // LSE 32768 Hz: PREDIV_A=127, PREDIV_S=255 -> 32768 / (128*256) = 1 Hz
            rtcPrer() = (127u << 16) | 255u;
        }

        // 24-hour format (FMT = 0)
        rtcCr() &= ~(1u << 6);

        exitInitMode();
        lockWpr();
    }

    void rtcSetTime(const RtcTime &time)
    {
        unlockWpr();
        enterInitMode();

        std::uint32_t tr = 0;
        tr |= (static_cast<std::uint32_t>(toBcd(time.hours)) << 16);
        tr |= (static_cast<std::uint32_t>(toBcd(time.minutes)) << 8);
        tr |= (static_cast<std::uint32_t>(toBcd(time.seconds)));
        rtcTr() = tr;

        exitInitMode();
        lockWpr();
    }

    void rtcGetTime(RtcTime &time)
    {
        // Read TR twice and compare to handle BCD shadow update
        std::uint32_t tr1 = rtcTr();
        std::uint32_t tr2 = rtcTr();
        if (tr1 != tr2)
        {
            tr1 = rtcTr();
        }

        time.hours = fromBcd(static_cast<std::uint8_t>((tr1 >> 16) & 0x3F));
        time.minutes = fromBcd(static_cast<std::uint8_t>((tr1 >> 8) & 0x7F));
        time.seconds = fromBcd(static_cast<std::uint8_t>(tr1 & 0x7F));
    }

    void rtcSetDate(const RtcDate &date)
    {
        unlockWpr();
        enterInitMode();

        std::uint32_t dr = 0;
        dr |= (static_cast<std::uint32_t>(toBcd(date.year)) << 16);
        dr |= (static_cast<std::uint32_t>(date.weekday & 0x07) << 13);
        dr |= (static_cast<std::uint32_t>(toBcd(date.month)) << 8);
        dr |= (static_cast<std::uint32_t>(toBcd(date.day)));
        rtcDr() = dr;

        exitInitMode();
        lockWpr();
    }

    void rtcGetDate(RtcDate &date)
    {
        std::uint32_t dr1 = rtcDr();
        std::uint32_t dr2 = rtcDr();
        if (dr1 != dr2)
        {
            dr1 = rtcDr();
        }

        date.year = fromBcd(static_cast<std::uint8_t>((dr1 >> 16) & 0xFF));
        date.weekday = static_cast<std::uint8_t>((dr1 >> 13) & 0x07);
        date.month = fromBcd(static_cast<std::uint8_t>((dr1 >> 8) & 0x1F));
        date.day = fromBcd(static_cast<std::uint8_t>(dr1 & 0x3F));
    }

    void rtcSetAlarm(const RtcAlarmConfig &config, RtcAlarmCallbackFn callback, void *arg)
    {
        unlockWpr();

        // Disable Alarm A first (ALRAE in CR bit 8)
        rtcCr() &= ~(1u << 8);

        // Wait for ALRAWF (ISR bit 0)
        while ((rtcIsr() & (1u << 0)) == 0)
        {
        }

        // Build ALRMAR register
        std::uint32_t alrm = 0;
        alrm |= static_cast<std::uint32_t>(toBcd(config.seconds));
        alrm |= static_cast<std::uint32_t>(toBcd(config.minutes)) << 8;
        alrm |= static_cast<std::uint32_t>(toBcd(config.hours)) << 16;

        if (config.maskSeconds)
        {
            alrm |= (1u << 7);   // MSK1
        }
        if (config.maskMinutes)
        {
            alrm |= (1u << 15);  // MSK2
        }
        if (config.maskHours)
        {
            alrm |= (1u << 23);  // MSK3
        }
        if (config.maskDate)
        {
            alrm |= (1u << 31);  // MSK4
        }

        rtcAlrmar() = alrm;

        // Store callback
        g_alarmCallback = callback;
        g_alarmArg = arg;

        // Enable Alarm A interrupt (ALRAIE in CR bit 12)
        rtcCr() |= (1u << 12);

        // Enable Alarm A (ALRAE in CR bit 8)
        rtcCr() |= (1u << 8);

        lockWpr();

        // Configure EXTI line 17 for RTC Alarm
        extiImr() |= (1u << 17);
        extiRtsr() |= (1u << 17);

        // Enable NVIC IRQ 41 (RTC Alarm)
        nvicIser(kRtcAlarmIrqn / 32) = (1u << (kRtcAlarmIrqn % 32));
    }

    void rtcCancelAlarm()
    {
        unlockWpr();

        // Disable Alarm A (ALRAE in CR bit 8)
        rtcCr() &= ~(1u << 8);

        // Disable Alarm A interrupt (ALRAIE in CR bit 12)
        rtcCr() &= ~(1u << 12);

        lockWpr();

        // Disable NVIC IRQ 41
        nvicIcer(kRtcAlarmIrqn / 32) = (1u << (kRtcAlarmIrqn % 32));

        // Mask EXTI line 17
        extiImr() &= ~(1u << 17);

        g_alarmCallback = nullptr;
        g_alarmArg = nullptr;
    }

    bool rtcIsReady()
    {
        // Check RSF (Register Synchronization Flag) in ISR bit 5
        return (rtcIsr() & (1u << 5)) != 0;
    }

}  // namespace hal

extern "C" void RTC_Alarm_IRQHandler()
{
    // Check Alarm A flag (ALRAF in ISR bit 8)
    if (rtcIsr() & (1u << 8))
    {
        // Clear ALRAF (write 0 to bit 8, other bits preserved)
        rtcIsr() &= ~(1u << 8);

        // Clear EXTI line 17 pending
        extiPr() = (1u << 17);

        if (g_alarmCallback != nullptr)
        {
            g_alarmCallback(g_alarmArg);
        }
    }
}
