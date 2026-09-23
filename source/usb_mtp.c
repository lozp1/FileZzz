/*
 * usb_mtp.c — Driver USB MTP para Nintendo Switch (libnx 5.0+)
 * Basado en la arquitectura oficial de Atmosphère Haze (troposphere/haze).
 */

#include <string.h>
#include <malloc.h>
#include "usb_mtp.h"

#define TOTAL_ENDPOINTS 3
#define MTP_TRANSFER_BUFFER_SIZE 0x10000 // 64 KB DMA buffer

typedef struct {
    UsbDsEndpoint *endpoint;
    u8            *buffer;
    RwLock         lock;
} usbMtpEndpoint;

static bool            g_mtpInitialized = false;
static UsbDsInterface *g_mtpInterface   = NULL;
static usbMtpEndpoint  g_mtpEndpoints[TOTAL_ENDPOINTS];
static RwLock          g_mtpLock;

Result usbMtpInitialize(void)
{
    Result rc = 0;
    rwlockWriteLock(&g_mtpLock);

    if (g_mtpInitialized) { rwlockWriteUnlock(&g_mtpLock); return 0; }

    memset(g_mtpEndpoints, 0, sizeof(g_mtpEndpoints));
    for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) rwlockInit(&g_mtpEndpoints[i].lock);

    rc = usbDsInitialize();
    if (R_FAILED(rc)) { rwlockWriteUnlock(&g_mtpLock); return rc; }

    /* Strings de identificación del dispositivo (idéntico a Haze) */
    u8 iMan = 0, iProd = 0, iSer = 0, iInterface = 0;
    static const u16 langs[1] = {0x0409};
    rc = usbDsAddUsbLanguageStringDescriptor(NULL, langs, 1);
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iMan,  "Nintendo");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iProd, "Nintendo Switch");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iSer,  "000000000001");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iInterface, "MTP");

    /* Device descriptor oficial Haze: 0x057E : 0x201D (Nintendo Switch MTP) */
    struct usb_device_descriptor dev = {
        .bLength            = USB_DT_DEVICE_SIZE,
        .bDescriptorType    = USB_DT_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = 0x40,
        .idVendor           = 0x057E, /* Nintendo VID */
        .idProduct          = 0x201D, /* Switch MTP standard PID reconocido por Windows */
        .bcdDevice          = 0x0100,
        .iManufacturer      = iMan,
        .iProduct           = iProd,
        .iSerialNumber      = iSer,
        .bNumConfigurations = 0x01
    };

    /* High Speed (USB 2.0) */
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &dev);

    /* Super Speed (USB 3.0) */
    dev.bcdUSB = 0x0300;
    dev.bMaxPacketSize0 = 0x09;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &dev);

    /* BOS oficial de Haze (22 bytes, USB 2.0 + SuperSpeed con wSpeedSupported=0x000C) */
    u8 bos[0x16] = {
        0x05, USB_DT_BOS, 0x16, 0x00, 0x02,
        0x07, USB_DT_DEVICE_CAPABILITY, 0x02, 0x02, 0x00, 0x00, 0x00,
        0x0A, USB_DT_DEVICE_CAPABILITY, 0x03, 0x00, 0x0C, 0x00, 0x03, 0x00, 0x00, 0x00
    };
    if (R_SUCCEEDED(rc)) rc = usbDsSetBinaryObjectStore(bos, sizeof(bos));

    /* Interfaz MTP: Clase 6 (Still Image), Subclase 1, Protocolo 1 */
    struct usb_interface_descriptor intf = {
        .bLength            = USB_DT_INTERFACE_SIZE,
        .bDescriptorType    = USB_DT_INTERFACE,
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 3,
        .bInterfaceClass    = 6,    /* Still Image (PTP/MTP) */
        .bInterfaceSubClass = 1,
        .bInterfaceProtocol = 1,
        .iInterface         = iInterface /* Asignar nombre "MTP" a la interfaz */
    };

    /* Endpoints base */
    struct usb_endpoint_descriptor ep_in = {
        .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,  .bmAttributes = USB_TRANSFER_TYPE_BULK, .wMaxPacketSize = 0x200
    };
    struct usb_endpoint_descriptor ep_out = {
        .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_OUT, .bmAttributes = USB_TRANSFER_TYPE_BULK, .wMaxPacketSize = 0x200
    };
    struct usb_endpoint_descriptor ep_int = {
        .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,  .bmAttributes = USB_TRANSFER_TYPE_INTERRUPT,
        .wMaxPacketSize = 0x18, .bInterval = 6
    };

    struct usb_ss_endpoint_companion_descriptor comp_bulk = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE, .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F, .bmAttributes = 0x00, .wBytesPerInterval = 0x00
    };
    struct usb_ss_endpoint_companion_descriptor comp_int = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE, .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x00, .bmAttributes = 0x00, .wBytesPerInterval = 0x00
    };

    /* Buferes de transferencia de 64KB alineados para DMA de alta velocidad */
    for (u32 i = 0; i < TOTAL_ENDPOINTS && R_SUCCEEDED(rc); i++) {
        g_mtpEndpoints[i].buffer = (u8*)memalign(0x1000, MTP_TRANSFER_BUFFER_SIZE);
        if (!g_mtpEndpoints[i].buffer) { rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory); break; }
        memset(g_mtpEndpoints[i].buffer, 0, MTP_TRANSFER_BUFFER_SIZE);
    }

    if (R_SUCCEEDED(rc)) rc = usbDsRegisterInterface(&g_mtpInterface);

    if (R_SUCCEEDED(rc)) {
        intf.bInterfaceNumber = g_mtpInterface->interface_index;

        /* Asignación de direcciones de endpoint (idéntico a Haze) */
        ep_in.bEndpointAddress  += intf.bInterfaceNumber + 1; // 0x81
        ep_out.bEndpointAddress += intf.bInterfaceNumber + 1; // 0x01
        ep_int.bEndpointAddress += intf.bInterfaceNumber + 2; // 0x82

        /* Configuración High Speed (USB 2.0) */
        ep_in.wMaxPacketSize  = 0x200;
        ep_out.wMaxPacketSize = 0x200;
        rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &intf, USB_DT_INTERFACE_SIZE);
        if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &ep_in, USB_DT_ENDPOINT_SIZE);
        if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &ep_out, USB_DT_ENDPOINT_SIZE);
        if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &ep_int, USB_DT_ENDPOINT_SIZE);

        /* Configuración Super Speed (USB 3.0) */
        if (R_SUCCEEDED(rc)) {
            ep_in.wMaxPacketSize  = 0x400;
            ep_out.wMaxPacketSize = 0x400;
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &intf, USB_DT_INTERFACE_SIZE);
            if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &ep_in, USB_DT_ENDPOINT_SIZE);
            if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &comp_bulk, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
            if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &ep_out, USB_DT_ENDPOINT_SIZE);
            if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &comp_bulk, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
            if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &ep_int, USB_DT_ENDPOINT_SIZE);
            if (R_SUCCEEDED(rc)) rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &comp_int, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
        }

        /* Registrar endpoints */
        if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(g_mtpInterface, &g_mtpEndpoints[MTP_EP_BULK_IN].endpoint, ep_in.bEndpointAddress);
        if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(g_mtpInterface, &g_mtpEndpoints[MTP_EP_BULK_OUT].endpoint, ep_out.bEndpointAddress);
        if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(g_mtpInterface, &g_mtpEndpoints[MTP_EP_INTERRUPT].endpoint, ep_int.bEndpointAddress);

        if (R_SUCCEEDED(rc)) rc = usbDsInterface_EnableInterface(g_mtpInterface);
        if (R_SUCCEEDED(rc)) rc = usbDsEnable();
    }

    if (R_SUCCEEDED(rc)) {
        g_mtpInitialized = true;
    } else {
        for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) {
            if (g_mtpEndpoints[i].buffer) { free(g_mtpEndpoints[i].buffer); g_mtpEndpoints[i].buffer = NULL; }
            g_mtpEndpoints[i].endpoint = NULL;
        }
        if (g_mtpInterface) { usbDsInterface_Close(g_mtpInterface); g_mtpInterface = NULL; }
        usbDsExit();
    }

    rwlockWriteUnlock(&g_mtpLock);
    return rc;
}

