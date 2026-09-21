#pragma once
// Respondedor MTP experimental: bucle de operaciones sobre bulk USB.
//
// Cubre el subconjunto que usa el Explorador de Windows para navegar y
// transferir: GetDeviceInfo, OpenSession/CloseSession, GetStorageIDs/Info,
// GetNumObjects, GetObjectHandles/Info, GetObject, SendObjectInfo/SendObject,
// DeleteObject. Lo demás responde OperationNotSupported (los hosts lo toleran).
//
// Experimental: activar solo desde la pantalla MTP ([A]). Todo paso queda en
// la bitácora. Parada limpia: flag + Cancel en endpoints + join.
#include <switch.h>
#include "mtp_usb.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <unistd.h>

namespace mtp_ops {

// --- Opcodes / respuestas ---
enum {
    OP_GetDeviceInfo = 0x1001, OP_OpenSession = 0x1002, OP_CloseSession = 0x1003,
    OP_GetStorageIDs = 0x1004, OP_GetStorageInfo = 0x1005, OP_GetNumObjects = 0x1006,
    OP_GetObjectHandles = 0x1007, OP_GetObjectInfo = 0x1008, OP_GetObject = 0x1009,
    OP_DeleteObject = 0x100B, OP_SendObjectInfo = 0x100C, OP_SendObject = 0x100D,
    OP_GetDevicePropDesc = 0x1014,
};
enum { MR_OK = 0x2001, MR_GeneralError = 0x2002, MR_NotSupported = 0x2005, MR_InvalidStorage = 0x2009 };
enum { T_Operation = 1, T_Data = 2, T_Response = 3 };
enum { F_Undefined = 0x3000, F_Association = 0x3001 };

inline std::atomic<bool> g_run(false);
inline std::atomic<bool> g_active(false);
inline std::thread g_thr;

inline UsbDsEndpoint* g_epIn = nullptr;
inline UsbDsEndpoint* g_epOut = nullptr;

// --- LE helpers ---
inline void put16(std::vector<u8>& v, u16 x) { v.push_back((u8)x); v.push_back((u8)(x >> 8)); }
inline void put32(std::vector<u8>& v, u32 x) { for (int i = 0; i < 4; i++) v.push_back((u8)(x >> (8 * i))); }
inline void put64(std::vector<u8>& v, u64 x) { for (int i = 0; i < 8; i++) v.push_back((u8)(x >> (8 * i))); }
inline u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8* p) { return (u32)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24)); }

// UTF-8 -> UTF-16LE (con pares subrogados; inválido -> U+FFFD)
inline void putUtf16(std::vector<u8>& v, const char* s) {
    std::vector<u16> u;
    for (const u8* p = (const u8*)s; *p;) {
        u32 cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0) { cp = (*p & 0x1F); p++; if ((*p & 0xC0) != 0x80) { cp = 0xFFFD; } else { cp = (cp << 6) | (*p & 0x3F); p++; } }
        else if ((*p & 0xF0) == 0xE0) { cp = (*p & 0x0F); p++; for (int i = 0; i < 2; i++) { if ((*p & 0xC0) != 0x80) { cp = 0xFFFD; break; } cp = (cp << 6) | (*p & 0x3F); p++; } }
        else if ((*p & 0xF8) == 0xF0) { cp = (*p & 0x07); p++; for (int i = 0; i < 3; i++) { if ((*p & 0xC0) != 0x80) { cp = 0xFFFD; break; } cp = (cp << 6) | (*p & 0x3F); p++; } }
        else { cp = 0xFFFD; p++; }
        if (cp < 0x10000) u.push_back((u16)cp);
        else { cp -= 0x10000; u.push_back((u16)(0xD800 + (cp >> 10))); u.push_back((u16)(0xDC00 + (cp & 0x3FF))); }
    }
    v.push_back((u8)(u.size() + 1)); // longitud con NUL
    for (u16 c : u) put16(v, c);
    put16(v, 0);
}
inline void putStr(std::vector<u8>& v, const char* s) { putUtf16(v, s ? s : ""); }
inline void putEmptyStr(std::vector<u8>& v) { v.push_back(0); }

