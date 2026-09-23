#pragma once
// Respondedor MTP compatible con Windows Explorer y basado en Atmosphère Haze.
// Implementa operaciones PTP estándar y extensiones MTP (0x98XX) de propiedades de objetos.

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

// --- Opcodes PTP estándar y extensiones MTP ---
enum {
    OP_GetDeviceInfo               = 0x1001,
    OP_OpenSession                 = 0x1002,
    OP_CloseSession                = 0x1003,
    OP_GetStorageIDs               = 0x1004,
    OP_GetStorageInfo              = 0x1005,
    OP_GetNumObjects               = 0x1006,
    OP_GetObjectHandles            = 0x1007,
    OP_GetObjectInfo               = 0x1008,
    OP_GetObject                   = 0x1009,
    OP_GetThumb                    = 0x100A,
    OP_DeleteObject                = 0x100B,
    OP_SendObjectInfo              = 0x100C,
    OP_SendObject                  = 0x100D,
    OP_GetDevicePropDesc           = 0x1014,

    // MTP Object Properties (requeridas por Windows)
    OP_MtpGetObjectPropsSupported  = 0x9801,
    OP_MtpGetObjectPropDesc        = 0x9802,
    OP_MtpGetObjectPropValue       = 0x9803,
    OP_MtpSetObjectPropValue       = 0x9804,
    OP_MtpGetObjPropList           = 0x9805,
};

// Códigos de respuesta MTP
enum {
    MR_OK                   = 0x2001,
    MR_GeneralError         = 0x2002,
    MR_SessionNotOpen       = 0x2003,
    MR_InvalidTransactionId = 0x2004,
    MR_NotSupported         = 0x2005,
    MR_ParameterNotSupported= 0x2006,
    MR_IncompleteTransfer   = 0x2007,
    MR_InvalidStorage       = 0x2009,
    MR_InvalidObjectHandle  = 0x200B,
    MR_DeviceBusy           = 0x2019,
    MR_PropNotSupported     = 0xA80A,
};

// Códigos de propiedades de objeto MTP
enum {
    PROP_StorageId           = 0xDC01,
    PROP_ObjectFormat        = 0xDC02,
    PROP_ProtectionStatus    = 0xDC03,
    PROP_ObjectCompressedSize= 0xDC04,
    PROP_ObjectFileName      = 0xDC07,
    PROP_DateModified        = 0xDC09,
    PROP_ParentObject        = 0xDC0B,
    PROP_PersistentGUID      = 0xDC41,
    PROP_Name                = 0xDC44,
};

// Tipos de datos MTP
enum {
    TYPE_U16    = 0x0004,
    TYPE_U32    = 0x0006,
    TYPE_U64    = 0x0008,
    TYPE_U128   = 0x000A,
    TYPE_String = 0xFFFF,
};

enum { T_Operation = 1, T_Data = 2, T_Response = 3 };
enum { F_Undefined = 0x3000, F_Association = 0x3001 };

inline std::atomic<bool> g_run(false);
inline std::atomic<bool> g_active(false);
inline std::thread g_thr;

inline UsbDsEndpoint* g_epIn = nullptr;
inline UsbDsEndpoint* g_epOut = nullptr;

// --- LE helpers ---
inline void put8(std::vector<u8>& v, u8 x) { v.push_back(x); }
inline void put16(std::vector<u8>& v, u16 x) { v.push_back((u8)x); v.push_back((u8)(x >> 8)); }
inline void put32(std::vector<u8>& v, u32 x) { for (int i = 0; i < 4; i++) v.push_back((u8)(x >> (8 * i))); }
inline void put64(std::vector<u8>& v, u64 x) { for (int i = 0; i < 8; i++) v.push_back((u8)(x >> (8 * i))); }
inline u16 rd16(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }
inline u32 rd32(const u8* p) { return (u32)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24)); }