void usbMtpExit(void)
{
    rwlockWriteLock(&g_mtpLock);
    if (!g_mtpInitialized) { rwlockWriteUnlock(&g_mtpLock); return; }
    g_mtpInitialized = false;

    for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) {
        if (g_mtpEndpoints[i].endpoint) usbDsEndpoint_Cancel(g_mtpEndpoints[i].endpoint);
        rwlockWriteLock(&g_mtpEndpoints[i].lock);
        if (g_mtpEndpoints[i].buffer) { free(g_mtpEndpoints[i].buffer); g_mtpEndpoints[i].buffer = NULL; }
        g_mtpEndpoints[i].endpoint = NULL;
        rwlockWriteUnlock(&g_mtpEndpoints[i].lock);
    }
    if (g_mtpInterface) { usbDsInterface_Close(g_mtpInterface); g_mtpInterface = NULL; }
    usbDsExit();
    rwlockWriteUnlock(&g_mtpLock);
}

bool usbMtpIsActive(void)
{
    rwlockReadLock(&g_mtpLock);
    bool a = g_mtpInitialized;
    rwlockReadUnlock(&g_mtpLock);
    return a;
}

Result usbMtpWaitReady(u64 timeout_ns)
{
    if (!g_mtpInitialized) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    return usbDsWaitReady(timeout_ns);
}

