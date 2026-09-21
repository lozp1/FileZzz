# EzFiles

Gestor de archivos para Nintendo Switch (Atmosphere / homebrew `.nro`):
explorador MicroSD, servidor FTP con login del perfil de la consola,
monitor USB y modos de interfaz Visual, Terminal (estilo DBI) y Ranger.

- **Visual**: tarjetas, táctil, temas Dark / OLED / White.
- **Terminal**: réplica de consola estilo DBI con fuente monoespaciada y 6 temas propios.
- **Ranger**: explorador por columnas Miller con vista previa.
- Cambio de modo con **[L]**, 11 idiomas (incluye quechua y k'iche').

## Compilar

Requiere [devkitPro](https://devkitpro.org) con devkitA64, libnx y portlibs (SDL2):

```bash
make
```

Genera `EzFiles.nro` (+ `.nacp`).

## Desplegar

```powershell
.\deploy_switch.ps1
```

o copia `EzFiles.nro` a `sdmc:/switch/EzFiles/EzFiles.nro` (vía DBI MTP, FTP o USB).

## Controles

| Tecla | Acción |
|---|---|
| D-PAD / Stick | Navegar |
| A | Abrir / confirmar |
| B | Volver |
| L | Cambiar modo Visual → Terminal → Ranger |
| Y | Limpiar bitácora (FTP / MTP / consola) |
| + | Opciones de archivo · - Propiedades (explorador) |

FTP: `ftp://<ip-switch>:5000`, usuario y clave = nickname del perfil de la consola.
