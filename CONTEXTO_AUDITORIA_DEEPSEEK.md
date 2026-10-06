# Auditoría Técnica y Contexto Global de FileZzz (para revisión por DeepSeek)

**Fecha de generación:** Octubre 2026  
**Proyecto:** FileZzz (anteriormente EzFiles)  
**Propósito:** Proporcionar todo el contexto técnico, arquitectónico, histórico de errores, soluciones aplicadas y tareas pendientes para revisión, auditoría y propuestas de mejora por parte de un agente de IA especializado (DeepSeek).

---

## 1. Resumen Ejecutivo del Proyecto

**FileZzz** es una aplicación homebrew multifuncional para **Nintendo Switch** desarrollada en **C++20** sobre el framework gráfico **Borealis** y la biblioteca oficial homebrew **libnx**. Su objetivo es posicionarse como una alternativa moderna, visualmente elegante y completa frente a herramientas consolidadas como **DBI** y **Goldleaf**, integrando:

*   Explorador de archivos completo con operaciones avanzadas en tarjetas SD y particiones NAND.
*   Servidor MTP integrado con particiones virtuales (estilo DBI).
*   Servidor FTP multi-hilo integrado para transferencia inalámbrica.
*   Administrador visual de títulos instalados (juegos base, parches, DLCs, estadísticas de almacenamiento).
*   Motor nativo de instalación de paquetes NSP/NCA con registro en el sistema operativo Horizon (Menú HOME).
*   Herramientas de mantenimiento del almacenamiento (limpieza de archivos huérfanos).

### Entorno de Desarrollo y Stack Tecnológico
*   **Lenguaje:** C++20.
*   **Toolchain:** devkitPro (`devkitA64`, `gcc 14+`, `libnx` versión reciente).
*   **Sistema de compilación:** CMake + Ninja.
*   **UI Framework:** Borealis (librería gráfica basada en NanoVG / OpenGL / motor de layout Yoga).
*   **Repositorio Git:** `https://github.com/lozp1/FileZzz.git` (Rama activa: `dev/borealis-ui`).
*   **Ubicación del binario listo para consola:** `C:\Projects\c++\EzFiles\FileZzz.nro`.

---

## 2. Arquitectura de Módulos y Estado de Implementación

| Módulo | Archivos Clave | Estado (%) | Descripción y Capacidades Actuales |
| :--- | :--- | :---: | :--- |
| **Explorador de Archivos** | `source/main_borealis.cpp` (`ExplorerTab`) | **95%** | Navegación en `sdmc:/`, `USER`, `SYSTEM`, `GameCard`. Copiar, cortar, pegar, renombrar, eliminar, selección múltiple. Diálogo interactivo de instalación para `.nsp`. |
| **Cliente MTP** | `include/mtp/*.hpp`, `source/main_borealis.cpp` (`MtpTab`) | **90%** | Protocolo MTP nativo para PC con 8 particiones virtuales (MicroSD, NAND User, NAND System, GameCard, MicroSD install, NAND install, Album). Detección de emuMMC en Album (`sdmc:/emuMMC/*/Nintendo/Album`). Auto-cierre de hilos USB al salir del tab. |
| **Servidor FTP** | `include/ftp_server.hpp`, `source/main_borealis.cpp` (`FtpTab`) | **95%** | Servidor FTP multi-hilo sobre sockets BSD. Auto-cierre de sockets y parada de servicio al volver al menú o cambiar de pestaña. |
| **Visor de Juegos** | `source/main_borealis.cpp` (`GamesTab`) | **85%** | Lectura de títulos instalados en SD y NAND interna mediante `ns` y `ncm`. Muestra icono extraído de NACP, nombre, versión, desglose de peso (base, update, DLC) y medidores visuales de memoria libre. |
| **Motor de Instalación NSP** | `include/ncm_installer.hpp`, `include/pfs0.hpp` | **75%** | Parser PFS0, importación de tickets (`esImportTicket`), streaming a placeholders de `NcmContentStorage`, montaje de `.cnmt.nca` vía FS, registro en `NcmContentMetaDatabase` y creación de icono en Menú HOME (`nsPushApplicationRecord`). |
| **Limpiador de Huérfanos** | `include/ncm_cleaner.hpp` | **50%** | Escaneo seguro vía `ncmContentMetaDatabaseLookupOrphanContent` y eliminación con `ncmContentStorageDelete`. Pendiente optimización de paginación IPC. |
| **Ajustes y Personalización** | `source/main_borealis.cpp` (`SettingsTab`, `AboutTab`) | **95%** | Temas claro/oscuro/personalizado, wallpapers personalizados con opacidad, i18n con 13 idiomas, splash screen animado y pantalla About con QR. |

---

## 3. Historial de Hallazgos Críticos y Soluciones Recientes

