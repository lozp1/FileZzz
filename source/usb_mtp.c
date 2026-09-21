/*
 * usb_mtp.c — Driver USB MTP para Nintendo Switch (libnx 5.0+, fw 22.x)
 *
 * Usa Microsoft OS 2.0 Descriptor Set (BOS Platform Capability) para que
 * Windows cargue MTBClassDriver/WPD sin intervención del usuario.
 *
 * Flujo con Windows:
 *   1. usbDsEnable() → Windows lee BOS → ve Platform Capability MS OS 2.0
 *   2. Windows emite vendor GET (bmRequestType=0xC0, bRequest=0x01, wIndex=0x07)
 *   3. SetupEvent dispara → ctrlThread responde con descriptor set (CompatID="MTP")
 *   4. Windows carga MTBClassDriver → envía GetDeviceInfo al worker bulk
 */

#include <string.h>
#include <malloc.h>
#include "usb_mtp.h"

/* ─── Microsoft OS 2.0 Descriptor Set ─────────────────────────────────────
 * Tamaño total: 30 bytes (0x001E)
 * Referencia: https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/
 *             microsoft-defined-usb-descriptors
 * ───────────────────────────────────────────────────────────────────────── */
static const u8 s_ms_os20_desc_set[30] = {
    /* MS OS 2.0 Set Header Descriptor (10 bytes) */
    0x0A, 0x00,              /* wLength = 10 */
    0x00, 0x00,              /* wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR */
    0x00, 0x00, 0x03, 0x06,  /* dwWindowsVersion = 0x06030000 (Windows 8.1+) */
    0x1E, 0x00,              /* wTotalLength = 30 */

    /* MS OS 2.0 Compatible ID Descriptor (20 bytes) */
    0x14, 0x00,              /* wLength = 20 */
    0x03, 0x00,              /* wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID */
    'M', 'T', 'P', ' ', ' ', ' ', ' ', ' ',   /* CompatibleID "MTP     " */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 /* SubCompatibleID = empty */
};
#define MS_VENDOR_CODE 0x01

/* ─── BOS con MS OS 2.0 Platform Capability ───────────────────────────────
 * USB 2.0 Extension (7) + SuperSpeed (10) + MS OS 2.0 Platform (28) = 50 bytes
 * ───────────────────────────────────────────────────────────────────────── */
static const u8 s_bos[50] = {
    /* BOS Header (5 bytes) */
    0x05, 0x0F,        /* bLength=5, bDescriptorType=BOS */
    0x32, 0x00,        /* wTotalLength = 50 */
    0x03,              /* bNumDeviceCaps = 3 */

    /* USB 2.0 Extension (7 bytes) */
    0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,

    /* SuperSpeed Device Capability (10 bytes) */
    0x0A, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00,

    /* Microsoft OS 2.0 Platform Capability Descriptor (28 bytes)
     * UUID: D8DD60DF-4589-4CC7-9CD2-659D9E648A9F (little-endian) */
    0x1C,              /* bLength = 28 */
    0x10,              /* bDescriptorType = Device Capability */
    0x05,              /* bDevCapabilityType = Platform */
    0x00,              /* bReserved */
    /* PlatformCapabilityUUID (GUID little-endian) */
    0xDF, 0x60, 0xDD, 0xD8,
    0x89, 0x45,
    0xC7, 0x4C,
    0x9C, 0xD2,
    0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
    /* CapabilityData */
    0x00, 0x00, 0x03, 0x06,  /* dwWindowsVersion = Windows 8.1+ */
    0x1E, 0x00,              /* wMSOSDescriptorSetTotalLength = 30 */
    MS_VENDOR_CODE,          /* bMS_VendorCode */
    0x00                     /* bAltEnumCode = 0 */
};

/* ─── Endpoint structure ───────────────────────────────────────────────── */
#define TOTAL_ENDPOINTS 3

typedef struct {
    UsbDsEndpoint *endpoint;
    u8            *buffer;
    RwLock         lock;
} usbMtpEndpoint;

static bool            g_mtpInitialized = false;
static UsbDsInterface *g_mtpInterface   = NULL;
static usbMtpEndpoint  g_mtpEndpoints[TOTAL_ENDPOINTS];
static RwLock          g_mtpLock;

/* ─── Control thread (MS OS 2.0 vendor request handler) ─────────────────── */
static Thread  g_ctrlThread;
static bool    g_ctrlRunning = false;