// UTF-8 -> UTF-16LE
inline void putUtf16(std::vector<u8>& v, const char* s) {
    std::vector<u16> u;
    for (const u8* p = (const u8*)s; *p;) {
        u32 cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0) { cp = (*p & 0x1F); p++; if ((*p & 0xC0) != 0x80) cp = 0xFFFD; else { cp = (cp << 6) | (*p & 0x3F); p++; } }
        else if ((*p & 0xF0) == 0xE0) { cp = (*p & 0x0F); p++; for (int i = 0; i < 2; i++) { if ((*p & 0xC0) != 0x80) { cp = 0xFFFD; break; } cp = (cp << 6) | (*p & 0x3F); p++; } }
        else if ((*p & 0xF8) == 0xF0) { cp = (*p & 0x07); p++; for (int i = 0; i < 3; i++) { if ((*p & 0xC0) != 0x80) { cp = 0xFFFD; break; } cp = (cp << 6) | (*p & 0x3F); p++; } }
        else { cp = 0xFFFD; p++; }
        if (cp < 0x10000) u.push_back((u16)cp);
        else { cp -= 0x10000; u.push_back((u16)(0xD800 + (cp >> 10))); u.push_back((u16)(0xDC00 + (cp & 0x3FF))); }
    }
    v.push_back((u8)(u.size() + 1));
    for (u16 c : u) put16(v, c);
    put16(v, 0);
}
inline void putStr(std::vector<u8>& v, const char* s) { putUtf16(v, s ? s : ""); }
inline void putEmptyStr(std::vector<u8>& v) { v.push_back(0); }

// UTF-16LE -> UTF-8
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
struct MtpObj {
    u32 handle;
    u32 parent;
    std::string path;
    std::string name;
    bool dir;
    u64 size;
    time_t mtime;
};

constexpr u32 MTP_STORAGE = 0x00010001;
constexpr size_t SCAN_MAX = 8192;
inline std::vector<MtpObj> g_objs;
inline u32 g_nextHandle = 1;
inline bool g_objsDirty = true;
inline u32 g_session = 0;

inline void mtpDateStr(time_t t, char* buf, size_t sz) {
    struct tm tm;
    if (gmtime_r(&t, &tm)) snprintf(buf, sz, "%04d%02d%02dT%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    else snprintf(buf, sz, "20260101T000000");
}

inline std::string mtpJoin(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

inline bool mtpDelRec(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) return false;
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            mtpDelRec(mtpJoin(path, e->d_name));
        }
        closedir(d);
        return rmdir(path.c_str()) == 0;
    }
    return unlink(path.c_str()) == 0;
}


inline u32 getOrRegisterHandle(const std::string& path, u32 parent, const std::string& name, bool dir, u64 size, time_t mtime) {
    for (auto& o : g_objs) {
        if (o.path == path) {
            o.parent = parent;
            o.size = size;
            o.mtime = mtime;
            return o.handle;
        }
    }
    u32 h = g_nextHandle++;
    g_objs.push_back({ h, parent, path, name, dir, size, mtime });
    return h;
}

// Escanea ÚNICAMENTE la carpeta solicitada en demanda (instantáneo, sin bloquear el hilo)
inline std::vector<u32> mtpScanDirectory(const std::string& dirPath, u32 parentHandle) {
    std::vector<u32> handles;
    DIR* d = opendir(dirPath.c_str());
    if (!d) return handles;

    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string fp = mtpJoin(dirPath, e->d_name);
        struct stat st;
        if (stat(fp.c_str(), &st) != 0) continue;
        bool isDir = S_ISDIR(st.st_mode);
        u64 sz = isDir ? 0 : (u64)st.st_size;
        u32 h = getOrRegisterHandle(fp, parentHandle, e->d_name, isDir, sz, st.st_mtime);
        handles.push_back(h);
    }
    closedir(d);
    return handles;
}

inline const MtpObj* mtpFind(u32 h) {
    for (auto& o : g_objs) if (o.handle == h) return &o;
    return nullptr;
}

