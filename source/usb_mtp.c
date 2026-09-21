#include <string.h>
#include <malloc.h>
#include "usb_mtp.h"

#define TOTAL_ENDPOINTS 3

typedef struct {
    UsbDsEndpoint *endpoint;
    u8 *buffer;
    RwLock lock;
} usbMtpEndpoint;

static bool g_mtpInitialized = false;
static UsbDsInterface* g_mtpInterface = NULL;  /* nullptr es C++, aqui es C */
static usbMtpEndpoint g_mtpEndpoints[TOTAL_ENDPOINTS];
static RwLock g_mtpLock;

Result usbMtpInitialize(void)
{
    Result rc = 0;
    rwlockWriteLock(&g_mtpLock);

    if (g_mtpInitialized) {
        rwlockWriteUnlock(&g_mtpLock);
        return 0;
    }

    /* Inicializar struct de endpoints a cero */
    memset(g_mtpEndpoints, 0, sizeof(g_mtpEndpoints));

    rc = usbDsInitialize();
    if (R_FAILED(rc)) {
        rwlockWriteUnlock(&g_mtpLock);
        return rc;
    }

    u8 iManufacturer = 0, iProduct = 0, iSerialNumber = 0;
    static const u16 supported_langs[1] = {0x0409}; /* en-US */

    rc = usbDsAddUsbLanguageStringDescriptor(NULL, supported_langs, sizeof(supported_langs)/sizeof(u16));
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iManufacturer, "Nintendo");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iProduct, "Nintendo Switch");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&iSerialNumber, "000000000001");

    struct usb_device_descriptor device_descriptor = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0110,
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,
        .bMaxPacketSize0 = 0x40,
        .idVendor = 0x057e,
        .idProduct = 0x4000,
        .bcdDevice = 0x0100,
        .iManufacturer = iManufacturer,
        .iProduct = iProduct,
        .iSerialNumber = iSerialNumber,
        .bNumConfigurations = 0x01
    };

    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &device_descriptor);

    device_descriptor.bcdUSB = 0x0200;
    device_descriptor.bMaxPacketSize0 = 0x40;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &device_descriptor);

    device_descriptor.bcdUSB = 0x0300;
    device_descriptor.bMaxPacketSize0 = 0x09;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &device_descriptor);

    /* BOS: USB 2.0 Extension + SuperSpeed capability */
    u8 bos[0x16] = {
        0x05, USB_DT_BOS, 0x16, 0x00, 0x02,
        0x07, USB_DT_DEVICE_CAPABILITY, 0x02, 0x02, 0x00, 0x00, 0x00,
        0x0A, USB_DT_DEVICE_CAPABILITY, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00
    };
    if (R_SUCCEEDED(rc)) rc = usbDsSetBinaryObjectStore(bos, sizeof(bos));

    /* Interfaz MTP: clase 6 (Still Image / PTP), subclase 1, protocolo 1 */
    struct usb_interface_descriptor mtp_intf_desc = {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 3,
        .bInterfaceClass = 6,
        .bInterfaceSubClass = 1,
        .bInterfaceProtocol = 1,
        .iInterface = 0
    };

    u8 intf_str_idx = 0;
    if (R_SUCCEEDED(rc)) {
        if (R_SUCCEEDED(usbDsAddUsbStringDescriptor(&intf_str_idx, "MTP"))) {
            mtp_intf_desc.iInterface = intf_str_idx;
        }
    }

    /* Bulk IN (host <- device) */
    struct usb_endpoint_descriptor ep_in_desc = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = 0x40
    };

    /* Bulk OUT (host -> device) */
    struct usb_endpoint_descriptor ep_out_desc = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_OUT,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = 0x40
    };

    /* Interrupt IN (eventos del dispositivo) */
    struct usb_endpoint_descriptor ep_int_desc = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,
        .bmAttributes = USB_TRANSFER_TYPE_INTERRUPT,
        .wMaxPacketSize = 0x1c,
        .bInterval = 6
    };

    struct usb_endpoint_descriptor* ep_descs[TOTAL_ENDPOINTS] = {
        &ep_in_desc,
        &ep_out_desc,
        &ep_int_desc
    };

    /* Companion descriptor SuperSpeed (solo para bulk; interrupt usa 0) */
    struct usb_ss_endpoint_companion_descriptor ep_companion_bulk = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE,
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F,
        .bmAttributes = 0x00,
        .wBytesPerInterval = 0x00
    };
    struct usb_ss_endpoint_companion_descriptor ep_companion_int = {
        .bLength = USB_DT_SS_ENDPOINT_COMPANION_SIZE,
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x00,
        .bmAttributes = 0x00,
        .wBytesPerInterval = 0x1c  /* debe coincidir con wMaxPacketSize */
    };

    /* Buferes alineados 4 KB para PostBufferAsync */
    for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) {
        rwlockInit(&g_mtpEndpoints[i].lock);
        g_mtpEndpoints[i].buffer = (u8*)memalign(0x1000, 0x1000);
        if (g_mtpEndpoints[i].buffer == NULL) {
            rc = MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
            break;
        }
        memset(g_mtpEndpoints[i].buffer, 0, 0x1000);
    }

    if (R_SUCCEEDED(rc)) {
        rc = usbDsRegisterInterface(&g_mtpInterface);
    }

    if (R_SUCCEEDED(rc)) {
        mtp_intf_desc.bInterfaceNumber = g_mtpInterface->interface_index;

        /* Asignar direcciones de endpoint (IN: bit7=1, OUT: bit7=0) */
        int ep_in_num = 1;
        int ep_out_num = 1;
        for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) {
            if (ep_descs[i]->bEndpointAddress & USB_ENDPOINT_IN) {
                ep_descs[i]->bEndpointAddress = USB_ENDPOINT_IN | ep_in_num++;
            } else {
                ep_descs[i]->bEndpointAddress = ep_out_num++;
            }
        }

        /* --- Full Speed (12 Mbps) --- */
        rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Full, &mtp_intf_desc, USB_DT_INTERFACE_SIZE);
        for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++) {
            struct usb_endpoint_descriptor d = *ep_descs[i];
            if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) d.wMaxPacketSize = 0x40;
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Full, &d, USB_DT_ENDPOINT_SIZE);
        }

        /* --- High Speed (480 Mbps) --- */
        if (R_SUCCEEDED(rc)) {
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &mtp_intf_desc, USB_DT_INTERFACE_SIZE);
            for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++) {
                struct usb_endpoint_descriptor d = *ep_descs[i];
                if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) d.wMaxPacketSize = 0x200;
                rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_High, &d, USB_DT_ENDPOINT_SIZE);
            }
        }

        /* --- Super Speed (5 Gbps) --- */
        if (R_SUCCEEDED(rc)) {
            rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &mtp_intf_desc, USB_DT_INTERFACE_SIZE);
            for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++) {
                struct usb_endpoint_descriptor d = *ep_descs[i];
                struct usb_ss_endpoint_companion_descriptor *comp;
                if (d.bmAttributes == USB_TRANSFER_TYPE_BULK) {
                    d.wMaxPacketSize = 0x400;
                    comp = &ep_companion_bulk;
                } else {
                    /* Interrupt endpoint: wMaxPacketSize igual que Full/High */
                    comp = &ep_companion_int;
                }
                rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, &d, USB_DT_ENDPOINT_SIZE);
                if (R_SUCCEEDED(rc)) {
                    rc = usbDsInterface_AppendConfigurationData(g_mtpInterface, UsbDeviceSpeed_Super, comp, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
                }
            }
        }

        /* Registrar endpoints */
        for (u32 i = 0; R_SUCCEEDED(rc) && i < TOTAL_ENDPOINTS; i++) {
            rc = usbDsInterface_RegisterEndpoint(g_mtpInterface, &g_mtpEndpoints[i].endpoint, ep_descs[i]->bEndpointAddress);
        }

        if (R_SUCCEEDED(rc)) rc = usbDsInterface_EnableInterface(g_mtpInterface);
        if (R_SUCCEEDED(rc)) rc = usbDsEnable();
    }

    if (R_SUCCEEDED(rc)) {
        g_mtpInitialized = true;
    } else {
        for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) {
            if (g_mtpEndpoints[i].buffer) {
                free(g_mtpEndpoints[i].buffer);
                g_mtpEndpoints[i].buffer = NULL;
            }
            g_mtpEndpoints[i].endpoint = NULL;
        }
        if (g_mtpInterface) {
            usbDsInterface_Close(g_mtpInterface);
            g_mtpInterface = NULL;
        }
        usbDsExit();
    }

    rwlockWriteUnlock(&g_mtpLock);
    return rc;
}