// UTF-16LE (MTP string) -> UTF-8, saneando separadores de ruta
inline std::string getMtpStr(const u8* p, size_t maxBytes) {
    if (maxBytes < 1 || p[0] == 0) return "";
    size_t nchars = p[0];
    std::string out;
    for (size_t i = 0; i + 1 < nchars && (1 + i * 2 + 1) < maxBytes; i++) {
        u16 c = (u16)(p[1 + i * 2] | (p[1 + i * 2 + 1] << 8));
        if (c == 0) break;
        if (c >= 0xD800 && c <= 0xDBFF && (1 + (i + 1) * 2 + 1) < maxBytes) {
            u16 lo = (u16)(p[1 + (i + 1) * 2] | (p[1 + (i + 1) * 2 + 1] << 8));
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                u32 cp = 0x10000 + (((u32)c - 0xD800) << 10) + (lo - 0xDC00);
                out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
                i++;
                continue;
            }
        }
        if (c == '/' || c == '\\') c = '_';
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F)); }
    }
    return out;
}

// --- FS local ---
inline std::string mtpJoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}
inline bool mtpDelRec(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) return false;
        bool ok = true;
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (!mtpDelRec(mtpJoin(path, e->d_name))) { ok = false; break; }
        }
        closedir(d);
        return ok && rmdir(path.c_str()) == 0;
    }
    return remove(path.c_str()) == 0;
}
inline void mtpDateStr(time_t t, char* out, size_t outLen) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    localtime_r(&t, &tmv);
    int Y = tmv.tm_year + 1900; if (Y < 1980) Y = 1980; if (Y > 2100) Y = 2100;
    int M = tmv.tm_mon + 1; if (M < 1) M = 1; if (M > 12) M = 12;
    int D = tmv.tm_mday; if (D < 1) D = 1; if (D > 31) D = 31;
    int h = tmv.tm_hour; if (h < 0) h = 0; if (h > 23) h = 23;
    int m = tmv.tm_min; if (m < 0) m = 0; if (m > 59) m = 59;
    int s = tmv.tm_sec; if (s < 0) s = 0; if (s > 60) s = 60;
    snprintf(out, outLen, "%04d%02d%02dT%02d%02d%02d", Y, M, D, h, m, s);
}

struct MtpObj { u32 handle; u32 parent; std::string path; std::string name; bool dir; u64 size; time_t mtime; };
inline std::vector<MtpObj> g_objs;
inline u32 g_nextHandle = 1;
inline u32 g_session = 0;
inline bool g_objsDirty = true;
inline const u32 MTP_STORAGE = 0x00010001;
inline const size_t SCAN_MAX = 4000;

inline void mtpScanRec(const std::string& path, u32 parent) {
    if (g_objs.size() >= SCAN_MAX) return;
    DIR* d = opendir(path.c_str());
    if (!d) return;
    // dirs primero, alfabético (estable entre rescans)
    std::vector<std::string> names;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        names.push_back(e->d_name);
    }
    closedir(d);
    std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
        struct stat sa, sb;
        bool da = stat(mtpJoin(path, a).c_str(), &sa) == 0 && S_ISDIR(sa.st_mode);
        bool db = stat(mtpJoin(path, b).c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
        if (da != db) return da > db;
        return a < b;
    });
    for (auto& n : names) {
        if (g_objs.size() >= SCAN_MAX) return;
        std::string fp = mtpJoin(path, n);
        struct stat st;
        if (stat(fp.c_str(), &st) != 0) continue;
        MtpObj o{ g_nextHandle++, parent, fp, n, S_ISDIR(st.st_mode) != 0,
                  S_ISDIR(st.st_mode) ? 0 : (u64)st.st_size, st.st_mtime };
        g_objs.push_back(o);
        if (o.dir) mtpScanRec(fp, o.handle);
    }
}
inline void mtpRescan() {
    g_objs.clear();
    g_nextHandle = 1;
    mtpScanRec("sdmc:/", 0);
    g_objsDirty = false;
    char b[64];
    snprintf(b, sizeof(b), "MTP scan: %u objetos", (unsigned)g_objs.size());
    mtp_usb::mlog(b);
}
inline const MtpObj* mtpFind(u32 h) {
    for (auto& o : g_objs) if (o.handle == h) return &o;
    return nullptr;
}

// --- Bulk I/O delegadas al driver de hardware usbMtpTransfer ---
inline bool epXfer(UsbDsEndpoint* /*ep*/, u8* buf, size_t len, bool isWrite, size_t* doneOut) {
    if (!g_run) return false;
    size_t transferred = usbMtpTransfer(isWrite ? MTP_EP_BULK_IN : MTP_EP_BULK_OUT, isWrite ? 1 : 0, buf, len, 5000000000ULL);
    if (doneOut) *doneOut = transferred;
    return transferred == len;
}