### 3.1. Caso "Minecraft / Instalación Incompleta y Archivos Huérfanos"
*   **Problema observado por el usuario:** Al instalar un juego por primera vez vía MTP/Explorador, la barra de progreso llegó al 100%, pero el icono nunca apareció en el Menú HOME de la consola. El usuario descubrió que el almacenamiento se redujo y vio carpetas numeradas en `.../Contents/registered/`.
*   **Causa raíz identificada:** El motor original sólo escribía los archivos `.nca` en el almacenamiento físico con `ncmContentStorageRegister`, pero **no realizaba los 3 pasos obligatorios del sistema Horizon OS**:
    1.  No importaba el ticket criptográfico (`.tik`) ni certificado (`.cert`) mediante el servicio `es`.
    2.  No montaba el `.cnmt.nca` para leer y registrar los metadatos en `NcmContentMetaDatabase`.
    3.  No registraba el título en el gestor de aplicaciones (`nsPushApplicationRecord`), por lo que Horizon nunca creaba la burbuja del juego.
*   **Residuos huérfanos:** Cuando un `.nca` se copia a la partición registrada sin un registro de metadatos válido, el sistema operativo no lo reconoce como parte de ningún juego y queda como archivo "huérfano".

### 3.2. Crash en el Limpiador de Huérfanos (`ncm_cleaner.hpp`)
*   **Problema:** Al pulsar `[Y] Limpiar huérfanos`, la aplicación se cerraba inesperadamente (crash/panic).
*   **Causa raíz identificada:** La función `ncmContentStorageListContentId` recibía el total de contenidos del sistema (en una consola con más de 35 juegos, esto supera miles de IDs). Al intentar volcar todos los IDs de golpe en una sola petición IPC sin paginar, el búfer IPC de libnx desbordaba el límite permitido por el kernel de Horizon, forzando la desconexión del servicio NCM y crasheando el proceso.
*   **Recomendación de contingencia:** Limpiar los huérfanos desde la opción `Tools -> Cleanup orphaned files` de DBI hasta que se integre la paginación por lotes de 64 en FileZzz.

### 3.3. Por qué el nuevo instalador llegaba al 100% pero el juego seguía sin aparecer en el Menú HOME (Auditoría profunda del commit `cc10d5e` vs `b5dbaaf`)
En las pruebas recientes con el instalador mejorado, el usuario reportó que la barra llegó al 100% y mostró el diálogo de "¡Paquete instalado exitosamente!", pero al volver al Menú HOME de la Switch el juego no estaba.

La auditoría del código reveló **tres errores concurrentes**:
1.  **Orden de confirmación en la base de datos (Database Commit Ordering):**
    *   *Error en código:* Se llamaba a `pushApplicationRecord` antes de ejecutar `ncmContentMetaDatabaseCommit(&metaDb)`.
    *   *Mecanismo interno de Horizon:* Cuando el gestor de la interfaz (`ns`) procesa `nsPushApplicationRecord`, hace una verificación síncrona consultando a `ncm` para confirmar que el `TitleID` y su `ContentMeta` existen. Como la transacción de la base de datos aún no se había confirmado (`Commit`), `ns` no encontraba el título en la base de datos de contenidos y rechazaba silenciosamente la creación del registro en el menú HOME.
2.  **Omisión del valor de retorno de `pushApplicationRecord`:**
    *   En `registerCnmtAndAppRecord`, la llamada a `pushApplicationRecord(...)` se ejecutaba sin capturar ni comprobar su `Result rc`, retornando siempre `0` (éxito). Como consecuencia, la UI creía que todo había sido exitoso aunque `ns` hubiera fallado internamente.
3.  **Alineación de memoria y tipo de datos en IPC:**
    *   La estructura IPC `ContentStorageRecord` para el servicio `ns` requiere que el campo `storageId` sea un `u64` (8 bytes), y el paquete de comando requiere una alineación nativa de 64 bits para `application_id`.
4.  **Sensibilidad a mayúsculas/minúsculas en la detección del CNMT:**
    *   En `pfs0.hpp`, la comprobación `isCnmt()` utilizaba `name.compare(..., ".cnmt.nca")`, lo cual fallaba en paquetes con nombres en mayúsculas como `.CNMT.NCA`.
5.  **Falta de Rollback en caso de fallo:**
    *   Si el proceso de registro fallaba al final, los archivos `.nca` copiados durante los minutos anteriores permanecían en `registered/`, generando más residuos huérfanos en la SD.

---

## 4. Mejoras Recientes Implementadas (Commit `b5dbaaf`)

En el último commit subido al repositorio (`b5dbaaf` en la rama `dev/borealis-ui`), se aplicaron las siguientes correcciones:

```cpp
// 1. Commit anticipado en ncmContentMetaDatabase
rc = ncmContentMetaDatabaseSet(&metaDb, &metaKey, installBuffer.data(), installBuffer.size());
if (R_FAILED(rc)) return rc;

rc = ncmContentMetaDatabaseCommit(&metaDb); // ¡CONFIRMAR ANTES DE LLAMAR A NS!
if (R_FAILED(rc)) return rc;

// 2. Registro formal en el menú HOME y verificación estricta de rc
rc = pushApplicationRecord(pkgHdr->title_id, targetStorage, metaKey);
if (R_FAILED(rc)) return rc;

// 3. Rollback automático de seguridad en NcmInstaller::installFromNsp
if (!res.success) {
    for (const auto& nid : newlyWrittenNcas) {
        ncmContentStorageDelete(&storage, &nid);
    }
}
```