void usbMtpExit(void)
{
    rwlockWriteLock(&g_mtpLock);
    if (!g_mtpInitialized) {
        rwlockWriteUnlock(&g_mtpLock);
        return;
    }

    g_mtpInitialized = false;

    for (u32 i = 0; i < TOTAL_ENDPOINTS; i++) {
        if (g_mtpEndpoints[i].endpoint) {
            usbDsEndpoint_Cancel(g_mtpEndpoints[i].endpoint);
        }
        rwlockWriteLock(&g_mtpEndpoints[i].lock);
        if (g_mtpEndpoints[i].buffer) {
            free(g_mtpEndpoints[i].buffer);
            g_mtpEndpoints[i].buffer = NULL;
        }
        g_mtpEndpoints[i].endpoint = NULL;
        rwlockWriteUnlock(&g_mtpEndpoints[i].lock);
    }

    if (g_mtpInterface) {
        usbDsInterface_Close(g_mtpInterface);
        g_mtpInterface = NULL;
    }

    usbDsExit();
    rwlockWriteUnlock(&g_mtpLock);
}

bool usbMtpIsActive(void)
{
    bool active = false;
    rwlockReadLock(&g_mtpLock);
    active = g_mtpInitialized;
    rwlockReadUnlock(&g_mtpLock);
    return active;
}

