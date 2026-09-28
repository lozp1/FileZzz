#pragma once
#ifdef __SWITCH__
#include <switch.h>
#endif
#include <borealis.hpp>

namespace sys_clock {

inline bool g_boost_active = false;

inline void enableBoost() {
#ifdef __SWITCH__
    if (!g_boost_active) {
        Result rc = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
        if (R_SUCCEEDED(rc)) {
            g_boost_active = true;
            brls::Logger::info("sys_clock: CPU boost enabled (1785 MHz / FastLoad)");
        } else {
            brls::Logger::warning("sys_clock: failed to set CPU boost: 0x{:08X}", rc);
        }
    }
#endif
}

inline void disableBoost() {
#ifdef __SWITCH__
    if (g_boost_active) {
        appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
        g_boost_active = false;
        brls::Logger::info("sys_clock: CPU boost disabled (normal mode)");
    }
#endif
}

inline bool isBoostActive() {
    return g_boost_active;
}

} // namespace sys_clock