static void ctrlThreadFunc(void *arg)
{
    (void)arg;
    u8 *buf = (u8*)memalign(0x1000, 0x1000);
    if (!buf) return;

    while (g_ctrlRunning) {
        /* Esperar setup packet (100 ms) */
        if (R_FAILED(eventWait(&g_mtpInterface->SetupEvent, 100000000ULL))) continue;
        eventClear(&g_mtpInterface->SetupEvent);
        if (!g_ctrlRunning) break;

        /* Leer el setup packet (8 bytes USB spec) */
        struct {
            u8  bmRequestType;
            u8  bRequest;
            u16 wValue;
            u16 wIndex;
            u16 wLength;
        } pkt;
        if (R_FAILED(usbDsInterface_GetSetupPacket(g_mtpInterface, &pkt, sizeof(pkt)))) {
            usbDsInterface_StallCtrl(g_mtpInterface);
            continue;
        }

        /* Vendor IN → Device: MS OS 2.0 descriptor request
         * bmRequestType = 0xC0, bRequest = MS_VENDOR_CODE, wIndex = 0x0007 */
        if (pkt.bmRequestType == 0xC0 &&
            pkt.bRequest      == MS_VENDOR_CODE &&
            pkt.wIndex        == 0x0007)
        {
            u16 sendLen = (u16)sizeof(s_ms_os20_desc_set);
            if (pkt.wLength < sendLen) sendLen = pkt.wLength;
            memcpy(buf, s_ms_os20_desc_set, sendLen);
            u32 urbId;
            if (R_SUCCEEDED(usbDsInterface_CtrlInPostBufferAsync(g_mtpInterface, buf, sendLen, &urbId))) {
                eventWait(&g_mtpInterface->CtrlInCompletionEvent, 2000000000ULL);
                eventClear(&g_mtpInterface->CtrlInCompletionEvent);
            }
            continue;
        }

        /* Cualquier otra petición de control no reconocida → STALL */
        usbDsInterface_StallCtrl(g_mtpInterface);
    }

    free(buf);
}