/*
 * usbMtpWaitReady: espera hasta timeout_ns a que el host haya completado la
 * enumeracion y la interfaz MTP este lista para transferencias.
 * Llama esto UNA VEZ antes del primer bucle de lectura en el worker.
 */
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

    if (!ep->endpoint || !ep->buffer) {
        rwlockWriteUnlock(&ep->lock);
        return 0;
    }

    Result rc = 0;
    u32 urbId = 0;
    u32 chunksize = 0;
    u8 transfer_type = 0;
    u8 *bufptr = (u8*)buffer;
    u8 *transfer_buffer = NULL;
    u32 tmp_transferredSize = 0;
    size_t total_transferredSize = 0;
    UsbDsReportData reportdata;

    while (size > 0) {
        if (((u64)bufptr) & 0xfff) {
            /* Buffer no alineado: usar buffer intermedio de 4KB */
            transfer_buffer = ep->buffer;
            memset(ep->buffer, 0, 0x1000);
            chunksize = 0x1000 - (((u64)bufptr) & 0xfff);
            if (size < chunksize) chunksize = (u32)size;
            if (isWrite) memcpy(ep->buffer, bufptr, chunksize);
            transfer_type = 0;
        } else {
            transfer_buffer = bufptr;
            chunksize = (u32)size;
            if (chunksize > 0x1000) chunksize = 0x1000; /* chunk maximo seguro */
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

        if (transfer_type == 0 && !isWrite) {
            memcpy(bufptr, transfer_buffer, tmp_transferredSize);
        }

        bufptr += tmp_transferredSize;
        size -= tmp_transferredSize;

        if (tmp_transferredSize < chunksize) break;
    }

    rwlockWriteUnlock(&ep->lock);
    return total_transferredSize;
}