*   **Rollback automático:** Se mantiene una lista `newlyWrittenNcas`. Si ocurre cualquier error durante la transferencia o el registro, o si el usuario cancela la instalación a mitad de camino, FileZzz elimina automáticamente todos los bloques NCA escritos durante esa sesión. **Cero archivos huérfanos residuales.**
*   **Detección de extensiones tolerante:** `isCnmt()`, `isNca()`, `isTicket()` y `isCert()` convierten el nombre a minúsculas antes de evaluar.

---

## 5. Tareas Pendientes y Puntos de Decisión Técnica

Se solicita a DeepSeek evaluar, auditar y proponer mejoras concretas para los siguientes puntos:

### Tarea A: Paginación segura en el Escáner de Huérfanos (`ncm_cleaner.hpp`)
*   **Problema actual:** `ncmContentStorageListContentId` intenta leer todos los IDs de una sola vez.
*   **Solución requerida:** Implementar un bucle de paginación que lea lotes de máximo 64 o 128 `NcmContentId` por llamada utilizando el parámetro `start_offset`, evitando saturar el búfer de IPC del kernel de Horizon.

### Tarea B: Instalación al vuelo vía MTP (DBI-Style Direct USB Install)
*   **Situación actual:** Las particiones MTP `5: MicroSD install` y `6: NAND install` existen en el árbol virtual de MTP, pero actualmente guardan el archivo físicamente en disco antes de procesarlo.
*   **Objetivo:** Interceptar el flujo de escritura MTP para hacer streaming directo desde el PC hacia `NcmContentStorage` sin escribir el `.nsp` intermedio en la tarjeta SD, ahorrando el doble de espacio y acelerando la instalación.

### Tarea C: Soporte para formatos NSZ, XCZ y XCI
*   **NSZ / XCZ:** Requiere descompresión en memoria mediante `zstd` (Zstandard) y descapsulado del bloque de cabecera NCZ (`nczHeader`).
*   **XCI:** Requiere parsear la cabecera HFS0 de la imagen de tarjeta de juego, ubicar la partición `secure` e instalar los NCAs y el CNMT contenidos en ella.

### Tarea D: Módulo Verificador / Actualizador de Juegos (Game Update Manager)
*   **Requerimiento del usuario:** Poder consultar qué juegos instalados tienen actualizaciones pendientes y aplicar los parches correspondientes.
*   **Diseño propuesto:**
    *   Lectura de la versión local instalada (`NcmContentMetaKey.version` / NACP).
    *   Comparación contra una lista de títulos/versiones estructurada en JSON.
    *   Identificación de actualizaciones disponibles (`TitleID_Base + 0x800`).

### Tarea E: Política de Tiendas / Descargas Online
*   **Conclusión legal y técnica:** No se implementará ninguna tienda de contenido comercial ni descargadores torrent de juegos protegidos por derechos de autor (evitando avisos DMCA y cierre del proyecto).
*   **Alcance aprobado:** Implementación de un **Homebrew App Store** legítimo (integración con la API pública de 4TU / Switch Appstore o GitHub Releases) para descargar y actualizar herramientas caseras, emuladores y ports abiertos.

---

## 6. Estructura de Archivos del Proyecto para Referencia

```
C:\Projects\c++\EzFiles\
├── CMakeLists.txt
├── include/
│   ├── ftp_server.hpp          # Servidor FTP multi-hilo
│   ├── ncm_cleaner.hpp         # Escáner y limpiador de huérfanos NCM
│   ├── ncm_installer.hpp       # Motor completo de instalación NSP / Horizon OS
│   ├── pfs0.hpp                # Parser de contenedores PFS0/NSP
│   ├── mtp/                    # Implementación del protocolo MTP (8 particiones)
│   │   ├── mtp_server.hpp
│   │   ├── mtp_storage.hpp
│   │   └── mtp_usb.hpp
│   └── views/                  # Vistas personalizadas de Borealis
├── source/
│   └── main_borealis.cpp       # Punto de entrada, ciclo de vida de UI y pestañas
└── resources/                  # Fuentes, iconos, assets y archivos de idioma i18n
```

---

## 7. Preguntas Específicas para DeepSeek

1.  ¿Existe algún detalle adicional en la llamada `serviceDispatchIn` a `ns` (comando 16 `PushApplicationRecord`) o en los atributos de búfer (`SfBufferAttr_HipcMapAlias`) que pueda ser incompatible con versiones de firmware recientes (HOS 17.0.0 - 19.0.0+)?
2.  ¿Qué optimizaciones de rendimiento recomiendas para el streaming de placeholders en `ncmContentStorageWritePlaceHolder` (actualmente con búfer de 1 MB en `std::vector<u8>`) para maximizar la velocidad de escritura en tarjetas microSD clase U3/A2?
3.  ¿Cómo estructurarías la máquina de estados para la descompresión al vuelo de paquetes `.nsz` con la librería `libzstd` sin incurrir en desbordamientos de memoria en la Nintendo Switch (límite de ~400-500 MB en modo Applet vs 3.2 GB en Application Mode)?
