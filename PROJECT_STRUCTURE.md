# Arquitectura y Estructura del Proyecto FileZzz

Documento oficial de arquitectura técnica, módulos, flujo de ejecución y directrices de diseño para **FileZzz** (Nintendo Switch Homebrew).

---

## 1. Visión General del Sistema

FileZzz es un administrador multifuncional de archivos y contenidos para Nintendo Switch (Horizon OS / Atmosphere), desarrollado en **C++20** utilizando el framework gráfico **Borealis** (con backend OpenGL/GLFW y aceleración 2D por **NanoVG**), comunicándose con el hardware a través de **libnx**.

```
+-----------------------------------------------------------------------+
|                              FileZzz UI                               |
|       (Borealis Framework + Yoga Flex Layout + NanoVG Vector Engine)   |
+-------------------+-------------------+---------------+---------------+
|    Explorador     |  Gestión Datos    |    USB MTP    | Servidor FTP  |
|  (Archivos / SD)  | (Juegos / Espacio)|  (Responder)  | (Sockets TCP) |
+-------------------+-------------------+---------------+---------------+
|                           Core Services                               |
|   - ConfigManager (AppConfig)         - AppUpdater (GitHub API / HTTP)|
|   - ClockBoost (Overclock en MTP/FTP) - StorageScanner & Cleaner      |
+-----------------------------------------------------------------------+
|                      Hardware & OS Abstraction                        |
|   libnx (USB, FS, Clocks, Accounts, Titles, Sockets) / Horizon OS     |
+-----------------------------------------------------------------------+
```

---

## 2. Mapa de Directorios del Código

```
C:\Projects\c++\EzFiles\
├── CMakeLists.txt              # Configuración CMake, compilación ELF/NACP y empaquetado RomFS
├── icon.jpg                    # Icono oficial de la aplicación (256x256)
├── include/
│   ├── config.hpp              # AppConfig: lectura y persistencia INI (sdmc:/switch/FileZzz/config.ini)
│   ├── mtp_ops.hpp             # Motor MTP: despachador de operaciones, objetos, handles y caché
│   ├── mtp_usb.hpp             # API C++ del transporte USB MTP
│   ├── ftp_server.hpp          # Servidor FTP multihilo con soporte PASV y modo instalación
│   ├── sys_clock.hpp           # Control de Overclock/Boost de CPU/GPU durante transferencias
│   ├── title_manager.hpp       # Lector de títulos instalados, metadatos, versiones y DLCs
│   ├── app_updater.hpp         # Motor de auto-actualización desde GitHub Releases
│   └── i18n.hpp                # Respaldo de localización
├── source/
│   ├── main_borealis.cpp       # Interfaz gráfica principal: Tabs, Vistas, Diálogos y temas
│   └── usb_mtp.c               # Implementación en C de endpoints USB (Bulk IN/OUT) vía libnx
└── resources/romfs/
    ├── xml/
    │   ├── main_tabs.xml       # Estructura principal de la app (WallpaperAppletFrame + Sidebar)
    │   ├── view_explorer.xml   # Vista del explorador de archivos MicroSD
    │   ├── view_games.xml      # Vista de gestión de datos, espacio y programas
    │   ├── view_installed_games.xml # Lista detallada de juegos con iconos y versiones
    │   ├── view_mtp.xml        # Panel de control MTP y consola de eventos
    │   ├── view_ftp.xml        # Panel de control FTP y conexión móvil
    │   ├── view_settings.xml   # Ajustes de temas, fondos, opacidad e idioma
    │   ├── view_about.xml      # Acerca de, donaciones (QR) y créditos
    │   └── view_splash.xml     # Pantalla de bienvenida limpia
    ├── i18n/                   # 13 idiomas completos (hints.json y brls.json)
    ├── img/                    # Iconografía HD (clara y oscura) y logos
    └── fonts/                  # Fuentes auxiliares y símbolos
```

---

## 3. Módulos y Responsabilidades Técnicas

### A. Capa Gráfica (Borealis + NanoVG)
- **`WallpaperAppletFrame`**: Marco raíz de la aplicación.
  - Intercepta el ciclo de dibujado (`draw`) para pintar la imagen del fondo cargada en memoria (`nvgImagePattern`) con el porcentaje exacto de opacidad (`wallpaperOpacity`), antes de proyectar la barra lateral y el contenido de las pestañas.
  - Elimina el problema de que capas sólidas tapen el fondo de pantalla.
