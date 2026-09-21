#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include "usb_mtp.h"

#define TOTAL_ENDPOINTS 3

/* ─── Microsoft OS 1.0 Compatible ID Feature Descriptor ───────────────────
 * Windows envía un vendor GET_DESCRIPTOR al índice 0xEE para obtener la
 * string "MSFT100" + vendor_code.  Luego usa vendor_code para pedir el
 * Extended Compat ID, que le dice que cargue el MTBClassDriver (WPD/MTP).
 * ───────────────────────────────────────────────────────────────────────── */
#define MS_VENDOR_CODE 0x01

/* String descriptor 0xEE — "MSFT100\x01" (MS OS 1.0 signature) */
static const u8 s_msft_string_desc[] = {
    0x12,       /* bLength */
    0x03,       /* bDescriptorType = String */
    'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0,
    MS_VENDOR_CODE,  /* qwSignature[7] = vendor code */
    0x00             /* bPad */
};

/* Extended Compat ID OS Feature Descriptor (40 bytes) */
static const u8 s_compat_id_desc[] = {
    /* Header (16 bytes) */
    0x28, 0x00, 0x00, 0x00,  /* dwLength = 40 */
    0x00, 0x01,              /* bcdVersion = 1.0 */
    0x04, 0x00,              /* wIndex = 0x0004 (Extended Compat ID) */
    0x01,                    /* bCount = 1 function */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* reserved */
    /* Function section (24 bytes) */
    0x00,                    /* bFirstInterfaceNumber = 0 */
    0x01,                    /* bReserved = 0x01 */
    'M', 'T', 'P', ' ', ' ', ' ', ' ', ' ', /* CompatibleID "MTP     " */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* SubCompatibleID */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00              /* reserved */
};

typedef struct {
    UsbDsEndpoint *endpoint;
    u8 *buffer;
    RwLock lock;
} usbMtpEndpoint;

static bool              g_mtpInitialized = false;
static UsbDsInterface   *g_mtpInterface   = NULL;
static usbMtpEndpoint    g_mtpEndpoints[TOTAL_ENDPOINTS];
static RwLock            g_mtpLock;

/* Thread de control para responder a los vendor requests de Windows */
static Thread  g_ctrlThread;
static bool    g_ctrlRunning = false;

/* ─── Ctrl-thread: responde al SetupEvent de la interfaz ────────────────── */
static void ctrlThreadFunc(void *arg)
{
    (void)arg;

    /* Buffer alineado para CtrlInPostBufferAsync */
    u8 *ctrlBuf = (u8*)memalign(0x1000, 0x1000);
    if (!ctrlBuf) return;

    while (g_ctrlRunning) {
        /* Esperar evento de setup packet (100 ms timeout) */
        if (R_FAILED(eventWait(&g_mtpInterface->SetupEvent, 100000000ULL))) {
            continue;
        }
        eventClear(&g_mtpInterface->SetupEvent);

        if (!g_ctrlRunning) break;

        /* Leer el setup packet (8 bytes) */
        struct usb_setup_packet {
            u8  bmRequestType;
            u8  bRequest;
            u16 wValue;
            u16 wIndex;
            u16 wLength;
        } pkt;
        if (R_FAILED(usbDsInterface_GetSetupPacket(g_mtpInterface, &pkt, sizeof(pkt)))) {
            continue;
        }

        /* ¿Es la petición del Microsoft OS 1.0 string descriptor (0xEE)?
         * bmRequestType = 0x80 (IN, Standard, Device), bRequest = 0x06 (GET_DESCRIPTOR)
         * wValue high byte = 0x03 (String), low byte = 0xEE              */
        if (pkt.bmRequestType == 0x80 &&
            pkt.bRequest == 0x06 &&
            (pkt.wValue >> 8) == 0x03 &&
            (pkt.wValue & 0xFF) == 0xEE)
        {
            u16 sendLen = sizeof(s_msft_string_desc);
            if (pkt.wLength < sendLen) sendLen = pkt.wLength;
            memcpy(ctrlBuf, s_msft_string_desc, sendLen);
            u32 urbId;
            if (R_SUCCEEDED(usbDsInterface_CtrlInPostBufferAsync(g_mtpInterface, ctrlBuf, sendLen, &urbId))) {
                eventWait(&g_mtpInterface->CtrlInCompletionEvent, 1000000000ULL);
                eventClear(&g_mtpInterface->CtrlInCompletionEvent);
            }
            continue;
        }

        /* ¿Es la petición del Extended Compat ID (vendor code = MS_VENDOR_CODE)?
         * bmRequestType = 0xC0 (IN, Vendor, Device), bRequest = MS_VENDOR_CODE
         * wIndex = 0x0004                                                  */
        if (pkt.bmRequestType == 0xC0 &&
            pkt.bRequest == MS_VENDOR_CODE &&
            pkt.wIndex == 0x0004)
        {
            u16 sendLen = (u16)sizeof(s_compat_id_desc);
            if (pkt.wLength < sendLen) sendLen = pkt.wLength;
            memcpy(ctrlBuf, s_compat_id_desc, sendLen);
            u32 urbId;
            if (R_SUCCEEDED(usbDsInterface_CtrlInPostBufferAsync(g_mtpInterface, ctrlBuf, sendLen, &urbId))) {
                eventWait(&g_mtpInterface->CtrlInCompletionEvent, 1000000000ULL);
                eventClear(&g_mtpInterface->CtrlInCompletionEvent);
            }
            continue;
        }

        /* Cualquier otro control request: STALL (no soportado) */
        usbDsInterface_StallCtrl(g_mtpInterface);
    }

    free(ctrlBuf);
}

