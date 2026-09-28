# Plan Maestro Integral de FileZzz (Roadmap de Producción)

Este documento detalla todas las características, correcciones y mejoras necesarias para llevar a **FileZzz** al nivel de calidad, estabilidad y utilidad que los usuarios y la comunidad demandan (superando las capacidades de DBI con una interfaz nativa Switch).

---

## Índice de Fases

1. **Fase 1: Corrección Visual Definitiva y Experiencia de Usuario (UI/UX)**
2. **Fase 2: Rendimiento y Estabilidad Extrema de Transferencia (MTP y Overclock)**
3. **Fase 3: Conexión con Teléfonos Móviles y Servidor Web HTTP**
4. **Fase 4: Gestión de Datos, Juegos, DLCs y Limpieza de Archivos Basura**
5. **Fase 5: Actualizador Integrado (In-App Updater) y Código QR "Buy Me a Coffee"**

---

## Detalle de Fases

### Fase 1: Corrección Visual Definitiva y UI/UX Horizon OS
*Objetivo: Eliminar cualquier imperfección visual, hack de texto o colapso de cajas.*

- [ ] **1.1. Renderizado Real del Wallpaper (Fondo de Pantalla)**:
  - Modificar el pipeline de dibujado de la ventana para que `nvgImagePattern` dibuje la imagen de fondo en toda la resolución ($1280 \times 720$).
  - Configurar las vistas de contenido y pestañas como translúcidas para que el fondo sea visible debajo de las listas.
  - Asegurar que el deslizador/ciclo de opacidad (`100%`, `75%`, `50%`, `25%`, `0%`) module el canal alfa del fondo contra el color base del tema (`Light`, `Dark`, `AMOLED`).
- [ ] **1.2. Menú Contextual Elegante de Ancho Completo**:
  - Reemplazar el contenedor actual por un diálogo con dimensiones exactas (`640px` de ancho).
  - Diseñar los botones con estilo de lista Horizon OS (icono a la izquierda + texto + chevron o atajo).
  - Diálogo de Propiedades con tabla clave-valor clara y legible sin truncar rutas.
- [ ] **1.3. Selección Múltiple sin Caracteres Unicode (`[ ✔ ]`)**:
  - Eliminar de raíz `lblCheck->setText("✔")`.
  - Diseñar el indicador de selección con una casilla gráfica vectorial (círculo azul/verde con tilde) y un fondo azul translúcido en toda la fila del archivo (`FileRowCell`).
- [ ] **1.4. Lista de Temas e Idiomas Pulida**:
  - En Ajustes, mostrar las 3 tarjetas de tema (`Blanco`, `Negro`, `AMOLED`) con `RadioCell` nativo.
  - El selector de idioma centrado en pantalla sin márgenes truncados.

---

### Fase 2: Rendimiento MTP y Boost de Frecuencias (Overclock)
*Objetivo: Transferencias continuas a 30-40 MB/s para archivos grandes (250 MB a 4+ GB) sin congelamientos.*

- [ ] **2.1. Boost de Frecuencia de CPU y Memoria (Overclock Dinámico)**:
  - Implementar `include/sys_clock.hpp` usando las funciones de libnx:
    - `hosversionGet()` / `pcvSetClockRate()` / `appletSetCpuBoostMode(ApmCpuBoostMode_Type1)`.
    - Elevar la CPU a **1785 MHz** y memoria a **1600 MHz** durante las operaciones activas de MTP y FTP.
    - Restaurar las frecuencias normales al detener el servicio o desconectar el cable para proteger la batería.
- [ ] **2.2. Transferencia MTP Sin Congelamientos**:
  - Buffer de 256 KB en memoria contigua alineada (`alignas(0x1000)`).
  - En `OP_SendObject`: bucle de recepción con verificación estricta de bytes restantes y temporizador de liveness para que Windows Explorer nunca quede bloqueado esperando paquetes.
  - Evitar timeouts USB en archivos grandes transmitiendo paquetes de mantenimiento si el disco tarda en escribir.
