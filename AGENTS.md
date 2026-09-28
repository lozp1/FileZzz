# FileZzz - Directrices de Arquitectura y Desarrollo para Agentes

Este documento contiene las reglas, estándares arquitectónicos y lineamientos de codificación para cualquier agente o desarrollador que trabaje en el proyecto **FileZzz** (anteriormente conocido como *EzFiles*).

---

## 1. Visión General del Proyecto
**FileZzz** es un gestor de archivos avanzado de alto rendimiento para Nintendo Switch con estética nativa de **Horizon OS**, desarrollado en **C++20** utilizando **Borealis** (con backend OpenGL / NanoVG) y comunicándose con el hardware y sistema operativo a través de **libnx**.

El objetivo central es brindar una experiencia fluida a 60 FPS, navegación intuitiva con Joy-Con/Pro Controller y pantalla táctil, transferencias USB MTP a alta velocidad (30-40 MB/s con overclock dinámico estilo DBI), servidor FTP integrado, gestión exhaustiva de programas/juegos y personalización visual (fondos de pantalla y temas).

---

## 2. Convenciones de Nomenclatura y Rutas Maestras
- **Nombre Oficial**: `FileZzz` (con triple 'Z').
- **Ruta en microSD para Datos y Configuración**: `sdmc:/switch/FileZzz/`
  - Archivo de configuración: `sdmc:/switch/FileZzz/config.ini`
  - Fondos de pantalla del usuario: `sdmc:/switch/FileZzz/wallpapers/`
  - Bitácora de depuración: `sdmc:/switch/FileZzz/filezzz.log`
  - Instaladores temporales SD/NAND: `sdmc:/switch/FileZzz/install_sd/` y `sdmc:/switch/FileZzz/install_nand/`
  - Respaldos de partidas: `sdmc:/switch/FileZzz/saves/`
- **Compatibilidad con Versiones Anteriores**:
  - Si no existe `sdmc:/switch/FileZzz/config.ini` pero sí `sdmc:/switch/EzFiles/config.ini`, se debe migrar automáticamente y conservar compatibilidad de lectura sin interrumpir al usuario.
- **Binario de Distribución**: `FileZzz.nro`
  - Generado en `build_ezfiles/FileZzz.nro`, copiado automáticamente a `FileZzz.nro` y `C:/Projects/FileZzz/FileZzz.nro`.

---

## 3. Directrices de UI (Borealis + NanoVG)
1. **Internacionalización Obligatoria (i18n)**:
   - **NUNCA** introducir cadenas de texto fijas en español o inglés en el código C++ ni en los archivos XML de vistas.
   - En XML: usar siempre `@i18n/hints/<clave>`.
   - En C++: usar siempre `"hints/<clave>"_i18n`.
   - Toda nueva clave debe agregarse en los 13 diccionarios soportados (`resources/romfs/i18n/<lang>/hints.json`).
2. **Cero Hacks de Caracteres Unicode para Íconos**:
   - No usar `[ ✔ ]`, `[ X ]` ni caracteres tipográficos propensos a desalineación o fuentes faltantes.
   - Usar siempre componentes vectoriales dibujados en **NanoVG** (`CheckmarkView`, `SelectionBoxView`, `QrCodeView`) con matemáticas de renderizado precisas.
3. **Reglas de Foco y Navegación con Gamepad**:
   - **NUNCA** llamar a `setFocusable(true)` en un contenedor raíz (`Box`, `ScrollingFrame`, `Activity`). Solo los elementos interactivos hijos (celdas, botones, filas) deben ser enfocables.
   - Si un contenedor raíz se hace enfocable, el D-Pad lo captura, dibuja un recuadro azul gigante alrededor de toda la pantalla (1280x720) y atrapa el cursor de navegación.
   - Al cambiar una opción simple (ej. tema visual), no destruir la actividad entera con `reloadMainActivity()`; actualizar el estado in-place para que el cursor no salte bruscamente al menú lateral.
4. **Opacidad de Fondo y Modales (`brls/background`)**:
   - `"brls/background"` **DEBE SER SIEMPRE OPACO** (`nvgRGB`).
   - Los modales (`brls::Dialog`) usan `"brls/background"`. Si se hace transparente con canal alfa reducido para los fondos de pantalla, todos los diálogos y confirmaciones de salida se volverán transparentes e ilegibles.
   - La textura del fondo de pantalla debe proyectarse exclusivamente dentro de `WallpaperAppletFrame::draw()`.

---

## 4. Rendimiento y Estabilidad
1. **Carga de Texturas e Imágenes**:
   - No utilizar `NVG_IMAGE_GENERATE_MIPMAPS` al cargar fondos de pantalla en tiempo real con NanoVG; esto satura la memoria del Tegra X1 y congela la interfaz durante varios segundos. Utilizar flags básicos (`0` o `NVG_IMAGE_NEAREST`).
2. **Ciclo de Vida de Actividades**:
   - No invocar `brls::Application::clear()` o destrucciones masivas de actividades dentro de callbacks de animación (como `popContentView`).
3. **Lectura de Metadatos de Juegos (libnx `ns`)**:
   - `NsApplicationOccupiedSize` contiene 4 estructuras `ApplicationOccupiedSizeEntity` de 32 bytes (`storageId`, 7 bytes reservados, `appSize`, `patchSize`, `aocSize`). Sumar las entidades para calcular el tamaño real de los juegos tanto en NAND como en tarjeta MicroSD.