Result usbMtpInitialize(void)
{
    Result rc = 0;
    rwlockWriteLock(&g_mtpLock);

    if (g_mtpInitialized) {
        rwlockWriteUnlock(&g_mtpLock);
        return 0;
    }

    memset(g_mtpEndpoints, 0, sizeof(g_mtpEndpoints));
    for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) rwlockInit(&g_mtpEndpoints[i].lock);

    rc = usbDsInitialize();
    if (R_FAILED(rc)) { rwlockWriteUnlock(&g_mtpLock); return rc; }

    u8 iManufacturer = 0, iProduct = 0, iSerialNumber = 0;
    static const u16 supported_langs[1] = {0x0409};

    rc = usbDsAddUsbLanguageStringDescriptor(NULL, supported_langs, sizeof(supported_langs)/sizeof(u16));
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iManufacturer, "Nintendo");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iProduct,      "Nintendo Switch");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iSerialNumber, "000000000001");

    struct usb_device_descriptor dev = {
        .bLength            = USB_DT_DEVICE_SIZE,
        .bDescriptorType    = USB_DT_DEVICE,
        .bcdUSB             = 0x0200,
        .bDeviceClass       = 0x00,   /* class defined in interface */
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = 0x40,
        .idVendor           = 0x0955,  /* NVIDIA/Android — mismo que DBI, Windows ya tiene MTBClassDriver */
        .idProduct          = 0x7321,
        .bcdDevice          = 0x0100,
        .iManufacturer      = iManufacturer,
        .iProduct           = iProduct,
        .iSerialNumber      = iSerialNumber,
        .bNumConfigurations = 0x01
    };
    dev.bcdUSB = 0x0110; dev.bMaxPacketSize0 = 0x40;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &dev);
    dev.bcdUSB = 0x0200; dev.bMaxPacketSize0 = 0x40;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &dev);
    dev.bcdUSB = 0x0300; dev.bMaxPacketSize0 = 0x09;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &dev);

    u8 bos[0x16] = {
        0x05, USB_DT_BOS, 0x16, 0x00, 0x02,
        0x07, USB_DT_DEVICE_CAPABILITY, 0x02, 0x02, 0x00, 0x00, 0x00,
        0x0A, USB_DT_DEVICE_CAPABILITY, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00
    };
    if (R_SUCCEEDED(rc)) rc = usbDsSetBinaryObjectStore(bos, sizeof(bos));

    /* ─── Interfaz: clase 0xFF (Vendor), como DBI ─── */
    struct usb_interface_descriptor intf = {
        .bLength            = USB_DT_INTERFACE_SIZE,
        .bDescriptorType    = USB_DT_INTERFACE,
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 3,
        .bInterfaceClass    = 0xFF,  /* Vendor — igual que DBI */
        .bInterfaceSubClass = 0xFF,
        .bInterfaceProtocol = 0x00,
        .iInterface         = 0
    };

    /* Endpoints */
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
    struct usb_endpoint_descriptor *ep_descs[TOTAL_ENDPOINTS] = { &ep_in, &ep_out, &ep_int };

    struct usb_ss_endpoint_companion_descriptor comp_bulk = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE, .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F, .bmAttributes = 0x00, .wBytesPerInterval = 0x00
    };
    struct usb_ss_endpoint_companion_descriptor comp_int = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE, .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x00, .bmAttributes = 0x00, .wBytesPerInterval = 0x1c
    };

    /* Buferes 4KB */
    for (u32 i = 0; i < TOTAL_ENDPOINTS && R_SUCCEEDED(rc); i++) {
        g_mtpEndpoints[i].buffer = (u8*)memalign(0x1000, 0x1000);
        if (!g_mtpEndpoints[i].buffer) { rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory); }
        else memset(g_mtpEndpoints[i].buffer, 0, 0x1000);
    }

    if (R_SUCCEEDED(rc)) rc = usbDsRegisterInterface(&g_mtpInterface);

    if (R_SUCCEEDED(rc)) {
        intf.bInterfaceNumber = g_mtpInterface->interface_index;

        int ep_in_num = 1, ep_out_num = 1;
        for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) {
            if (ep_descs[i]->bEndpointAddress & USB_ENDPOINT_IN)
                ep_descs[i]->bEndpointAddress = USB_ENDPOINT_IN | ep_in_num++;
            else
                ep_descs[i]->bEndpointAddress = ep_out_num++;
        }

        /* Full Speed */
        rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Full, &intf, USB_DT_INTERFACE_SIZE);
        for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++) {
            struct usb_endpoint_descriptor d = *ep_descs[i];
            if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) d.wMaxPacketSize = 0x40;
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Full, &d, USB_DT_ENDPOINT_SIZE);
        }
        /* High Speed */
        if (R_SUCCEEDED(rc)) {
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &intf, USB_DT_INTERFACE_SIZE);
            for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++) {
                struct usb_endpoint_descriptor d = *ep_descs[i];
                if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) d.wMaxPacketSize = 0x200;
                rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &d, USB_DT_ENDPOINT_SIZE);
            }
        }
        /* Super Speed */
        if (R_SUCCEEDED(rc)) {
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &intf, USB_DT_INTERFACE_SIZE);
            for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++) {
                struct usb_endpoint_descriptor d = *ep_descs[i];
                struct usb_ss_endpoint_companion_descriptor *comp;
                if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) {
                    d.wMaxPacketSize = 0x400;
                    comp = &comp_bulk;
                } else {
                    comp = &comp_int;
                }
                rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &d, USB_DT_ENDPOINT_SIZE);
                if (R_SUCCEEDED(rc))
                    rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, comp, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
            }
        }

        /* Registrar endpoints */
        for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++)
            rc = usbDsInterface_RegisterEndpoint(g_mtpInterface, &g_mtpEndpoints[i].endpoint, ep_descs[i]->bEndpointAddress);

        if (R_SUCCEEDED(rc)) rc = usbDsInterface_EnableInterface(g_mtpInterface);
        if (R_SUCCEEDED(rc)) rc = usbDsEnable();
    }

    if (R_SUCCEEDED(rc)) {
        /* Iniciar thread de control (Microsoft OS descriptors) */
        g_ctrlRunning = true;
        rc = threadCreate(&g_ctrlThread, ctrlThreadFunc, NULL, NULL, 0x4000, 0x2C, -2);
        if (R_SUCCEEDED(rc)) rc = threadStart(&g_ctrlThread);
        if (R_FAILED(rc)) { g_ctrlRunning = false; }
        else rc = 0; /* thread lanzado, continuar aun si ctrl falla — bulk igual funciona */
        g_mtpInitialized = true;
    } else {
        /* Limpiar en caso de fallo */
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

    /* Detener ctrl thread */
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

    Result rc = 0;
    u32 urbId = 0, chunksize = 0;
    u8 transfer_type = 0;
    u8 *bufptr = (u8*)buffer, *transfer_buffer = NULL;
    u32 tmp_transferredSize = 0;
    size_t total_transferredSize = 0;
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
        rc = usbDsParseReportData(&reportdata, urbId, NULL, &tmp_transferredSize);
        if (R_FAILED(rc)) break;

        if (tmp_transferredSize > chunksize) tmp_transferredSize = chunksize;
        total_transferredSize += (size_t)tmp_transferredSize;
        if (transfer_type == 0 && !isWrite) memcpy(bufptr, transfer_buffer, tmp_transferredSize);

        bufptr += tmp_transferredSize;
        size   -= tmp_transferredSize;
        if (tmp_transferredSize < chunksize) break;
    }

    rwlockWriteUnlock(&ep->lock);
    return total_transferredSize;
}