// --- Bulk I/O delegadas al driver de hardware usbMtpTransfer ---
inline bool epWrite(UsbDsEndpoint* /*ep*/, const u8* data, size_t len) {
    if (!g_run) return false;
    size_t transferred = usbMtpTransfer(MTP_EP_BULK_IN, 1, (void*)data, len, 5000000000ULL);
    return transferred == len;
}

inline bool sendResponse(u32 tx, u16 code, const u32* params, int nparams) {
    std::vector<u8> v;
    put32(v, 12 + 4 * nparams); put16(v, T_Response); put16(v, code); put32(v, tx);
    for (int i = 0; i < nparams; i++) put32(v, params[i]);
    return epWrite(g_epIn, v.data(), v.size());
}

inline bool sendData(u16 code, u32 tx, const std::vector<u8>& payload) {
    std::vector<u8> packet;
    packet.reserve(12 + payload.size());
    put32(packet, 12 + (u32)payload.size());
    put16(packet, T_Data);
    put16(packet, code);
    put32(packet, tx);
    packet.insert(packet.end(), payload.begin(), payload.end());
    bool ok = epWrite(g_epIn, packet.data(), packet.size());
    if (ok && (packet.size() % 512) == 0) {
        u8 z = 0;
        epWrite(g_epIn, &z, 0); // ZLT requerido cuando el paquete es múltiplo de 512
    }
    return ok;
}

inline bool sendFileData(u16 code, u32 tx, const std::string& path, u64 size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    u32 hdrSize = size > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : (u32)size;
    u64 totalSize = 12 + (u64)hdrSize;

    // Buffer de 64 KB para transferencias rápidas
    std::vector<u8> buf(65536);
    // Escribir cabecera de 12 bytes dentro del mismo búfer
    buf[0]  = (u8)(totalSize);
    buf[1]  = (u8)(totalSize >> 8);
    buf[2]  = (u8)(totalSize >> 16);
    buf[3]  = (u8)(totalSize >> 24);
    buf[4]  = (u8)(T_Data);
    buf[5]  = (u8)(T_Data >> 8);
    buf[6]  = (u8)(code);
    buf[7]  = (u8)(code >> 8);
    buf[8]  = (u8)(tx);
    buf[9]  = (u8)(tx >> 8);
    buf[10] = (u8)(tx >> 16);
    buf[11] = (u8)(tx >> 24);

    size_t firstChunk = (size_t)(buf.size() - 12);
    if ((u64)firstChunk > size) firstChunk = (size_t)size;
    size_t got = fread(buf.data() + 12, 1, firstChunk, f);
    if (firstChunk > 0 && got != firstChunk) { fclose(f); return false; }

    // Enviar primer paquete (cabecera + primeros datos juntos sin short-packet prematuro)
    if (!epWrite(g_epIn, buf.data(), 12 + got)) { fclose(f); return false; }

    u64 left = size - got;
    bool ok = true;
    while (left > 0 && g_run) {
        size_t want = left > buf.size() ? buf.size() : (size_t)left;
        size_t n = fread(buf.data(), 1, want, f);
        if (n == 0) { ok = false; break; }
        if (!epWrite(g_epIn, buf.data(), n)) { ok = false; break; }
        left -= n;
    }
    fclose(f);

    if (ok && (totalSize % 512) == 0) {
        u8 z = 0;
        epWrite(g_epIn, &z, 0); // ZLT
    }
    return ok && left == 0;
}