- [ ] **2.3. Eliminación Recursiva Limpia sin Error de Desconexión**:
  - Purga inmediata de `g_objs` al borrar carpetas o archivos.
  - Desacoplar el archivo de log para que `sdmc:/switch/EzFiles` o `FileZzz` no tenga candados de escritura.

---

### Fase 3: Conexión Móvil (Smartphones Android / iOS)
*Objetivo: Pasar juegos y archivos desde el celular a la consola con total facilidad.*

- [ ] **3.1. Código QR con Acceso Rápido FTP**:
  - En la pestaña "Servidor FTP", generar y mostrar un código QR en pantalla con la dirección `ftp://<IP>:<PORT>`.
  - Permitir al usuario escanearlo con el móvil para abrir directamente clientes como AndFTP o Documents de iOS.
- [ ] **3.2. Servidor Web HTTP Liviano para Subir Archivos desde el Navegador del Móvil**:
  - Habilitar un micro-servidor HTTP en el puerto `8080` (ej. `http://192.168.1.50:8080`).
  - Al escanear el QR desde el móvil, se abre una página web limpia con un botón "Seleccionar archivos y Enviar a Switch", sin necesidad de instalar ninguna aplicación en el teléfono.

---

### Fase 4: Gestión de Datos, Juegos, DLCs y Limpiador Basura
*Objetivo: Responder a la retroalimentación de la comunidad (características de DBI con interfaz moderna).*

- [ ] **4.1. Verificador de Actualizaciones y DLCs Faltantes**:
  - En la pestaña "Gestión de Datos", al abrir un juego instalado:
    - Mostrar versión base, versión de actualización instalada y cantidad de DLCs activos.
    - Notificar si hay actualizaciones o DLCs pendientes comparando con la base de datos de títulos.
- [ ] **4.2. Instalación Directa de Juegos (NSP, NSZ, XCI)**:
  - Partición MTP virtual "Instalar Juegos" y soporte en FTP para soltar archivos directamente en una carpeta de instalación automática.
- [ ] **4.3. Herramienta de Limpieza de Archivos Basura (Junk Cleaner)**:
  - Escáner para detectar y eliminar con un solo botón:
    - Carpetas vacías huérfanas en la tarjeta SD.
    - Archivos de volcado de errores (`fatal.log`, `crash_dumps`).
    - Archivos temporales de instalación incompletos (`.nca` temporales huérfanos).

---

### Fase 5: Actualizador en la Aplicación y Soporte al Desarrollador
*Objetivo: Mantener la aplicación actualizada sin depender de una PC y recibir donaciones.*

- [ ] **5.1. Auto-Actualizador In-App desde GitHub**:
  - En "Ajustes" / "Acerca de": botón "Buscar Actualizaciones".
  - Consulta a `https://api.github.com/repos/lozp1/FileZzz/releases/latest` vía HTTP/HTTPS.
  - Si hay nueva versión, muestra el changelog, descarga el archivo `FileZzz.nro`, reemplaza el actual y solicita reiniciar.
- [ ] **5.2. Código QR "Buy Me a Coffee"**:
  - En la pestaña "Acerca de", mostrar un código QR vectorial nítido con el enlace de donaciones/apoyo al creador (Buy Me a Coffee / PayPal), junto a los créditos y enlaces a GitHub.

---

## Matriz de Prioridad de Ejecución

| Orden | Módulo | Tareas Clave | Impacto |
|---|---|---|---|
| **1** | **UI/UX Core** | Wallpaper real, menú contextual ancho, quitar `[ ✔ ]` | Crítico (inmediato) |
| **2** | **Overclock & MTP** | CPU Boost 1785 MHz, transferencias 250MB+ fluidas | Crítico (rendimiento) |
| **3** | **QR & Conexión Móvil** | QR para FTP/HTTP, carga desde celular | Alto (comunidad) |
| **4** | **Juegos & Limpiador** | Chequeo DLC/Updates, limpiador de basura | Alto (valor agregado) |
| **5** | **In-App Updater & Coffee** | Auto-update desde GitHub, QR Donación | Medio (mantenimiento) |
