#pragma once
// Andamiaje USB-MTP experimental (enumeración como dispositivo PTP/MTP).
//
// Estado honesto: registra interfaz usbDs clase Imagen (6/1/1, protocolo PIMA)
// con endpoints bulk + interrupt y la habilita, para que el PC enumere el
// equipo como dispositivo MTP/portátil. El bucle de operaciones MTP sobre
// bulk (transferencias reales) queda pendiente de validación en hardware:
// sin probar endpoint por endpoint en consola física no se puede garantizar.
// Todo es no-bloqueante y con Result verificado: si algo falla, se registra
// en la bitácora y se revierte, sin colgar la app.
#include <switch.h>
#include <cstring>
#include <string>
#include <cstdio>

namespace mtp_usb {

inline void (*g_logCb)(const char* msg) = nullptr;
inline void mlog(const std::string& s) {
    if (g_logCb) g_logCb(s.c_str());
}

inline UsbDsInterface* g_iface = nullptr;
inline UsbDsEndpoint* g_epBulkIn = nullptr;
inline UsbDsEndpoint* g_epBulkOut = nullptr;
inline UsbDsEndpoint* g_epIntrIn = nullptr;
inline bool g_ready = false;
inline u8 g_lastSetup[32] = {0};
inline bool g_haveLastSetup = false;

inline const char* stepName(Result rc) {
    static char b[16];
    snprintf(b, sizeof(b), "rc=0x%X", rc);
    return b;
}

// Descriptor de configuración crudo: config + interfaz PTP/MTP + 3 endpoints.
inline void buildConfigBlob(u8* out, size_t* outLen) {
    // Total = 9 + 9 + 7*3 = 39
    u8 cfg[] = {
        0x09, 0x02, 0x27, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32, // config (100mA)
        0x09, 0x04, 0x00, 0x00, 0x03, 0x06, 0x01, 0x01, 0x00, // if: Image/Still/PTP
        0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00,             // EP bulk OUT
        0x07, 0x05, 0x81, 0x02, 0x00, 0x02, 0x00,             // EP bulk IN
        0x07, 0x05, 0x83, 0x03, 0x40, 0x00, 0x0A              // EP interrupt IN
    };
    memcpy(out, cfg, sizeof(cfg));
    *outLen = sizeof(cfg);
}

inline void teardown() {
    if (g_epBulkIn) { usbDsEndpoint_Close(g_epBulkIn); g_epBulkIn = nullptr; }
    if (g_epBulkOut) { usbDsEndpoint_Close(g_epBulkOut); g_epBulkOut = nullptr; }
    if (g_epIntrIn) { usbDsEndpoint_Close(g_epIntrIn); g_epIntrIn = nullptr; }
    if (g_iface) { usbDsInterface_Close(g_iface); g_iface = nullptr; }
    g_ready = false;
    g_haveLastSetup = false;
}

// Registra y habilita la interfaz. No bloquea: devuelve true si quedó lista.
inline bool setup() {
    teardown();

    UsbDsDeviceInfo devInfo{0x057E, 0x3002, 0x0100};
    Result rc = usbDsSetVidPidBcd(&devInfo);
    if (R_FAILED(rc)) { mlog(std::string("MTP VidPid ERR ") + stepName(rc)); return false; }

    u8 iMan = 0, iProd = 0, iSer = 0;
    if (R_FAILED(rc = usbDsAddUsbStringDescriptor(&iMan, "EzFiles"))) { mlog("MTP strMan ERR"); return false; }
    if (R_FAILED(rc = usbDsAddUsbStringDescriptor(&iProd, "EzFiles MTP"))) { mlog("MTP strProd ERR"); return false; }
    if (R_FAILED(rc = usbDsAddUsbStringDescriptor(&iSer, "EZF000000001"))) { mlog("MTP strSer ERR"); return false; }

    struct usb_device_descriptor devDesc;
    memset(&devDesc, 0, sizeof(devDesc));
    devDesc.bLength = USB_DT_DEVICE_SIZE;
    devDesc.bDescriptorType = USB_DT_DEVICE;
    devDesc.bcdUSB = 0x0200;
    devDesc.bDeviceClass = 0;
    devDesc.bDeviceSubClass = 0;
    devDesc.bDeviceProtocol = 0;
    devDesc.bMaxPacketSize0 = 64;
    devDesc.idVendor = 0x057E;
    devDesc.idProduct = 0x3002;
    devDesc.bcdDevice = 0x0100;
    devDesc.iManufacturer = iMan;
    devDesc.iProduct = iProd;
    devDesc.iSerialNumber = iSer;
    devDesc.bNumConfigurations = 1;
    if (R_FAILED(rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &devDesc))) { mlog("MTP devDesc ERR"); return false; }
    if (R_FAILED(rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &devDesc))) { mlog("MTP devDescHS ERR"); return false; }

    struct usb_interface_descriptor ifDesc;
    memset(&ifDesc, 0, sizeof(ifDesc));
    ifDesc.bLength = USB_DT_INTERFACE_SIZE;
    ifDesc.bDescriptorType = USB_DT_INTERFACE;
    ifDesc.bInterfaceNumber = USBDS_DEFAULT_InterfaceNumber;
    ifDesc.bAlternateSetting = 0;
    ifDesc.bNumEndpoints = 3;
    ifDesc.bInterfaceClass = 6;   // Image
    ifDesc.bInterfaceSubClass = 1; // Still Image Capture
    ifDesc.bInterfaceProtocol = 1; // PIMA 15470 (MTP/PTP)
    ifDesc.iInterface = 0;
    if (R_FAILED(rc = usbDsGetDsInterface(&g_iface, &ifDesc, "EzFiles-MTP"))) {
        mlog(std::string("MTP iface ERR ") + stepName(rc));
        g_iface = nullptr;
        return false;
    }

    struct usb_endpoint_descriptor epBulkOut, epBulkIn, epIntr;
    memset(&epBulkOut, 0, sizeof(epBulkOut));
    epBulkOut.bLength = USB_DT_ENDPOINT_SIZE;
    epBulkOut.bDescriptorType = USB_DT_ENDPOINT;
    epBulkOut.bEndpointAddress = USB_ENDPOINT_OUT;
    epBulkOut.bmAttributes = USB_TRANSFER_TYPE_BULK;
    epBulkOut.wMaxPacketSize = 512;
    memset(&epBulkIn, 0, sizeof(epBulkIn));
    epBulkIn.bLength = USB_DT_ENDPOINT_SIZE;
    epBulkIn.bDescriptorType = USB_DT_ENDPOINT;
    epBulkIn.bEndpointAddress = USB_ENDPOINT_IN;
    epBulkIn.bmAttributes = USB_TRANSFER_TYPE_BULK;
    epBulkIn.wMaxPacketSize = 512;
    memset(&epIntr, 0, sizeof(epIntr));
    epIntr.bLength = USB_DT_ENDPOINT_SIZE;
    epIntr.bDescriptorType = USB_DT_ENDPOINT;
    epIntr.bEndpointAddress = USB_ENDPOINT_IN;
    epIntr.bmAttributes = USB_TRANSFER_TYPE_INTERRUPT;
    epIntr.wMaxPacketSize = 64;
    epIntr.bInterval = 10;
    if (R_FAILED(rc = usbDsInterface_GetDsEndpoint(g_iface, &g_epBulkOut, &epBulkOut))) { mlog("MTP epOUT ERR"); teardown(); return false; }
    if (R_FAILED(rc = usbDsInterface_GetDsEndpoint(g_iface, &g_epBulkIn, &epBulkIn))) { mlog("MTP epIN ERR"); teardown(); return false; }
    if (R_FAILED(rc = usbDsInterface_GetDsEndpoint(g_iface, &g_epIntrIn, &epIntr))) { mlog("MTP epINTR ERR"); teardown(); return false; }

    u8 blob[64]; size_t blobLen = 0;
    buildConfigBlob(blob, &blobLen);
    if (R_FAILED(rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Full, blob, blobLen))) { mlog("MTP cfgFS ERR"); teardown(); return false; }
    if (R_FAILED(rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_High, blob, blobLen))) { mlog("MTP cfgHS ERR"); teardown(); return false; }

    if (R_FAILED(rc = usbDsEnable())) { mlog(std::string("MTP enable ERR ") + stepName(rc)); teardown(); return false; }

    g_ready = true;
    mlog("MTP interfaz OK (clase 6/1/1) - conecta el USB al PC");
    return true;
}

// Sondeo no-bloqueante: registra paquetes setup nuevos (diagnóstico).
inline void poll() {
    if (!g_ready || !g_iface) return;
    u8 buf[32];
    memset(buf, 0, sizeof(buf));
    Result rc = usbDsInterface_GetSetupPacket(g_iface, buf, sizeof(buf));
    if (R_FAILED(rc)) return;
    // Ignorar paquetes vacíos repetidos
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