- **`Yoga Layout Engine`**:
  - **Regla estricta**: Nunca usar `setWidth(100.0f)` para anchos porcentuales. `setWidth(100)` establece 100 píxeles fijos. Debe usarse `setWidthPercentage(100.0f)` o `setGrow(1.0f)`.
  - Diálogos y menús contextuales deben usar anchos estables (`600px` - `680px`) para evitar columnas colapsadas a la izquierda.
- **Regla de Iconografía y Selección**:
  - **Prohibido el uso de caracteres unicode como parche visual** (ej. `[ ✔ ]` o emojis como texto).
  - La selección múltiple se realiza mediante:
    1. Resaltado de fila completo con tinte translúcido (`nvgRGBA(0, 242, 254, 40)`).
    2. Dibujo vectorial de la casilla de verificación nativa (`brls::CheckBox` o vector de NanoVG).

### B. Motor MTP (Transferencia por Cable USB)
- **Endpoints**: Bulk IN (0x81), Bulk OUT (0x01) y Event IN (0x82).
- **Buffer de transferencia**: 256 KB alineados a 4 KB (`alignas(0x1000)`) para saturar el bus USB de la Switch (~30-40 MB/s).
- **Caché de Handles (`g_objs`)**:
  - Cada directorio enumerado registra sus identificadores.
  - Al ejecutar `OP_DeleteObject`, se purga recursivamente el identificador y todos sus hijos en memoria para que Windows Explorer nunca reciba un identificador huérfano.
  - En caso de fallo de eliminación, responder con `MR_AccessDenied` (0x200F) para evitar que Windows interprete una desconexión física.
- **Directorio Virtual de Instalación**:
  - Partición `5: Instalar a SD/NAND`. Los archivos NSP/NSZ soltados aquí se instalan al vuelo.

### C. Motor FTP y Conexión para Móviles
- **Sockets TCP**: Soporta comandos estándar (`PASV`, `EPSV`, `RETR`, `STOR`, `DELE`, `RMD`, `MKD`, `OPTS UTF8 ON`).
- **Transferencia desde teléfonos**:
  - Código QR generado en pantalla con la URL `ftp://192.168.x.x:5000` y credenciales para conectar clientes móviles (AndFTP, Documents en iOS, WiFi FTP).
  - Servidor HTTP ligero auxiliar para subir archivos directamente desde el navegador de cualquier celular sin instalar apps.

### D. Overclock Dinámico (Boost Mode)
- Al iniciar MTP o Servidor FTP, eleva temporalmente las frecuencias:
  - CPU: **1785 MHz** (o 1224 MHz según modo batería/dock).
  - EMC (Memoria): **1600 MHz**.
- Al detener el servicio o desconectar el cable, restaura los relojes por defecto para ahorrar batería y no sobrecalentar la consola.

### E. Limpieza de Archivos Basura y Mantenimiento
- Escaneo de la tarjeta MicroSD en segundo plano para:
  - Eliminar carpetas vacías huérfanas.
  - Limpiar volcados de error (`fatal.log`, `crash_dumps`).
  - Detección de tickets huérfanos de títulos desinstalados.

### F. Actualizaciones Integradas (In-App Updater)
- Consulta la API pública de GitHub (`https://api.github.com/repos/lozp1/FileZzz/releases/latest`).
- Descarga el nuevo `FileZzz.nro`, reemplaza el archivo actual en `sdmc:/switch/FileZzz/FileZzz.nro` y solicita reiniciar la aplicación.

---

## 4. Guía de Calidad y Directrices

1. **Memoria y Descriptores de Archivos**:
   - Ningún archivo de registro (`filezzz.log`) debe permanecer abierto con un puntero `FILE*` permanente durante toda la sesión para evitar bloqueos del sistema de archivos al conectar a la PC.
2. **Localización (i18n)**:
   - Todo texto visible en pantalla debe resolverse mediante `@i18n/<dominio>/<clave>` o `"<dominio>/<clave>"_i18n`.
   - Se mantiene paridad en los 13 diccionarios lingüísticos.
3. **Estilo Visual Horizon OS**:
   - Tipografía oficial de Nintendo Switch (`PlSharedFontType_Standard`).
   - Contrastes altos en Blanco, Negro y AMOLED con acentos en cyan (`#00F2FE` / `#38BDF8`).
