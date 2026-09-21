#pragma once
// USB-DS / MTP: Inicialización correcta para fw >= 5.0 y fw < 5.0.
//
// CAMBIO CLAVE respecto a la versión anterior:
//   1. usbDsInitialize() se llama ANTES de cualquier otra llamada usb:ds.
//   2. teardown() llama usbDsExit() para liberar el servicio.
//   3. usbDsEnable() solo se llama en fw < 5.0 (en fw >= 5.0 la enumeración
//      es automática una vez habilitada la interfaz).
//   4. En fw >= 5.0, usbDsSetVidPidBcd / usbDsAddUsbStringDescriptor /
//      usbDsSetUsbDeviceDescriptor son las funciones correctas del API nuevo.
//   5. El VID/PID usa 0x18D1/0x4EE2 (clase de dispositivo MTP compatible con
//      Windows sin driver extra), no el VID de Nintendo.
//
// Todo no-bloqueante, todos los Result verificados, sin colgar la app.
#include <switch.h>
#include <cstring>
#include <string>
#include <cstdio>

namespace mtp_usb {

inline void (*g_logCb)(const char* msg) = nullptr;
inline void mlog(const std::string& s) {
    if (g_logCb) g_logCb(s.c_str());
}

inline UsbDsInterface* g_iface    = nullptr;
inline UsbDsEndpoint*  g_epBulkIn  = nullptr;
inline UsbDsEndpoint*  g_epBulkOut = nullptr;
inline UsbDsEndpoint*  g_epIntrIn  = nullptr;
inline bool            g_ready     = false;
inline bool            g_usbInited = false;   // ← nuevo: rastrea si llamamos usbDsInitialize
inline u8              g_lastSetup[32] = {0};
inline bool            g_haveLastSetup = false;

inline const char* rcStr(Result rc) {
    static char b[20];
    snprintf(b, sizeof(b), "rc=0x%X", rc);
    return b;
}

// Cierra endpoints, interfaz y libera el servicio usb:ds.
inline void teardown() {
    if (g_epBulkIn)  { usbDsEndpoint_Close(g_epBulkIn);  g_epBulkIn  = nullptr; }
    if (g_epBulkOut) { usbDsEndpoint_Close(g_epBulkOut); g_epBulkOut = nullptr; }
    if (g_epIntrIn)  { usbDsEndpoint_Close(g_epIntrIn);  g_epIntrIn  = nullptr; }
    if (g_iface)     { usbDsInterface_Close(g_iface);     g_iface     = nullptr; }
    if (g_usbInited) { usbDsExit(); g_usbInited = false; }   // ← CLAVE
    g_ready = false;
    g_haveLastSetup = false;
}

// Registra y habilita la interfaz MTP. Retorna true si quedó lista para USB bulk.
inline bool setup() {
    teardown();   // Siempre limpiamos antes

    // ── PASO 1: Inicializar el servicio usb:ds ────────────────────────────────
    Result rc = usbDsInitialize();
    if (R_FAILED(rc)) {
        mlog(std::string("MTP usbDsInit ERR ") + rcStr(rc));
        return false;
    }
    g_usbInited = true;
    mlog("MTP usbDsInitialize OK");

    // ── PASO 2: Información del dispositivo (VID/PID MTP genérico) ────────────
    // 0x18D1:0x4EE2 = Google/Android MTP → Windows lo reconoce sin driver extra
    UsbDsDeviceInfo devInfo;
    memset(&devInfo, 0, sizeof(devInfo));
    devInfo.idVendor  = 0x18D1;
    devInfo.idProduct = 0x4EE2;
    devInfo.bcdDevice = 0x0200;
    snprintf(devInfo.Manufacturer, sizeof(devInfo.Manufacturer), "EzFiles");
    snprintf(devInfo.Product,      sizeof(devInfo.Product),      "EzFiles SD Card");
    snprintf(devInfo.SerialNumber, sizeof(devInfo.SerialNumber), "EZF00000001");

    rc = usbDsSetVidPidBcd(&devInfo);
    if (R_FAILED(rc)) {
        mlog(std::string("MTP VidPid ERR ") + rcStr(rc));
        teardown();
        return false;
    }
    mlog("MTP VidPid OK (0x18D1:0x4EE2)");

    // ── PASO 3: String descriptors ───────────────────────────────────────────
    u8 iMan = 0, iProd = 0, iSer = 0;
    if (R_FAILED(rc = usbDsAddUsbStringDescriptor(&iMan,  "EzFiles")))         { mlog(std::string("MTP strMan ERR ")  + rcStr(rc)); teardown(); return false; }
    if (R_FAILED(rc = usbDsAddUsbStringDescriptor(&iProd, "EzFiles SD Card"))) { mlog(std::string("MTP strProd ERR ") + rcStr(rc)); teardown(); return false; }
    if (R_FAILED(rc = usbDsAddUsbStringDescriptor(&iSer,  "EZF00000001")))     { mlog(std::string("MTP strSer ERR ")  + rcStr(rc)); teardown(); return false; }
    mlog("MTP string descriptors OK");

    // ── PASO 4: Device descriptor (Full Speed y High Speed) ──────────────────
    struct usb_device_descriptor devDesc;
    memset(&devDesc, 0, sizeof(devDesc));
    devDesc.bLength            = USB_DT_DEVICE_SIZE;
    devDesc.bDescriptorType    = USB_DT_DEVICE;
    devDesc.bcdUSB             = 0x0200;   // USB 2.0
    devDesc.bDeviceClass       = 0x00;     // Defined at interface level
    devDesc.bDeviceSubClass    = 0x00;
    devDesc.bDeviceProtocol    = 0x00;
    devDesc.bMaxPacketSize0    = 64;
    devDesc.idVendor           = 0x18D1;
    devDesc.idProduct          = 0x4EE2;
    devDesc.bcdDevice          = 0x0200;
    devDesc.iManufacturer      = iMan;
    devDesc.iProduct           = iProd;
    devDesc.iSerialNumber      = iSer;
    devDesc.bNumConfigurations = 1;

    if (R_FAILED(rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &devDesc))) {
        mlog(std::string("MTP devDescFS ERR ") + rcStr(rc)); teardown(); return false;
    }
    if (R_FAILED(rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &devDesc))) {
        mlog(std::string("MTP devDescHS ERR ") + rcStr(rc)); teardown(); return false;
    }
    mlog("MTP device descriptor OK");

    // ── PASO 5: Interface descriptor (clase 6 = Image / PTP-MTP) ─────────────
    struct usb_interface_descriptor ifDesc;
    memset(&ifDesc, 0, sizeof(ifDesc));
    ifDesc.bLength            = USB_DT_INTERFACE_SIZE;
    ifDesc.bDescriptorType    = USB_DT_INTERFACE;
    ifDesc.bInterfaceNumber   = USBDS_DEFAULT_InterfaceNumber;
    ifDesc.bAlternateSetting  = 0;
    ifDesc.bNumEndpoints      = 3;
    ifDesc.bInterfaceClass    = 6;   // Image
    ifDesc.bInterfaceSubClass = 1;   // Still Image Capture
    ifDesc.bInterfaceProtocol = 1;   // PIMA 15470 (MTP/PTP)
    ifDesc.iInterface         = 0;

    if (R_FAILED(rc = usbDsGetDsInterface(&g_iface, &ifDesc, "EzFiles-MTP"))) {
        mlog(std::string("MTP iface ERR ") + rcStr(rc)); teardown(); return false;
    }
    mlog("MTP interface registered");

    // ── PASO 6: Endpoints ────────────────────────────────────────────────────
    // Bulk OUT (host → device, para recibir comandos)
    struct usb_endpoint_descriptor epBulkOut;
    memset(&epBulkOut, 0, sizeof(epBulkOut));
    epBulkOut.bLength          = USB_DT_ENDPOINT_SIZE;
    epBulkOut.bDescriptorType  = USB_DT_ENDPOINT;
    epBulkOut.bEndpointAddress = USB_ENDPOINT_OUT;
    epBulkOut.bmAttributes     = USB_TRANSFER_TYPE_BULK;
    epBulkOut.wMaxPacketSize   = 512;

    // Bulk IN (device → host, para enviar datos/respuestas)
    struct usb_endpoint_descriptor epBulkIn;
    memset(&epBulkIn, 0, sizeof(epBulkIn));
    epBulkIn.bLength           = USB_DT_ENDPOINT_SIZE;
    epBulkIn.bDescriptorType   = USB_DT_ENDPOINT;
    epBulkIn.bEndpointAddress  = USB_ENDPOINT_IN;
    epBulkIn.bmAttributes      = USB_TRANSFER_TYPE_BULK;
    epBulkIn.wMaxPacketSize    = 512;

    // Interrupt IN (device → host, para eventos asíncronos MTP)
    struct usb_endpoint_descriptor epIntr;
    memset(&epIntr, 0, sizeof(epIntr));
    epIntr.bLength             = USB_DT_ENDPOINT_SIZE;
    epIntr.bDescriptorType     = USB_DT_ENDPOINT;
    epIntr.bEndpointAddress    = USB_ENDPOINT_IN;
    epIntr.bmAttributes        = USB_TRANSFER_TYPE_INTERRUPT;
    epIntr.wMaxPacketSize      = 28;
    epIntr.bInterval           = 4;

    if (R_FAILED(rc = usbDsInterface_GetDsEndpoint(g_iface, &g_epBulkOut, &epBulkOut))) { mlog(std::string("MTP epOUT ERR ")  + rcStr(rc)); teardown(); return false; }
    if (R_FAILED(rc = usbDsInterface_GetDsEndpoint(g_iface, &g_epBulkIn,  &epBulkIn)))  { mlog(std::string("MTP epIN ERR ")   + rcStr(rc)); teardown(); return false; }
    if (R_FAILED(rc = usbDsInterface_GetDsEndpoint(g_iface, &g_epIntrIn,  &epIntr)))    { mlog(std::string("MTP epINTR ERR ") + rcStr(rc)); teardown(); return false; }
    mlog("MTP endpoints OK");

    // ── PASO 7: Configuration data (raw blob para cada velocidad) ────────────
    // Config descriptor + Interface descriptor + 3 Endpoint descriptors
    u8 blob[] = {
        // Configuration descriptor (9 bytes)
        0x09, 0x02, 0x27, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
        // Interface descriptor (9 bytes)
        0x09, 0x04, 0x00, 0x00, 0x03, 0x06, 0x01, 0x01, 0x00,
        // Endpoint Bulk OUT (7 bytes)
        0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00,
        // Endpoint Bulk IN  (7 bytes)
        0x07, 0x05, 0x81, 0x02, 0x00, 0x02, 0x00,
        // Endpoint Interrupt IN (7 bytes)
        0x07, 0x05, 0x83, 0x03, 0x1C, 0x00, 0x04
    };
    size_t blobLen = sizeof(blob);

    if (R_FAILED(rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Full, blob, blobLen))) {
        mlog(std::string("MTP cfgFS ERR ") + rcStr(rc)); teardown(); return false;
    }
    if (R_FAILED(rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_High, blob, blobLen))) {
        mlog(std::string("MTP cfgHS ERR ") + rcStr(rc)); teardown(); return false;
    }

    // ── PASO 8: Enable (solo fw < 5.0; en fw >= 5.0 es automático) ───────────
    if (hosversionBefore(5, 0, 0)) {
        if (R_FAILED(rc = usbDsEnable())) {
            mlog(std::string("MTP enable ERR ") + rcStr(rc)); teardown(); return false;
        }
        mlog("MTP usbDsEnable OK (fw < 5.0)");
    } else {
        mlog("MTP fw >= 5.0: enable automatico tras AppendConfigurationData");
    }

    g_ready = true;
    mlog("MTP listo. Conecta el cable USB-C al PC.");
    mlog("Windows: 'Ver archivos del dispositivo'. macOS: Android File Transfer.");
    return true;
}

// Sondeo no-bloqueante: registra paquetes setup nuevos (diagnóstico).
inline void poll() {
    if (!g_ready || !g_iface) return;
    u8 buf[32];
    memset(buf, 0, sizeof(buf));
    if (R_FAILED(usbDsInterface_GetSetupPacket(g_iface, buf, sizeof(buf)))) return;
    bool empty = true;
    for (size_t i = 0; i < sizeof(buf); i++) if (buf[i]) { empty = false; break; }
    if (empty) return;
    if (g_haveLastSetup && memcmp(buf, g_lastSetup, sizeof(buf)) == 0) return;
    memcpy(g_lastSetup, buf, sizeof(buf));
    g_haveLastSetup = true;
    char msg[96];
    snprintf(msg, sizeof(msg), "MTP setup %02X %02X v%02X%02X i%02X%02X l%d",
             buf[0], buf[1], buf[3], buf[2], buf[5], buf[4], buf[6] | (buf[7] << 8));
    mlog(msg);
}

} // namespace mtp_usb