/* ─── usbMtpInitialize ──────────────────────────────────────────────────── */
Result usbMtpInitialize(void)
{
    Result rc = 0;
    rwlockWriteLock(&g_mtpLock);

    if (g_mtpInitialized) { rwlockWriteUnlock(&g_mtpLock); return 0; }

    memset(g_mtpEndpoints, 0, sizeof(g_mtpEndpoints));
    for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) rwlockInit(&g_mtpEndpoints[i].lock);

    rc = usbDsInitialize();
    if (R_FAILED(rc)) { rwlockWriteUnlock(&g_mtpLock); return rc; }

    /* Strings */
    u8 iMan = 0, iProd = 0, iSer = 0;
    static const u16 langs[1] = {0x0409};
    rc = usbDsAddUsbLanguageStringDescriptor(NULL, langs, 1);
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iMan,  "Nintendo");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iProd, "Nintendo Switch");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iSer,  "000000000001");

    /* Device descriptor base */
    struct usb_device_descriptor dev = {
        .bLength            = USB_DT_DEVICE_SIZE,
        .bDescriptorType    = USB_DT_DEVICE,
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = 0x40,
        .idVendor           = 0x0955,   /* NVIDIA/Android — compatible con MTBClassDriver */
        .idProduct          = 0x7321,
        .bcdDevice          = 0x0100,
        .iManufacturer      = iMan,
        .iProduct           = iProd,
        .iSerialNumber      = iSer,
        .bNumConfigurations = 0x01
    };

    dev.bcdUSB = 0x0110; dev.bMaxPacketSize0 = 0x40;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &dev);
    dev.bcdUSB = 0x0200; dev.bMaxPacketSize0 = 0x40;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &dev);
    dev.bcdUSB = 0x0300; dev.bMaxPacketSize0 = 0x09;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &dev);

    /* BOS con MS OS 2.0 Platform Capability */
    if (R_SUCCEEDED(rc)) rc = usbDsSetBinaryObjectStore(s_bos, sizeof(s_bos));

    /* Interfaz: clase 0xFF Vendor (igual que Android MTP / DBI) */
    struct usb_interface_descriptor intf = {
        .bLength            = USB_DT_INTERFACE_SIZE,
        .bDescriptorType    = USB_DT_INTERFACE,
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 3,
        .bInterfaceClass    = 0xFF,
        .bInterfaceSubClass = 0xFF,
        .bInterfaceProtocol = 0x00,
        .iInterface         = 0
    };

    /* Endpoints: Bulk IN, Bulk OUT, Interrupt IN */
    struct usb_endpoint_descriptor ep_in = {
        .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,  .bmAttributes = USB_TRANSFER_TYPE_BULK, .wMaxPacketSize = 0x40
    };
    struct usb_endpoint_descriptor ep_out = {
        .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_OUT, .bmAttributes = USB_TRANSFER_TYPE_BULK, .wMaxPacketSize = 0x40
    };
    struct usb_endpoint_descriptor ep_int = {
        .bLength = USB_DT_ENDPOINT_SIZE, .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,  .bmAttributes = USB_TRANSFER_TYPE_INTERRUPT,
        .wMaxPacketSize = 0x1c, .bInterval = 6
    };
    struct usb_endpoint_descriptor *eps[3] = { &ep_in, &ep_out, &ep_int };

    struct usb_ss_endpoint_companion_descriptor comp_bulk = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE, .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F, .bmAttributes = 0x00, .wBytesPerInterval = 0x00
    };
    struct usb_ss_endpoint_companion_descriptor comp_int = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE, .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x00, .bmAttributes = 0x00, .wBytesPerInterval = 0x1c
    };

    /* Buferes alineados 4 KB */
    for (u32 i = 0; i < TOTAL_ENDPOINTS && R_SUCCEEDED(rc); i++) {
        g_mtpEndpoints[i].buffer = (u8*)memalign(0x1000, 0x1000);
        if (!g_mtpEndpoints[i].buffer) { rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory); break; }
        memset(g_mtpEndpoints[i].buffer, 0, 0x1000);
    }

    if (R_SUCCEEDED(rc)) rc = usbDsRegisterInterface(&g_mtpInterface);

    if (R_SUCCEEDED(rc)) {
        intf.bInterfaceNumber = g_mtpInterface->interface_index;

        /* Asignar endpoint addresses */
        int ep_in_num = 1, ep_out_num = 1;
        for (u32 i = 0; i < 3; i++) {
            if (eps[i]->bEndpointAddress & USB_ENDPOINT_IN)
                eps[i]->bEndpointAddress = USB_ENDPOINT_IN | ep_in_num++;
            else
                eps[i]->bEndpointAddress = ep_out_num++;
        }

        /* Full Speed */
        rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Full, &intf, USB_DT_INTERFACE_SIZE);
        for (u32 i = 0; R_SUCCEEDED(rc) && i < 3; i++) {
            struct usb_endpoint_descriptor d = *eps[i];
            if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) d.wMaxPacketSize = 0x40;
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Full, &d, USB_DT_ENDPOINT_SIZE);
        }
        /* High Speed */
        if (R_SUCCEEDED(rc)) {
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &intf, USB_DT_INTERFACE_SIZE);
            for (u32 i = 0; R_SUCCEEDED(rc) && i < 3; i++) {
                struct usb_endpoint_descriptor d = *eps[i];
                if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) d.wMaxPacketSize = 0x200;
                rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &d, USB_DT_ENDPOINT_SIZE);
            }
        }
        /* Super Speed */
        if (R_SUCCEEDED(rc)) {
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &intf, USB_DT_INTERFACE_SIZE);
            for (u32 i = 0; R_SUCCEEDED(rc) && i < 3; i++) {
                struct usb_endpoint_descriptor d = *eps[i];
                struct usb_ss_endpoint_companion_descriptor *comp;
                if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) { d.wMaxPacketSize = 0x400; comp = &comp_bulk; }
                else comp = &comp_int;
                rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &d, USB_DT_ENDPOINT_SIZE);
                if (R_SUCCEEDED(rc))
                    rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, comp, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
            }
        }

        /* Registrar endpoints */
        for (u32 i = 0; R_SUCCEEDED(rc) && i < 3; i++)
            rc = usbDsInterface_RegisterEndpoint(g_mtpInterface, &g_mtpEndpoints[i].endpoint, eps[i]->bEndpointAddress);

        if (R_SUCCEEDED(rc)) rc = usbDsInterface_EnableInterface(g_mtpInterface);
        if (R_SUCCEEDED(rc)) rc = usbDsEnable();
    }

    if (R_SUCCEEDED(rc)) {
        /* Iniciar control thread para MS OS 2.0 vendor requests */
        g_ctrlRunning = true;
        if (R_SUCCEEDED(threadCreate(&g_ctrlThread, ctrlThreadFunc, NULL, NULL, 0x4000, 0x2C, -2)))
            threadStart(&g_ctrlThread);
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

    g_ctrlRunning = false;
    threadWaitForExit(&g_ctrlThread);
    threadClose(&g_ctrlThread);

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

    /* Esperar a que USB esté en estado Configured antes de cada transferencia
     * (igual que retronx — sobrevive reconexiones por instalación de driver) */
    if (R_FAILED(usbDsWaitReady(5000000000ULL))) { /* 5 s max */
        rwlockWriteUnlock(&ep->lock);
        return 0;
    }

    Result rc = 0;
    u32 urbId = 0, chunksize = 0;
    u8 transfer_type = 0;
    u8 *bufptr = (u8*)buffer, *transfer_buffer = NULL;
    u32 tmp_sz = 0;
    size_t total = 0;
    UsbDsReportData reportdata;

    while (size > 0) {
        if (((u64)bufptr) & 0xfff) {
            transfer_buffer = ep->buffer;
            memset(ep->buffer, 0, 0x1000);
            chunksize = (u32)(0x1000 - (((u64)bufptr) & 0xfff));
            if ((u32)size < chunksize) chunksize = (u32)size;
            if (isWrite) memcpy(ep->buffer, bufptr, chunksize);
            transfer_type = 0;
        } else {
            transfer_buffer = bufptr;
            chunksize = (u32)size;
            if (chunksize > 0x1000) chunksize = 0x1000;
            transfer_type = 1;
        }

        eventClear(&ep->endpoint->CompletionEvent);
        rc = usbDsEndpoint_PostBufferAsync(ep->endpoint, transfer_buffer, chunksize, &urbId);
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
        if (transfer_type == 0 && !isWrite) memcpy(bufptr, transfer_buffer, tmp_sz);
        bufptr += tmp_sz;
        size   -= tmp_sz;
        if (tmp_sz < chunksize) break;
    }

    rwlockWriteUnlock(&ep->lock);
    return total;
}
