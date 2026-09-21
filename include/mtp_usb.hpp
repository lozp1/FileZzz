#pragma once
// USB-DS / MTP: Implementación moderna para firmware >= 5.0.0 (Atmosphere / Horizon)
// Siguiendo la especificación exacta de libnx usb_comms.c
#include <switch.h>
#include <cstring>
#include <string>
#include <cstdio>
#include <malloc.h>

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
inline bool            g_usbInited = false;
inline u8              g_lastSetup[32] = {0};
inline bool            g_haveLastSetup = false;

inline const char* rcStr(Result rc) {
    static char b[32];
    snprintf(b, sizeof(b), "rc=0x%X", rc);
    return b;
}

inline void teardown() {
    g_ready = false;
    if (g_epBulkIn)  { usbDsEndpoint_Close(g_epBulkIn);  g_epBulkIn  = nullptr; }
    if (g_epBulkOut) { usbDsEndpoint_Close(g_epBulkOut); g_epBulkOut = nullptr; }
    if (g_epIntrIn)  { usbDsEndpoint_Close(g_epIntrIn);  g_epIntrIn  = nullptr; }
    if (g_iface)     { usbDsInterface_Close(g_iface);     g_iface     = nullptr; }
    if (g_usbInited) { usbDsExit(); g_usbInited = false; }
    g_haveLastSetup = false;
}

