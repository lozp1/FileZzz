#pragma once
#include <switch.h>
#include <string>
#include <cstdio>
#include "usb_mtp.h"

namespace mtp_usb {

inline void (*g_logCb)(const char* msg) = nullptr;
inline void mlog(const std::string& s) {
    if (g_logCb) g_logCb(s.c_str());
}

inline bool g_ready = false;

inline void teardown() {
    usbMtpExit();
    g_ready = false;
}

inline bool setup() {
    teardown();
    Result rc = usbMtpInitialize();
    if (R_FAILED(rc)) {
        char b[32];
        snprintf(b, sizeof(b), "rc=0x%X", rc);
        mlog(std::string("USB MTP Init ERR ") + b);
        return false;
    }
    g_ready = true;
    mlog("USB MTP: Inicializado OK (0x0955:0x7321)");
    mlog("USB MTP: Listo para transferencias en PC");
    return true;
}

inline void poll() {
}

} // namespace mtp_usb