inline bool epWrite(UsbDsEndpoint* /*ep*/, const u8* data, size_t len) {
    if (!g_run) return false;
    size_t transferred = usbMtpTransfer(MTP_EP_BULK_IN, 1, (void*)data, len, 5000000000ULL);
    return transferred == len;
}

// Lee exactamente len bytes (fase comando/parámetros). false = parar o error.
inline bool epReadExact(UsbDsEndpoint* /*ep*/, u8* buf, size_t len) {
    if (!g_run) return false;
    size_t transferred = usbMtpTransfer(MTP_EP_BULK_OUT, 0, buf, len, 500000000ULL);
    return transferred == len;
}

// --- Constructores de datasets ---
inline std::vector<u8> dsDeviceInfo() {
    std::vector<u8> v;
    put16(v, 100); put32(v, 6); put16(v, 100); putStr(v, "microsoft.com: 1.0;"); put16(v, 0);
    u16 ops[] = { OP_GetDeviceInfo, OP_OpenSession, OP_CloseSession, OP_GetStorageIDs,
                  OP_GetStorageInfo, OP_GetNumObjects, OP_GetObjectHandles, OP_GetObjectInfo,
                  OP_GetObject, OP_DeleteObject, OP_SendObjectInfo, OP_SendObject };
    put32(v, 12); for (u16 o : ops) put16(v, o);
    put32(v, 0); // eventos
    put32(v, 0); // props
    put32(v, 0); // captura
    put32(v, 2); put16(v, F_Undefined); put16(v, F_Association); // playback
    putStr(v, "EzFiles"); putStr(v, "EzFiles MTP"); putStr(v, "1.3"); putStr(v, "EZF000000001");
    return v;
}
inline std::vector<u8> dsStorageIDs() {
    std::vector<u8> v; put32(v, 1); put32(v, MTP_STORAGE); return v;
}
inline std::vector<u8> dsStorageInfo() {
    struct statvfs sv; u64 cap = 0, fr = 0;
    if (statvfs("sdmc:/", &sv) == 0) { cap = (u64)sv.f_blocks * sv.f_frsize; fr = (u64)sv.f_bavail * sv.f_frsize; }
    std::vector<u8> v;
    put16(v, 0x0004); put16(v, 0x0003); put16(v, 0x0000);
    put64(v, cap); put64(v, fr); put32(v, 0xFFFFFFFF);
    putStr(v, "EzFiles SD"); putStr(v, "sdcard");
    return v;
}
inline std::vector<u8> dsObjectInfo(const MtpObj& o) {
    std::vector<u8> v;
    put32(v, MTP_STORAGE);
    put16(v, o.dir ? F_Association : F_Undefined);
    put16(v, 0); // protección
    u32 sz = o.size > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (u32)o.size;
    put32(v, sz);
    put16(v, 0); put32(v, 0); put32(v, 0); put32(v, 0); put32(v, 0); put32(v, 0); // thumbs/imagen
    put32(v, o.parent);
    put16(v, o.dir ? 1 : 0); put32(v, 0); put32(v, 0); // assoc
    putStr(v, o.name.c_str());
    char d[24]; mtpDateStr(o.mtime, d, sizeof(d));
    putStr(v, d); putStr(v, d); putEmptyStr(v);
    return v;
}

// --- Envío de contenedores ---
inline bool sendResponse(u32 tx, u16 code, const u32* params, int nparams) {
    std::vector<u8> v;
    put32(v, 12 + 4 * nparams); put16(v, T_Response); put16(v, code); put32(v, tx);
    for (int i = 0; i < nparams; i++) put32(v, params[i]);
    return epWrite(g_epIn, v.data(), v.size());
}
inline bool sendData(u16 code, u32 tx, const std::vector<u8>& payload) {
    std::vector<u8> h;
    put32(h, 12 + (u32)payload.size()); put16(h, T_Data); put16(h, code); put32(h, tx);
    if (!epWrite(g_epIn, h.data(), h.size())) return false;
    if (!payload.empty() && !epWrite(g_epIn, payload.data(), payload.size())) return false;
    return true;
}
inline bool sendFileData(u16 code, u32 tx, const std::string& path, u64 size) {
    u32 hdrSize = size > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (u32)size;
    std::vector<u8> h;
    put32(h, 12 + hdrSize); put16(h, T_Data); put16(h, code); put32(h, tx);
    if (!epWrite(g_epIn, h.data(), h.size())) return false;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<u8> buf(65536);
    u64 left = size;
    bool ok = true;
    while (left > 0 && g_run) {
        size_t want = left > buf.size() ? buf.size() : (size_t)left;
        size_t got = fread(buf.data(), 1, want, f);
        if (got == 0) { ok = false; break; }
        if (!epWrite(g_epIn, buf.data(), got)) { ok = false; break; }
        left -= got;
    }
    fclose(f);
    return ok && left == 0;
}