size_t usbMtpTransfer(u32 endpoint, int isWrite, void* buffer, size_t size, u64 timeout_ns)
{
    if (endpoint >= TOTAL_ENDPOINTS || !g_mtpInitialized) return 0;

    usbMtpEndpoint *ep = &g_mtpEndpoints[endpoint];
    rwlockWriteLock(&ep->lock);

    if (!ep->endpoint || !ep->buffer) { rwlockWriteUnlock(&ep->lock); return 0; }

    Result rc = 0;
    u32 urbId = 0, chunksize = 0;
    u8 *bufptr = (u8*)buffer;
    u32 tmp_sz = 0;
    size_t total = 0;
    UsbDsReportData reportdata;

    while (size > 0) {
        chunksize = (u32)size;
        if (chunksize > MTP_TRANSFER_BUFFER_SIZE) chunksize = MTP_TRANSFER_BUFFER_SIZE;

        if (isWrite) {
            memcpy(ep->buffer, bufptr, chunksize);
        }

        eventClear(&ep->endpoint->CompletionEvent);
        rc = usbDsEndpoint_PostBufferAsync(ep->endpoint, ep->buffer, chunksize, &urbId);
        if (R_FAILED(rc)) break;

        rc = eventWait(&ep->endpoint->CompletionEvent, timeout_ns);
        if (R_FAILED(rc)) {
            usbDsEndpoint_Cancel(ep->endpoint);
            eventWait(&ep->endpoint->CompletionEvent, UINT64_MAX);
            eventClear(&ep->endpoint->CompletionEvent);
            break;
        }
        eventClear(&ep->endpoint->CompletionEvent);

        rc = usbDsEndpoint_GetReportData(ep->endpoint, &reportdata);
        if (R_FAILED(rc)) break;
        rc = usbDsParseReportData(&reportdata, urbId, NULL, &tmp_sz);
        if (R_FAILED(rc)) break;

        if (tmp_sz > chunksize) tmp_sz = chunksize;
        total += (size_t)tmp_sz;
        if (!isWrite) {
            memcpy(bufptr, ep->buffer, tmp_sz);
        }
        bufptr += tmp_sz;
        size   -= tmp_sz;
        if (tmp_sz < chunksize) break;
    }

    rwlockWriteUnlock(&ep->lock);
    return total;
}