// --- Constructores de datasets ---
inline std::vector<u8> dsDeviceInfo() {
    std::vector<u8> v;
    put16(v, 100);                    // MtpStandardVersion = 100
    put32(v, 6);                      // MtpVendorExtensionId = 6 (MTP)
    put16(v, 100);                    // MtpStandardVersion
    putStr(v, "microsoft.com: 1.0;"); // MtpVendorExtensionDesc
    put16(v, 0);                      // FunctionalMode = 0

    // Operaciones soportadas declaradas (incluye extensiones MTP para Windows)
    u16 ops[] = {
        OP_GetDeviceInfo, OP_OpenSession, OP_CloseSession, OP_GetStorageIDs,
        OP_GetStorageInfo, OP_GetNumObjects, OP_GetObjectHandles, OP_GetObjectInfo,
        OP_GetObject, OP_DeleteObject, OP_SendObjectInfo, OP_SendObject,
        OP_MtpGetObjectPropsSupported, OP_MtpGetObjectPropDesc,
        OP_MtpGetObjectPropValue, OP_MtpGetObjPropList
    };
    put32(v, sizeof(ops) / sizeof(ops[0]));
    for (u16 o : ops) put16(v, o);

    put32(v, 0); // Eventos soportados
    put32(v, 0); // Device properties

    // Formatos de captura
    put32(v, 0);

    // Formatos de reproducción
    put32(v, 2);
    put16(v, F_Undefined);
    put16(v, F_Association);

    // Información de fabricante / dispositivo (idéntico a Haze)
    putStr(v, "Nintendo");
    putStr(v, "Nintendo Switch");
    putStr(v, "1.3.0");
    putStr(v, "000000000001");
    return v;
}

inline std::vector<u8> dsStorageIDs() {
    std::vector<u8> v; put32(v, 1); put32(v, MTP_STORAGE); return v;
}