// --- Bucle de operaciones ---
inline void worker() {
    g_active = true;
    mtp_usb::mlog("MTP worker iniciado");
    // ZLT (Zero Length Termination) gestionado por el driver usb_mtp.c
    std::string pendingPath;
    bool hasPending = false;

    // Esperar a que Windows complete la enumeracion USB (max 30 segundos)
    mtp_usb::mlog("MTP: esperando host...");
    Result wrc = usbMtpWaitReady(30000000000ULL); // 30s
    if (R_FAILED(wrc)) {
        char wb[48];
        snprintf(wb, sizeof(wb), "MTP: host timeout rc=0x%08X", wrc);
        mtp_usb::mlog(wb);
        g_active = false;
        return;
    }
    mtp_usb::mlog("MTP: host listo, escuchando...");

    while (g_run) {
        u8 hdr[12];
        if (!epReadExact(g_epOut, hdr, 12)) continue;
        u32 len = rd32(hdr), type = rd16(hdr + 4), code = rd16(hdr + 6), tx = rd32(hdr + 8);
        if (type != T_Operation || len < 12 || len > 128) {
            char b[64]; snprintf(b, sizeof(b), "MTP frame raro t=%u l=%u", type, len);
            mtp_usb::mlog(b); continue;
        }
        u8 pb[116] = {0};
        if (len > 12 && !epReadExact(g_epOut, pb, len - 12)) continue;
        auto P = [&](int i) -> u32 { return rd32(pb + i * 4); };

        char ob[64];
        snprintf(ob, sizeof(ob), "MTP op 0x%04X tx=%u", code, tx);
        mtp_usb::mlog(ob);

        switch (code) {
        case OP_GetDeviceInfo: {
            auto d = dsDeviceInfo();
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_OpenSession:
            g_session = len >= 16 ? P(0) : 1;
            g_objsDirty = true;
            sendResponse(tx, MR_OK, nullptr, 0);
            break;
        case OP_CloseSession:
            g_session = 0;
            sendResponse(tx, MR_OK, nullptr, 0);
            break;
        case OP_GetStorageIDs: {
            auto d = dsStorageIDs();
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_GetStorageInfo: {
            if (len >= 16 && P(0) != MTP_STORAGE && P(0) != 0xFFFFFFFF) { sendResponse(tx, MR_InvalidStorage, nullptr, 0); break; }
            auto d = dsStorageInfo();
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_GetNumObjects:
        case OP_GetObjectHandles: {
            if (g_objsDirty || g_objs.empty()) mtpRescan();
            u32 parent = len >= 24 ? P(2) : 0xFFFFFFFF;
            u16 fmt = len >= 20 ? (u16)P(1) : 0;
            std::vector<u32> hs;
            for (auto& o : g_objs) {
                if (fmt == F_Association && !o.dir) continue;
                if (fmt != 0 && fmt != F_Association && o.dir) continue;
                if (parent != 0xFFFFFFFF && o.parent != parent) continue;
                hs.push_back(o.handle);
            }
            if (code == OP_GetNumObjects) {
                u32 n = (u32)hs.size();
                sendResponse(tx, MR_OK, &n, 1);
            } else {
                std::vector<u8> d; put32(d, (u32)hs.size());
                for (u32 h : hs) put32(d, h);
                if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            }
            break;
        }
        case OP_GetObjectInfo: {
            if (g_objsDirty || g_objs.empty()) mtpRescan();
            const MtpObj* o = len >= 16 ? mtpFind(P(0)) : nullptr;
            if (!o) { sendResponse(tx, MR_GeneralError, nullptr, 0); break; }
            auto d = dsObjectInfo(*o);
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_GetObject: {
            if (g_objsDirty || g_objs.empty()) mtpRescan();
            const MtpObj* o = len >= 16 ? mtpFind(P(0)) : nullptr;
            if (!o || o->dir) { sendResponse(tx, MR_GeneralError, nullptr, 0); break; }
            char b[96]; snprintf(b, sizeof(b), "MTP GET %s (%llu B)", o->name.c_str(), (unsigned long long)o->size);
            mtp_usb::mlog(b);
            if (sendFileData(code, tx, o->path, o->size)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_DeleteObject: {
            if (g_objsDirty || g_objs.empty()) mtpRescan();
            const MtpObj* o = len >= 16 ? mtpFind(P(0)) : nullptr;
            bool ok = o && mtpDelRec(o->path);
            if (ok) g_objsDirty = true;
            sendResponse(tx, ok ? MR_OK : MR_GeneralError, nullptr, 0);
            break;
        }
        case OP_SendObjectInfo: {
            u32 parent = len >= 20 ? P(1) : 0;
            // Fase de datos: dataset ObjectInfo
            u8 dh[12];
            if (!epReadExact(g_epOut, dh, 12)) break;
            u32 dlen = rd32(dh);
            if (rd16(dh + 4) != T_Data || dlen < 12 || dlen > 4096) { sendResponse(tx, MR_GeneralError, nullptr, 0); break; }
            std::vector<u8> dd(dlen - 12);
            if (!dd.empty() && !epReadExact(g_epOut, dd.data(), dd.size())) break;
            std::string name = dd.size() > 53 ? getMtpStr(dd.data() + 52, dd.size() - 52) : "";
            if (name.empty()) name = "mtp_upload.bin";
            std::string dir = ".";
            if (parent != 0) { const MtpObj* po = mtpFind(parent); if (!po || !po->dir) { sendResponse(tx, MR_GeneralError, nullptr, 0); break; } dir = po->path; }
            else dir = "sdmc:/";
            pendingPath = mtpJoin(dir, name);
            if (access(pendingPath.c_str(), F_OK) == 0) {
                // Evita sobrescribir: sufijo numérico
                for (int i = 1; i < 100; i++) {
                    std::string cand = pendingPath + "." + std::to_string(i);
                    if (access(cand.c_str(), F_OK) != 0) { pendingPath = cand; break; }
                }
            }
            FILE* f = fopen(pendingPath.c_str(), "wb");
            if (f) fclose(f); else { sendResponse(tx, MR_GeneralError, nullptr, 0); break; }
            hasPending = true;
            MtpObj no{ g_nextHandle++, parent, pendingPath, name, false, 0, time(nullptr) };
            g_objs.push_back(no);
            u32 rp[3] = { MTP_STORAGE, parent, no.handle };
            sendResponse(tx, MR_OK, rp, 3);
            break;
        }
        case OP_SendObject: {
            if (!hasPending) { sendResponse(tx, MR_GeneralError, nullptr, 0); break; }
            u8 dh[12];
            if (!epReadExact(g_epOut, dh, 12)) { hasPending = false; break; }
            u32 dlen = rd32(dh);
            if (rd16(dh + 4) != T_Data || dlen < 12) { sendResponse(tx, MR_GeneralError, nullptr, 0); hasPending = false; break; }
            u64 total = dlen - 12;
            FILE* f = fopen(pendingPath.c_str(), "wb");
            bool ok = f != nullptr;
            std::vector<u8> buf(65536);
            u64 left = total;
            while (ok && left > 0 && g_run) {
                size_t want = left > buf.size() ? buf.size() : (size_t)left;
                if (!epReadExact(g_epOut, buf.data(), want)) { ok = false; break; }
                ok = fwrite(buf.data(), 1, want, f) == want;
                left -= want;
            }
            if (f) fclose(f);
            hasPending = false;
            g_objsDirty = true;
            char b[96]; snprintf(b, sizeof(b), "MTP PUT %s (%llu B) %s", pendingPath.c_str(), (unsigned long long)total, ok ? "OK" : "ERR");
            mtp_usb::mlog(b);
            sendResponse(tx, ok ? MR_OK : MR_GeneralError, nullptr, 0);
            break;
        }
        default:
            sendResponse(tx, MR_NotSupported, nullptr, 0);
            break;
        }
    }
    g_active = false;
    mtp_usb::mlog("MTP worker detenido");
}

inline bool running() { return g_active; }

inline bool start(UsbDsEndpoint* epIn = nullptr, UsbDsEndpoint* epOut = nullptr) {
    (void)epIn; (void)epOut;
    if (g_active) return true;
    g_run = true;
    g_thr = std::thread(worker);
    return true;
}

inline void stop() {
    if (!g_active) return;
    g_run = false;
    if (g_thr.joinable()) g_thr.join();
}

} // namespace mtp_ops