// Inicialización de dispositivo USB MTP (Clase 6/1/1 PIMA 15470)
inline bool setup() {
    teardown();

    Result rc = usbDsInitialize();
    if (R_FAILED(rc)) {
        mlog(std::string("usbDsInit ERR ") + rcStr(rc));
        return false;
    }
    g_usbInited = true;

    // String Descriptors
    u8 iManufacturer = 0, iProduct = 0, iSerialNumber = 0;
    static const u16 supported_langs[1] = {0x0409}; // English (US)
    rc = usbDsAddUsbLanguageStringDescriptor(NULL, supported_langs, 1);
    if (R_FAILED(rc)) { mlog(std::string("langDesc ERR ") + rcStr(rc)); teardown(); return false; }

    rc = usbDsAddUsbStringDescriptor(&iManufacturer, "Nintendo");
    if (R_FAILED(rc)) { mlog(std::string("strMan ERR ") + rcStr(rc)); teardown(); return false; }

    rc = usbDsAddUsbStringDescriptor(&iProduct, "Nintendo Switch");
    if (R_FAILED(rc)) { mlog(std::string("strProd ERR ") + rcStr(rc)); teardown(); return false; }

    rc = usbDsAddUsbStringDescriptor(&iSerialNumber, "000000000001");
    if (R_FAILED(rc)) { mlog(std::string("strSer ERR ") + rcStr(rc)); teardown(); return false; }

    // Device Descriptors (Full, High, Super Speed)
    // 0x057E:0x201D = Nintendo Switch MTP Responder (usado por DBI / NXMTP)
    struct usb_device_descriptor devDesc;
    memset(&devDesc, 0, sizeof(devDesc));
    devDesc.bLength            = USB_DT_DEVICE_SIZE;
    devDesc.bDescriptorType    = USB_DT_DEVICE;
    devDesc.bcdUSB             = 0x0110;
    devDesc.bDeviceClass       = 0x00; // Definido en la interfaz
    devDesc.bDeviceSubClass    = 0x00;
    devDesc.bDeviceProtocol    = 0x00;
    devDesc.bMaxPacketSize0    = 0x40;
    devDesc.idVendor           = 0x057E; // Nintendo
    devDesc.idProduct          = 0x201D; // Switch MTP
    devDesc.bcdDevice          = 0x0100;
    devDesc.iManufacturer      = iManufacturer;
    devDesc.iProduct           = iProduct;
    devDesc.iSerialNumber      = iSerialNumber;
    devDesc.bNumConfigurations = 0x01;

    rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &devDesc);
    if (R_FAILED(rc)) { mlog(std::string("devDescFS ERR ") + rcStr(rc)); teardown(); return false; }

    devDesc.bcdUSB = 0x0200;
    rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &devDesc);
    if (R_FAILED(rc)) { mlog(std::string("devDescHS ERR ") + rcStr(rc)); teardown(); return false; }

    devDesc.bcdUSB = 0x0300;
    devDesc.bMaxPacketSize0 = 0x09;
    rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &devDesc);
    if (R_FAILED(rc)) { mlog(std::string("devDescSS ERR ") + rcStr(rc)); teardown(); return false; }

    // Binary Object Store (BOS)
    u8 bos[0x16] = {
        0x05, USB_DT_BOS, 0x16, 0x00, 0x02,
        0x07, USB_DT_DEVICE_CAPABILITY, 0x02, 0x02, 0x00, 0x00, 0x00,
        0x0A, USB_DT_DEVICE_CAPABILITY, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00
    };
    rc = usbDsSetBinaryObjectStore(bos, sizeof(bos));
    if (R_FAILED(rc)) { mlog(std::string("bos ERR ") + rcStr(rc)); teardown(); return false; }

    // Registrar Interfaz (Modo 5.0.0+)
    rc = usbDsRegisterInterface(&g_iface);
    if (R_FAILED(rc)) { mlog(std::string("regIface ERR ") + rcStr(rc)); teardown(); return false; }

    // Configurar Descriptor de Interfaz MTP (Clase 6: Imagen, Subclase 1: Still, Protocolo 1: PIMA 15470 MTP)
    struct usb_interface_descriptor ifDesc;
    memset(&ifDesc, 0, sizeof(ifDesc));
    ifDesc.bLength            = USB_DT_INTERFACE_SIZE;
    ifDesc.bDescriptorType    = USB_DT_INTERFACE;
    ifDesc.bInterfaceNumber   = g_iface->interface_index;
    ifDesc.bAlternateSetting  = 0;
    ifDesc.bNumEndpoints      = 3;
    ifDesc.bInterfaceClass    = 0x06; // Still Image Capture / MTP
    ifDesc.bInterfaceSubClass = 0x01;
    ifDesc.bInterfaceProtocol = 0x01;
    ifDesc.iInterface         = 0;

    // Descriptores de Endpoints:
    // Bulk IN  = 0x80 | (interface_index + 1) -> ej 0x81
    // Bulk OUT = 0x00 | (interface_index + 1) -> ej 0x01
    // Intr IN  = 0x80 | (interface_index + 2) -> ej 0x82
    u8 epInAddr   = (u8)(USB_ENDPOINT_IN  + g_iface->interface_index + 1);
    u8 epOutAddr  = (u8)(USB_ENDPOINT_OUT + g_iface->interface_index + 1);
    u8 epIntrAddr = (u8)(USB_ENDPOINT_IN  + g_iface->interface_index + 2);

    struct usb_endpoint_descriptor ep_in;
    memset(&ep_in, 0, sizeof(ep_in));
    ep_in.bLength          = USB_DT_ENDPOINT_SIZE;
    ep_in.bDescriptorType  = USB_DT_ENDPOINT;
    ep_in.bEndpointAddress = epInAddr;
    ep_in.bmAttributes     = USB_TRANSFER_TYPE_BULK;
    ep_in.wMaxPacketSize   = 0x40;

    struct usb_endpoint_descriptor ep_out;
    memset(&ep_out, 0, sizeof(ep_out));
    ep_out.bLength          = USB_DT_ENDPOINT_SIZE;
    ep_out.bDescriptorType  = USB_DT_ENDPOINT;
    ep_out.bEndpointAddress = epOutAddr;
    ep_out.bmAttributes     = USB_TRANSFER_TYPE_BULK;
    ep_out.wMaxPacketSize   = 0x40;

    struct usb_endpoint_descriptor ep_intr;
    memset(&ep_intr, 0, sizeof(ep_intr));
    ep_intr.bLength          = USB_DT_ENDPOINT_SIZE;
    ep_intr.bDescriptorType  = USB_DT_ENDPOINT;
    ep_intr.bEndpointAddress = epIntrAddr;
    ep_intr.bmAttributes     = USB_TRANSFER_TYPE_INTERRUPT;
    ep_intr.wMaxPacketSize   = 0x1C;
    ep_intr.bInterval        = 0x04;

    struct usb_ss_endpoint_companion_descriptor ep_comp;
    memset(&ep_comp, 0, sizeof(ep_comp));
    ep_comp.bLength           = sizeof(ep_comp);
    ep_comp.bDescriptorType   = USB_DT_SS_ENDPOINT_COMPANION;
    ep_comp.bMaxBurst         = 0x0F;
    ep_comp.bmAttributes      = 0x00;
    ep_comp.wBytesPerInterval = 0x00;

    // 1. Config Full Speed (USB 1.1)
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Full, &ifDesc, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgFS_if ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Full, &ep_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgFS_in ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Full, &ep_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgFS_out ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Full, &ep_intr, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgFS_intr ERR ") + rcStr(rc)); teardown(); return false; }

    // 2. Config High Speed (USB 2.0)
    ep_in.wMaxPacketSize   = 0x200;
    ep_out.wMaxPacketSize  = 0x200;
    ep_intr.wMaxPacketSize = 0x40;
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_High, &ifDesc, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgHS_if ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_High, &ep_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgHS_in ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_High, &ep_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgHS_out ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_High, &ep_intr, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgHS_intr ERR ") + rcStr(rc)); teardown(); return false; }

    // 3. Config Super Speed (USB 3.0)
    ep_in.wMaxPacketSize   = 0x400;
    ep_out.wMaxPacketSize  = 0x400;
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Super, &ifDesc, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgSS_if ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Super, &ep_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgSS_in ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Super, &ep_comp, sizeof(ep_comp));
    if (R_FAILED(rc)) { mlog(std::string("cfgSS_cin ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Super, &ep_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgSS_out ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Super, &ep_comp, sizeof(ep_comp));
    if (R_FAILED(rc)) { mlog(std::string("cfgSS_cout ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Super, &ep_intr, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc)) { mlog(std::string("cfgSS_intr ERR ") + rcStr(rc)); teardown(); return false; }
    rc = usbDsInterface_AppendConfigurationData(g_iface, UsbDeviceSpeed_Super, &ep_comp, sizeof(ep_comp));
    if (R_FAILED(rc)) { mlog(std::string("cfgSS_cintr ERR ") + rcStr(rc)); teardown(); return false; }

    // Registrar Endpoints
    rc = usbDsInterface_RegisterEndpoint(g_iface, &g_epBulkIn, epInAddr);
    if (R_FAILED(rc)) { mlog(std::string("regEpIn ERR ") + rcStr(rc)); teardown(); return false; }

    rc = usbDsInterface_RegisterEndpoint(g_iface, &g_epBulkOut, epOutAddr);
    if (R_FAILED(rc)) { mlog(std::string("regEpOut ERR ") + rcStr(rc)); teardown(); return false; }

    rc = usbDsInterface_RegisterEndpoint(g_iface, &g_epIntrIn, epIntrAddr);
    if (R_FAILED(rc)) { mlog(std::string("regEpIntr ERR ") + rcStr(rc)); teardown(); return false; }

    // Habilitar Interfaz y USB Device
    rc = usbDsInterface_EnableInterface(g_iface);
    if (R_FAILED(rc)) { mlog(std::string("enIface ERR ") + rcStr(rc)); teardown(); return false; }

    rc = usbDsEnable();
    if (R_FAILED(rc)) { mlog(std::string("usbEn ERR ") + rcStr(rc)); teardown(); return false; }

    g_ready = true;
    mlog("MTP: USB inicializado OK (0x057E:0x201D)");
    mlog("MTP: Listo para transferencias en PC");
    return true;
}

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