inline std::vector<u8> dsStorageInfo() {
    struct statvfs sv; u64 cap = 0, fr = 0;
    if (statvfs("sdmc:/", &sv) == 0) { cap = (u64)sv.f_blocks * sv.f_frsize; fr = (u64)sv.f_bavail * sv.f_frsize; }
    std::vector<u8> v;
    put16(v, 0x0004); // FixedRAM
    put16(v, 0x0003); // GenericHierarchical
    put16(v, 0x0000); // Read-Write
    put64(v, cap);
    put64(v, fr);
    put32(v, 0xFFFFFFFF); // Free images
    putStr(v, "Nintendo Switch SD");
    putStr(v, "sdcard");
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

// Lista de propiedades soportadas para objetos MTP
inline const u16 kSupportedProps[] = {
    PROP_StorageId, PROP_ObjectFormat, PROP_ObjectCompressedSize,
    PROP_ObjectFileName, PROP_ParentObject, PROP_PersistentGUID
};

// --- Bucle principal del worker MTP ---
inline void worker() {
    g_active = true;
    mtp_usb::mlog("MTP worker iniciado");
    std::string pendingPath;
    bool hasPending = false;

    // Buffer de 4KB alineado para recepción continua sin cortes de USB
    alignas(0x1000) static u8 s_rxBuf[0x1000];

    // Esperar a que Windows configure la interfaz
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
        // Leer paquete de comando completo (mínimo 512 bytes recibibles)
        size_t rxGot = usbMtpTransfer(MTP_EP_BULK_OUT, 0, s_rxBuf, sizeof(s_rxBuf), 1000000000ULL);
        if (rxGot < 12) continue; // Si no hay datos en 1s, verificar g_run y seguir escuchando

        u32 len  = rd32(s_rxBuf);
        u16 type = rd16(s_rxBuf + 4);
        u16 code = rd16(s_rxBuf + 6);
        u32 tx   = rd32(s_rxBuf + 8);

        if (type != T_Operation || len < 12) {
            continue;
        }

        // Parámetros incluidos directamente en el paquete recibido
        auto P = [&](int i) -> u32 {
            size_t off = 12 + i * 4;
            if (off + 4 <= rxGot && off + 4 <= len) return rd32(s_rxBuf + off);
            return 0;
        };

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
            g_objs.clear();
            g_nextHandle = 1;
            mtpScanDirectory("sdmc:/", 0); // Cargar sólo la raíz (1 ms)
            sendResponse(tx, MR_OK, nullptr, 0);
            break;
        case OP_CloseSession:
            g_session = 0;
            g_objs.clear();
            sendResponse(tx, MR_OK, nullptr, 0);
            break;
        case OP_GetStorageIDs: {
            auto d = dsStorageIDs();
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_GetStorageInfo: {
            if (len >= 16 && P(0) != MTP_STORAGE && P(0) != 0xFFFFFFFF) {
                sendResponse(tx, MR_InvalidStorage, nullptr, 0);
                break;
            }
            auto d = dsStorageInfo();
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_GetNumObjects:
        case OP_GetObjectHandles: {
            u32 parent = len >= 24 ? P(2) : 0xFFFFFFFF;
            u32 targetParent = 0;
            std::string scanPath = "sdmc:/";
            if (parent != 0 && parent != 0xFFFFFFFF) {
                const MtpObj* po = mtpFind(parent);
                if (po && po->dir) {
                    scanPath = po->path;
                    targetParent = po->handle;
                }
            }

            // Buscar en caché si la carpeta ya fue escaneada
            std::vector<u32> hs;
            for (auto& o : g_objs) {
                if (o.parent == targetParent) hs.push_back(o.handle);
            }
            // Si es la primera vez que se accede a esta carpeta, escanearla en disco
            if (hs.empty()) {
                hs = mtpScanDirectory(scanPath, targetParent);
            }

            if (code == OP_GetNumObjects) {
                u32 n = (u32)hs.size();
                sendResponse(tx, MR_OK, &n, 1);
            } else {
                std::vector<u8> d;
                put32(d, (u32)hs.size());
                for (u32 h : hs) put32(d, h);
                if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            }
            break;
        }
        case OP_GetObjectInfo: {
            const MtpObj* o = len >= 16 ? mtpFind(P(0)) : nullptr;
            if (!o) { sendResponse(tx, MR_InvalidObjectHandle, nullptr, 0); break; }
            auto d = dsObjectInfo(*o);
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_GetObject: {
            const MtpObj* o = len >= 16 ? mtpFind(P(0)) : nullptr;
            if (!o || o->dir) { sendResponse(tx, MR_InvalidObjectHandle, nullptr, 0); break; }
            char b[96]; snprintf(b, sizeof(b), "MTP GET %s (%llu B)", o->name.c_str(), (unsigned long long)o->size);
            mtp_usb::mlog(b);
            if (sendFileData(code, tx, o->path, o->size)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_DeleteObject: {
            const MtpObj* o = len >= 16 ? mtpFind(P(0)) : nullptr;
            bool ok = o && mtpDelRec(o->path);
            sendResponse(tx, ok ? MR_OK : MR_GeneralError, nullptr, 0);
            break;
        }

        // --- Extensiones de Propiedades MTP (0x98XX para Windows Explorer) ---
        case OP_MtpGetObjectPropsSupported: {
            std::vector<u8> d;
            put32(d, sizeof(kSupportedProps) / sizeof(kSupportedProps[0]));
            for (u16 prop : kSupportedProps) put16(d, prop);
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_MtpGetObjectPropDesc: {
            u16 prop = (u16)P(0);
            std::vector<u8> d;
            put16(d, prop); // PropertyCode
            switch (prop) {
            case PROP_StorageId:
                put16(d, TYPE_U32); put8(d, 0); put32(d, MTP_STORAGE); break;
            case PROP_ObjectFormat:
                put16(d, TYPE_U16); put8(d, 0); put16(d, 0); break;
            case PROP_ObjectCompressedSize:
                put16(d, TYPE_U64); put8(d, 0); put64(d, 0); break;
            case PROP_ParentObject:
                put16(d, TYPE_U32); put8(d, 0); put32(d, 0); break;
            case PROP_ObjectFileName:
                put16(d, TYPE_String); put8(d, 1); putEmptyStr(d); break;
            case PROP_PersistentGUID:
                put16(d, TYPE_U128); put8(d, 0); put64(d, 0); put64(d, 0); break;
            default:
                sendResponse(tx, MR_PropNotSupported, nullptr, 0);
                continue;
            }
            put32(d, 0); // GroupCode = 0
            d.push_back(0); // FormFlag = 0 (None)
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_MtpGetObjectPropValue: {
            u32 handle = P(0);
            u16 prop   = (u16)P(1);
            const MtpObj* o = mtpFind(handle);
            if (!o) { sendResponse(tx, MR_InvalidObjectHandle, nullptr, 0); break; }
            std::vector<u8> d;
            switch (prop) {
            case PROP_StorageId:
                put32(d, MTP_STORAGE); break;
            case PROP_ObjectFormat:
                put16(d, o->dir ? F_Association : F_Undefined); break;
            case PROP_ObjectCompressedSize:
                put64(d, o->size); break;
            case PROP_ParentObject:
                put32(d, o->parent); break;
            case PROP_ObjectFileName:
                putStr(d, o->name.c_str()); break;
            case PROP_PersistentGUID:
                put64(d, o->handle); put64(d, 0); break;
            default:
                sendResponse(tx, MR_PropNotSupported, nullptr, 0);
                continue;
            }
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }
        case OP_MtpGetObjPropList: {
            u32 handle = P(0);
            u32 depth  = len >= 32 ? P(4) : 0;
            // Si Windows solicita profundidad recursiva o lote de todos los objetos,
            // devolver SpecificationByDepthUnsupported (0xA808) igual que Atmosphère Haze.
            // Esto instruye a Windows a usar GetObjectInfo (0x1008) por objeto, que es instantáneo y no se cuelga.
            if (depth != 0 || handle == 0 || handle == 0xFFFFFFFF) {
                sendResponse(tx, 0xA808, nullptr, 0);
                break;
            }

            const MtpObj* o = mtpFind(handle);
            if (!o) { sendResponse(tx, MR_InvalidObjectHandle, nullptr, 0); break; }

            u16 prop = (u16)P(2);
            std::vector<u8> elemBuf;
            u32 count = 0;
            auto emit = [&](u16 pcode, u16 ptype, auto fn) {
                if (prop != 0 && prop != 0xFFFF && prop != pcode) return;
                put32(elemBuf, o->handle);
                put16(elemBuf, pcode);
                put16(elemBuf, ptype);
                fn(elemBuf);
                count++;
            };
            emit(PROP_StorageId, TYPE_U32, [](std::vector<u8>& b) { put32(b, MTP_STORAGE); });
            emit(PROP_ObjectFormat, TYPE_U16, [&](std::vector<u8>& b) { put16(b, o->dir ? F_Association : F_Undefined); });
            emit(PROP_ObjectCompressedSize, TYPE_U64, [&](std::vector<u8>& b) { put64(b, o->size); });
            emit(PROP_ParentObject, TYPE_U32, [&](std::vector<u8>& b) { put32(b, o->parent); });
            emit(PROP_ObjectFileName, TYPE_String, [&](std::vector<u8>& b) { putStr(b, o->name.c_str()); });
            emit(PROP_PersistentGUID, TYPE_U128, [&](std::vector<u8>& b) { put64(b, o->handle); put64(b, 0); });

            std::vector<u8> d;
            put32(d, count);
            d.insert(d.end(), elemBuf.begin(), elemBuf.end());
            if (sendData(code, tx, d)) sendResponse(tx, MR_OK, nullptr, 0);
            break;
        }

        default:
            // Respuesta a cualquier comando no reconocido (evita que el host se quede colgado)
            sendResponse(tx, MR_NotSupported, nullptr, 0);
            break;
        }
    }

    g_active = false;
    mtp_usb::mlog("MTP worker finalizado");
}

inline void start() {
    if (g_run) return;
    g_run = true;
    g_thr = std::thread(worker);
}

inline void stop() {
    if (!g_run) return;
    g_run = false;
    if (g_thr.joinable()) g_thr.join();
    g_active = false;
}

inline bool running() {
    return g_run && g_active;
}

} // namespace mtp_ops
