#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

#define MTP_EP_BULK_IN   0
#define MTP_EP_BULK_OUT  1
#define MTP_EP_INTERRUPT 2

// Inicializa el hardware USB como respondedor MTP clase 6/1/1
Result usbMtpInitialize(void);

// Cierra la conexión USB y libera los endpoints
void usbMtpExit(void);

// Consulta si la conexión USB está activa
bool usbMtpIsActive(void);

// Transfiere datos por el endpoint especificado
size_t usbMtpTransfer(u32 endpoint, int isWrite, void* buffer, size_t size, u64 timeout_ns);

// Espera a que el host haya completado la enumeracion USB (llamar antes del primer read)
Result usbMtpWaitReady(u64 timeout_ns);

#ifdef __cplusplus
}
#endif
