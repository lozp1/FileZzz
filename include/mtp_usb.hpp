#pragma once
#include <switch.h>
#include <string>
#include <cstdio>
#include "usb_mtp.h"

#include <mutex>
#include <vector>
#include "sys_clock.hpp"

namespace mtp_usb {

inline std::mutex g_logMutex;
inline std::vector<std::string> g_logHistory = {
    "[MTP] Servicio iniciado y listo para recibir conexiones USB."
};

inline void logEvent(const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logHistory.push_back(msg);
    if (g_logHistory.size() > 50) {
        g_logHistory.erase(g_logHistory.begin());
    }
}

inline std::vector<std::string> getLogs() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    return g_logHistory;
}

inline void (*g_logCb)(const char* msg) = nullptr;
inline void mlog(const std::string& s) {
    logEvent(s);
    if (g_logCb) g_logCb(s.c_str());
}

inline bool g_ready = false;

inline void teardown() {
    sys_clock::disableBoost();
    usbMtpExit();
    g_ready = false;
}

inline bool setup() {
    teardown();
    sys_clock::enableBoost();
    Result rc = usbMtpInitialize();
    if (R_FAILED(rc)) {
        char b[32];
        snprintf(b, sizeof(b), "rc=0x%X", rc);
        mlog(std::string("USB MTP Init ERR ") + b);
        return false;
    }
    g_ready = true;
    mlog("USB MTP: Conexión USB preparada correctamente.");
    mlog("USB MTP: Listo para transferir archivos en tu PC.");
    return true;
}

inline void poll() {
}

} // namespace mtp_usb
