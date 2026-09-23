#include <switch.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL2_gfxPrimitives.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "config.hpp"
#include "theme.hpp"
#include "i18n.hpp"
#include "sound.hpp"
#include "log_console.hpp"
#include "ftp_server.hpp"
#include "mtp_usb.hpp"
#include "mtp_ops.hpp"

// Nombre legible del estado USB real (libnx UsbState)
inline const char* usbStateName(int st) {
    switch (st) {
        case UsbState_Detached: return "Desconectado";
        case UsbState_Attached: return "Cable conectado";
        case UsbState_Powered: return "Alimentado";
        case UsbState_Default: return "Detectado";
        case UsbState_Address: return "Direccionado";
        case UsbState_Configured: return "Configurado";
        case UsbState_Suspended: return "Suspendido";
        default: return "Desconocido";
    }
}

// Estados de pantalla del sistema
enum AppScreen {
    SCREEN_SPLASH = 0,
    SCREEN_DASHBOARD,
    SCREEN_EXPLORER,
    SCREEN_MTP,
    SCREEN_FTP,
    SCREEN_LANG,
    SCREEN_THEME,
    SCREEN_ABOUT,
    SCREEN_SYS
};

// Destinos del menú principal (índice 6 = Salir, caso especial).
// Única fuente de verdad para navegación; también la usa fillMenuDefs.
static const AppScreen kMenuTargets[7] = {
    SCREEN_EXPLORER, SCREEN_MTP, SCREEN_FTP, SCREEN_LANG,
    SCREEN_THEME, SCREEN_ABOUT, SCREEN_DASHBOARD // 6: Salir (no navega)
};

// Navegación del menú principal en un solo sitio (táctil, [A], SYS).
inline void navigateToMenuIdx(int idx, AppScreen& currentScreen, int& langSelectIdx,
                              Language currentLanguage, int& themeSelectIdx, int& termSelectIdx,
                              bool& showExitConfirmModal) {
    if (idx < 0 || idx >= 7) return;
    if (idx == 6) { showExitConfirmModal = true; sound::play(sound::SND_ACTION); return; }
    if (idx == 3) langSelectIdx = (int)currentLanguage;
    if (idx == 4) { themeSelectIdx = currentThemeIdx(); termSelectIdx = currentTermThemeIdx; }
    currentScreen = kMenuTargets[idx];
    sound::play(sound::SND_CONFIRM);
}

// Definición visual del menú (títulos/idioma + estilo + destino).
// Se rellena una vez por frame con fillMenuDefs y la usan los 3 modos.
struct MenuDef {
    const char* title;
    const char* sub;
    SDL_Color accent;
    SDL_Texture* icon;
    AppScreen targetScreen;
    bool isExit;
};

inline void fillMenuDefs(MenuDef defs[7], const ThemeColors& tc, SDL_Texture* const icons[7]) {
    SDL_Texture* ie = icons ? icons[0] : nullptr;
    SDL_Texture* iu = icons ? icons[1] : nullptr;
    SDL_Texture* ift = icons ? icons[2] : nullptr;
    SDL_Texture* il = icons ? icons[3] : nullptr;
    SDL_Texture* it = icons ? icons[4] : nullptr;
    SDL_Texture* ia = icons ? icons[5] : nullptr;
    SDL_Texture* ix = icons ? icons[6] : nullptr;
    defs[0] = { tr().menu_explorer, tr().menu_explorer_sub, tc.AccentCyan,   ie, kMenuTargets[0], false };
    defs[1] = { tr().menu_mtp,      tr().menu_mtp_sub,      tc.AccentEmerald, iu, kMenuTargets[1], false };
    defs[2] = { tr().menu_ftp,      tr().menu_ftp_sub,      tc.AccentViolet,  ift, kMenuTargets[2], false };
    defs[3] = { tr().menu_lang,     tr().menu_lang_sub,     tc.AccentBlue,    il, kMenuTargets[3], false };
    defs[4] = { tr().menu_theme,    tr().menu_theme_sub,    tc.AccentAmber,   it, kMenuTargets[4], false };
    defs[5] = { tr().menu_about,    tr().menu_about_sub,    tc.AccentCyan,    ia, kMenuTargets[5], false };
    defs[6] = { tr().menu_exit,     tr().menu_exit_sub,     tc.AccentRed,     ix, kMenuTargets[6], true  };
}

// Título textual por pantalla (cabecera del modo terminal).
inline const char* screenTitle(AppScreen s) {
    switch (s) {
        case SCREEN_DASHBOARD: return "Main menu";
        case SCREEN_EXPLORER:  return "Browse SD Card";
        case SCREEN_MTP:       return "MTP Responder";
        case SCREEN_FTP:       return "FTP Server";
        case SCREEN_LANG:      return "Language";
        case SCREEN_THEME:     return "Themes";
        case SCREEN_ABOUT:     return "About";
        case SCREEN_SYS:       return "System Logs";
        default:               return "";
    }
}

// Clipboard para operaciones de archivos
enum ClipboardOp {
    CLIP_NONE = 0,
    CLIP_COPY,
    CLIP_CUT
};

struct FileItem {
    std::string name;
    bool isDir;
    uint64_t size;
    std::string ext;
    time_t modTime;
};

// Helpers de renderizado de texto con Inter (con caché para no crear texturas cada frame)
struct TextCacheEntry { SDL_Texture* tex = nullptr; int w = 0; int h = 0; };
inline std::unordered_map<std::string, TextCacheEntry>& textCache() {
    static std::unordered_map<std::string, TextCacheEntry> c;
    return c;
}
inline void clearTextCache() {
    for (auto& kv : textCache()) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    textCache().clear();
}
inline std::string textCacheKey(TTF_Font* font, const std::string& text, SDL_Color col) {
    char key[64];
    snprintf(key, sizeof(key), "%p#%02x%02x%02x%02x#", (void*)font, col.r, col.g, col.b, col.a);
    return std::string(key) + text;
}
inline const TextCacheEntry* getCachedText(SDL_Renderer* ren, TTF_Font* font, const std::string& text, SDL_Color col) {
    if (!font || text.empty()) return nullptr;
    std::string key = textCacheKey(font, text, col);
    auto& cache = textCache();
    auto it = cache.find(key);
    if (it != cache.end()) return &it->second;
    if (cache.size() > 500) clearTextCache();
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), col);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (!tex) { SDL_FreeSurface(surf); return nullptr; }
    TextCacheEntry e{ tex, surf->w, surf->h };
    SDL_FreeSurface(surf);
    auto res = cache.emplace(std::move(key), e);
    return &res.first->second;
}
void renderText(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int x, int y, SDL_Color col) {
    const TextCacheEntry* e = getCachedText(ren, font, text, col);
    if (!e) return;
    SDL_Rect dst = { x, y, e->w, e->h };
    SDL_RenderCopy(ren, e->tex, NULL, &dst);
}

void renderTextCentered(SDL_Renderer* ren, TTF_Font* font, const std::string& text, int cx, int y, SDL_Color col) {
    const TextCacheEntry* e = getCachedText(ren, font, text, col);
    if (!e) return;
    SDL_Rect dst = { cx - (e->w / 2), y, e->w, e->h };
    SDL_RenderCopy(ren, e->tex, NULL, &dst);
}

// Consola de bitácora estilo DBI: caja con las últimas líneas del log.
// Recorta líneas largas al ancho disponible (fuente ~7px por carácter).
void drawLogConsole(SDL_Renderer* ren, TTF_Font* font, int x, int y, int w, int h,
                    const ThemeColors& tc, size_t maxLines, const char* title) {
    boxRGBA(ren, x, y, x + w, y + h, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
    rectangleRGBA(ren, x, y, x + w, y + h, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
    renderText(ren, font, title, x + 14, y + 8, tc.TextMuted);
    lineRGBA(ren, x + 14, y + 28, x + w - 14, y + 28, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
    std::vector<std::string> lines = logcon::last(maxLines);
    int rowH = 20;
    int maxChars = (w - 28) / 7;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string ln = lines[i];
        if ((int)ln.length() > maxChars) ln = ln.substr(0, maxChars - 3) + "...";
        SDL_Color c = tc.TextSecondary;
        if (ln.find("ERR") != std::string::npos || ln.find("FAIL") != std::string::npos) c = tc.AccentRed;
        else if (ln.find(" OK") != std::string::npos || ln.find("conectado") != std::string::npos) c = tc.AccentEmerald;
        renderText(ren, font, ln, x + 14, y + 36 + (int)i * rowH, c);
    }
    if (lines.empty())
        renderText(ren, font, "-- sin eventos --", x + 14, y + 36, tc.TextMuted);
}

// Formateo de bytes a cadena legible
std::string formatSize(uint64_t bytes) {
    char buf[32];
    if (bytes < 1024) snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else if (bytes < 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024 * 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
    else snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return std::string(buf);
}

// Formateo de fecha de modificación
std::string formatTime(time_t t) {
    if (t == 0) return "--";
    struct tm* ltime = localtime(&t);
    if (!ltime) return "--";
    char buf[64];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", ltime);
    return std::string(buf);
}

// Exploración de directorio en la MicroSD
std::vector<FileItem> getFolderItems(const std::string& path) {
    std::vector<FileItem> list;
    DIR* dir = opendir(path.c_str());
    if (!dir) return list;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        FileItem item;
        item.name = entry->d_name;
        item.isDir = (entry->d_type == DT_DIR);
        item.size = 0;
        item.modTime = 0;

        std::string fullPath = path + (path.back() == '/' ? "" : "/") + item.name;
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            item.isDir = S_ISDIR(st.st_mode);
            item.size = st.st_size;
            item.modTime = st.st_mtime;
        }

        if (!item.isDir) {
            size_t dot = item.name.find_last_of('.');
            if (dot != std::string::npos) {
                item.ext = item.name.substr(dot);
                std::transform(item.ext.begin(), item.ext.end(), item.ext.begin(), ::tolower);
            }
        }
        list.push_back(item);
    }
    closedir(dir);

    std::sort(list.begin(), list.end(), [](const FileItem& a, const FileItem& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return a.name < b.name;
    });

    return list;
}

// Operación de copia física de archivo con búfer de 64KB
bool copyFileStream(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in.is_open()) return false;
    std::ofstream out(dst, std::ios::binary);
    if (!out.is_open()) return false;

    char buffer[65536];
    while (in) {
        in.read(buffer, sizeof(buffer));
        std::streamsize got = in.gcount();
        if (got > 0) out.write(buffer, got);
    }
    return in.eof() && out.good();
}

bool pathExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool isDirectoryPath(const std::string& p) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    if (base.back() == '/') return base + name;
    return base + "/" + name;
}

// Copia recursiva (archivos y carpetas). No sobrescribe: falla si dst existe.
bool copyRecursive(const std::string& src, const std::string& dst) {
    struct stat st;
    if (stat(src.c_str(), &st) != 0) return false;
    if (pathExists(dst)) return false;
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst.c_str(), 0777) != 0) return false;
        DIR* d = opendir(src.c_str());
        if (!d) return false;
        bool ok = true;
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (!copyRecursive(joinPath(src, e->d_name), joinPath(dst, e->d_name))) { ok = false; break; }
        }
        closedir(d);
        return ok;
    }
    return copyFileStream(src, dst);
}

// Borrado recursivo. true si todo se eliminó.
bool deleteRecursive(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path.c_str());
        if (!d) return false;
        bool ok = true;
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (!deleteRecursive(joinPath(path, e->d_name))) { ok = false; break; }
        }
        closedir(d);
        if (!ok) return false;
        return rmdir(path.c_str()) == 0;
    }
    return remove(path.c_str()) == 0;
}

// Renderizar logo oficial EzFiles sin borde azul
void drawCleanLogo(SDL_Renderer* ren, SDL_Texture* logoTex, int x, int y, int size) {
    if (logoTex) {
        SDL_Rect dst = { x, y, size, size };
        SDL_RenderCopy(ren, logoTex, NULL, &dst);
    }
}

// Nickname del perfil activo de la consola (para user/pass FTP).
// Fallback "switch" si acc:u0 no disponible o sin usuarios.
inline std::string getConsoleUsername() {
    std::string name = "switch";
    if (R_SUCCEEDED(accountInitialize(AccountServiceType_Application))) {
        AccountUid uid{};
        bool got = R_SUCCEEDED(accountGetPreselectedUser(&uid));
        if (!got) {
            AccountUid uids[8];
            s32 total = 0;
            if (R_SUCCEEDED(accountListAllUsers(uids, 8, &total)) && total > 0) {
                uid = uids[0];
                got = true;
            }
        }
        if (got) {
            AccountProfile prof;
            if (R_SUCCEEDED(accountGetProfile(&prof, uid))) {
                AccountProfileBase base;
                memset(&base, 0, sizeof(base));
                if (R_SUCCEEDED(accountProfileGet(&prof, nullptr, &base)) && base.nickname[0]) {
                    char tmp[0x20 + 1] = {0};
                    memcpy(tmp, base.nickname, 0x20);
                    name = tmp;
                }
                accountProfileClose(&prof);
            }
        }
    }
    // FTP parte comando/arg por el primer espacio: sin espacios ni vacío.
    name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
    name.erase(std::remove(name.begin(), name.end(), '\r'), name.end());
    name.erase(std::remove(name.begin(), name.end(), '\n'), name.end());
    if (name.empty() || name.size() > 24) name = "switch";
    return name;
}

// Invocación del teclado oficial de Nintendo Switch para renombrar
bool showKeyboardRename(const std::string& oldName, std::string& outNewName) {
    SwkbdConfig kbd;
    char outBuffer[256] = {0};
    if (R_SUCCEEDED(swkbdCreate(&kbd, 0))) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetInitialText(&kbd, oldName.c_str());
        swkbdConfigSetHeaderText(&kbd, "Renombrar archivo / carpeta");
        swkbdConfigSetGuideText(&kbd, "Introduce el nuevo nombre");
        Result rc = swkbdShow(&kbd, outBuffer, sizeof(outBuffer));
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc) && strlen(outBuffer) > 0) {
            outNewName = std::string(outBuffer);
            return true;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    romfsInit();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);
    socketInitializeDefault();
    // usb:ds lo gestiona mtp_usb::setup()/teardown() internamente.
    bool usbAvailable = true; // Confirmado cuando el usuario activa MTP

    ftp_server::g_logCb = [](const char* s) { logcon::push(s ? s : ""); };
    mtp_usb::g_logCb = [](const char* s) { logcon::push(s ? s : ""); };
    std::string consoleUser = getConsoleUsername();
    ftp_server::g_ftpUser = consoleUser;
    ftp_server::g_ftpPass = consoleUser;
    logcon::push("EzFiles v1.3.0 iniciado");
    logcon::push("FTP user: " + consoleUser);
    logcon::push(std::string("USB detector: ") + (usbAvailable ? "OK" : "no disponible"));
    // MTP se activa cuando el usuario presiona [A] en SCREEN_MTP

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "2");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return 1;
    if (TTF_Init() < 0) return 1;
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP);

    SDL_Window* window = SDL_CreateWindow(
        "EzFiles - Andromeda Suite",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_SHOWN
    );
    if (!window) return 1;

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) return 1;

    sound::initAudio();

    TTF_Font* fontLogo  = TTF_OpenFont("romfs:/fonts/Inter.ttf", 26);
    TTF_Font* fontTitle = TTF_OpenFont("romfs:/fonts/Inter.ttf", 20);
    TTF_Font* fontBody  = TTF_OpenFont("romfs:/fonts/Inter.ttf", 16);
    TTF_Font* fontSmall = TTF_OpenFont("romfs:/fonts/Inter.ttf", 13);
    TTF_Font* fontJet   = TTF_OpenFont("romfs:/fonts/CaskaydiaCoveNerdFont-Regular.ttf", 16);

    SDL_Texture* ezfilesLogoTex = NULL;
    SDL_Surface* ezSurf = IMG_Load("romfs:/img/ezfiles_logo.jpg");
    if (ezSurf) {
        ezfilesLogoTex = SDL_CreateTextureFromSurface(renderer, ezSurf);
        SDL_FreeSurface(ezSurf);
    }

    auto loadTex = [renderer](const char* path) -> SDL_Texture* {
        SDL_Surface* s = IMG_Load(path);
        if (!s) return NULL;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FreeSurface(s);
        return t;
    };

    SDL_Texture* icoExplorer  = loadTex("romfs:/img/icon_explorer.png");
    SDL_Texture* icoUsb       = loadTex("romfs:/img/icon_usb.png");
    SDL_Texture* icoFtp       = loadTex("romfs:/img/icon_ftp.png");
    SDL_Texture* icoLang      = loadTex("romfs:/img/icon_lang.png");
    SDL_Texture* icoAbout     = loadTex("romfs:/img/icon_about.png");
    SDL_Texture* icoTheme     = loadTex("romfs:/img/icon_theme.png");
    SDL_Texture* icoExit      = loadTex("romfs:/img/icon_exit.png");

    SDL_Texture* icoFolderSm  = loadTex("romfs:/img/icon_folder_sm.png");
    SDL_Texture* icoFileSm    = loadTex("romfs:/img/icon_file_sm.png");
    SDL_Texture* icoNspSm     = loadTex("romfs:/img/icon_nsp_sm.png");
    SDL_Texture* icoNroSm     = loadTex("romfs:/img/icon_nro_sm.png");
    SDL_Texture* icoSavSm     = loadTex("romfs:/img/icon_sav_sm.png");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    AppConfig::get().load();

    AppScreen currentScreen = SCREEN_SPLASH;
    int menuIdx = 0;
    const int menuTotalItems = 7;

    int fileIdx = 0;
    int scrollOffset = 0;
    const int maxVisibleFiles = 9;

    std::string currentPath = "sdmc:/";
    std::vector<FileItem> files = getFolderItems(currentPath);

    bool showContextModal = false;
    int contextMenuIdx = 0;
    const int contextMenuCount = 6; // Copiar, Cortar, Pegar, Renombrar, Eliminar, Propiedades

    bool showDeleteConfirmModal = false;
    bool showPropertiesModal = false;

    int langSelectIdx = (int)currentLanguage;

    ClipboardOp clipboardOp = CLIP_NONE;
    std::string clipboardSrcPath = "";
    std::string clipboardItemName = "";

    std::string toastMessage = "";
    int toastTimer = 0;

    bool ftpRunning = false;

    int globalFrame = 0;
    int splashFrames = 0;
    const int maxSplashFrames = 80;

    int repeatUpTimer = 0;
    int repeatDownTimer = 0;
    const int REPEAT_DELAY = 15;
    const int REPEAT_RATE = 3;

    int touchPrevY = -1;
    bool wasTouch = false;
    // Reloj/batería cacheados a 1Hz para no llamar a syscalls cada frame
    u32 cachedBatt = 100;
    char cachedTime[32] = "--:--:--";
    char cachedDate[32] = "--.--.----";
    int clockCountdown = 0;
    int lastUsbRaw = -1;
    // Espacio SD cacheado (solo se consulta en pantalla SYS)
    unsigned long long sdFree = 0, sdTotal = 0;

    // === MODOS DE INTERFAZ ===
    enum UIMode { UI_MODERN = 0, UI_DBI, UI_RANGER };
    UIMode uiMode = UI_MODERN;

    // === ANIMACIONES (60fps Andromeda-style) ===
    int animFrame = 0;
    int screenEnterFrame = 0;
    AppScreen prevScreenAnim = SCREEN_SPLASH;
    bool showExitConfirmModal = false;
    int themeSelectIdx = 0;
    int termSelectIdx = 0;

    bool running = true;
    while (running && appletMainLoop()) {
        globalFrame++;
        animFrame++;
        screenEnterFrame++;
        if (currentScreen != prevScreenAnim) { screenEnterFrame = 0; prevScreenAnim = currentScreen; }
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);

        HidTouchScreenState touchState;
        hidGetTouchScreenStates(&touchState, 1);
        bool isTouch = (touchState.count > 0);
        int touchX = isTouch ? touchState.touches[0].x : -1;
        int touchY = isTouch ? touchState.touches[0].y : -1;
        bool touchPressed = isTouch && !wasTouch;

        if (clockCountdown <= 0) {
            psmGetBatteryChargePercentage(&cachedBatt);
            time_t rt; time(&rt);
            struct tm* ti = localtime(&rt);
            if (ti) {
                snprintf(cachedTime, sizeof(cachedTime), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
                snprintf(cachedDate, sizeof(cachedDate), "%02d.%02d.%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
            }
            if (mtp_usb::g_ready) {
                UsbState ust = UsbState_Detached;
                if (R_SUCCEEDED(usbDsGetState(&ust)) && (int)ust != lastUsbRaw) {
                    lastUsbRaw = (int)ust;
                    logcon::push(std::string("USB estado: ") + usbStateName((int)ust));
                }
                mtp_usb::poll();
            }
            if (currentScreen == SCREEN_SYS) {
                struct statvfs sv;
                if (statvfs("sdmc:/", &sv) == 0) {
                    sdTotal = (unsigned long long)sv.f_blocks * sv.f_frsize;
                    sdFree = (unsigned long long)sv.f_bavail * sv.f_frsize;
                }
            }
            clockCountdown = 60;
        } else clockCountdown--;

        if (toastTimer > 0) toastTimer--;

        // [L]: cicla Visual -> Terminal -> Ranger (menos splash y modales)
        if ((kDown & HidNpadButton_L) && currentScreen != SCREEN_SPLASH &&
            !showContextModal && !showDeleteConfirmModal && !showPropertiesModal && !showExitConfirmModal) {
            uiMode = (UIMode)((uiMode + 1) % 3);
            clearTextCache();
            sound::play(sound::SND_ACTION);
            toastMessage = std::string("Modo: ") + (uiMode == UI_MODERN ? "Visual" : (uiMode == UI_DBI ? "Terminal" : "Ranger"));
            toastTimer = 60;
        }

        // --- MANEJO DE AUTOREPEAT FLUIDO ---
        bool doMoveUp = false;
        bool doMoveDown = false;

        if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            doMoveUp = true;
            repeatUpTimer = 0;
        } else if (kHeld & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            repeatUpTimer++;
            if (repeatUpTimer >= REPEAT_DELAY && ((repeatUpTimer - REPEAT_DELAY) % REPEAT_RATE == 0)) {
                doMoveUp = true;
            }
        } else {
            repeatUpTimer = 0;
        }

        if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            doMoveDown = true;
            repeatDownTimer = 0;
        } else if (kHeld & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            repeatDownTimer++;
            if (repeatDownTimer >= REPEAT_DELAY && ((repeatDownTimer - REPEAT_DELAY) % REPEAT_RATE == 0)) {
                doMoveDown = true;
            }
        } else {
            repeatDownTimer = 0;
        }

        // --- PANTALLA 1: SPLASH SCREEN ---
        if (currentScreen == SCREEN_SPLASH) {
            splashFrames++;
            if (splashFrames >= maxSplashFrames || (kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))) {
                currentScreen = SCREEN_DASHBOARD;
                sound::play(sound::SND_CONFIRM);
            }
        }
        // --- PANTALLA 2: DASHBOARD ---
        else if (currentScreen == SCREEN_DASHBOARD) {
            if (doMoveUp) {
                menuIdx = (menuIdx + menuTotalItems - 1) % menuTotalItems;
                sound::play(sound::SND_NAV);
            }
            if (doMoveDown) {
                menuIdx = (menuIdx + 1) % menuTotalItems;
                sound::play(sound::SND_NAV);
            }

            // Toque en tarjetas (solo flanco de bajada: evita repetición al mantener)
            if (touchPressed && touchY >= 170 && touchY <= 650) {
                const int dashCardH = 60;
                const int dashStep = 68;
                int startY = 175;
                for (int i = 0; i < menuTotalItems; i++) {
                    int cY = startY + i * dashStep;
                    if (touchX >= 80 && touchX <= 1200 && touchY >= cY && touchY <= cY + dashCardH) {
                        menuIdx = i;
                        navigateToMenuIdx(menuIdx, currentScreen, langSelectIdx, currentLanguage,
                                          themeSelectIdx, termSelectIdx, showExitConfirmModal);
                        break;
                    }
                }
            }

            if (!showExitConfirmModal && (kDown & HidNpadButton_A)) {
                navigateToMenuIdx(menuIdx, currentScreen, langSelectIdx, currentLanguage,
                                  themeSelectIdx, termSelectIdx, showExitConfirmModal);
            }

            if (kDown & HidNpadButton_B) {
                if (showExitConfirmModal) {
                    showExitConfirmModal = false;
                    sound::play(sound::SND_BACK);
                } else {
                    running = false;
                }
            }
            if (showExitConfirmModal && (kDown & HidNpadButton_A)) {
                running = false;
            }
        }
        // --- PANTALLA 3: EXPLORADOR ---
        else if (currentScreen == SCREEN_EXPLORER) {
            int totalFiles = (int)files.size();

            // Swipe táctil en explorador
            if (isTouch && !showContextModal && !showDeleteConfirmModal && !showPropertiesModal) {
                if (touchPrevY >= 0) {
                    int delta = touchY - touchPrevY;
                    if (delta > 22 && fileIdx > 0) {
                        fileIdx--;
                        if (fileIdx < scrollOffset) scrollOffset = fileIdx;
                        touchPrevY = touchY;
                    } else if (delta < -22 && fileIdx < totalFiles - 1) {
                        fileIdx++;
                        if (fileIdx >= scrollOffset + maxVisibleFiles) scrollOffset = fileIdx - maxVisibleFiles + 1;
                        touchPrevY = touchY;
                    }
                } else {
                    touchPrevY = touchY;
                }
            } else {
                touchPrevY = -1;
            }

            if (showDeleteConfirmModal) {
                if (kDown & HidNpadButton_A) {
                    if (!files.empty() && fileIdx < (int)files.size()) {
                        std::string target = joinPath(currentPath, files[fileIdx].name);
                        bool ok = deleteRecursive(target);

                        sound::play(ok ? sound::SND_DELETE : sound::SND_BACK);
                        toastMessage = ok ? tr().toast_deleted : tr().toast_copy_err;
                        logcon::push(std::string("FS eliminar ") + (ok ? "OK " : "ERR ") + files[fileIdx].name);
                        toastTimer = 90;

                        files = getFolderItems(currentPath);
                        if (fileIdx >= (int)files.size() && fileIdx > 0) fileIdx--;
                    }
                    showDeleteConfirmModal = false;
                }
                if (kDown & (HidNpadButton_B | HidNpadButton_X)) {
                    sound::play(sound::SND_BACK);
                    showDeleteConfirmModal = false;
                }
            }
            else if (showPropertiesModal) {
                if (kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus)) {
                    sound::play(sound::SND_BACK);
                    showPropertiesModal = false;
                }
            }
            else if (showContextModal) {
                if (doMoveUp) {
                    contextMenuIdx = (contextMenuIdx + contextMenuCount - 1) % contextMenuCount;
                    sound::play(sound::SND_NAV);
                }
                if (doMoveDown) {
                    contextMenuIdx = (contextMenuIdx + 1) % contextMenuCount;
                    sound::play(sound::SND_NAV);
                }
                if (kDown & HidNpadButton_B) {
                    sound::play(sound::SND_BACK);
                    showContextModal = false;
                }

                if (kDown & HidNpadButton_A) {
                    if (!files.empty() && fileIdx < (int)files.size()) {
                        const FileItem& cur = files[fileIdx];
                        std::string fullPath = joinPath(currentPath, cur.name);

                        if (contextMenuIdx == 0) { // Copiar
                            clipboardOp = CLIP_COPY;
                            clipboardSrcPath = fullPath;
                            clipboardItemName = cur.name;
                            toastMessage = std::string(tr().exp_copied_badge) + " " + cur.name;
                            toastTimer = 90;
                            sound::play(sound::SND_ACTION);
                            showContextModal = false;
                        } else if (contextMenuIdx == 1) { // Cortar
                            clipboardOp = CLIP_CUT;
                            clipboardSrcPath = fullPath;
                            clipboardItemName = cur.name;
                            toastMessage = std::string(tr().exp_cut_badge) + " " + cur.name;
                            toastTimer = 90;
                            sound::play(sound::SND_ACTION);
                            showContextModal = false;
                        } else if (contextMenuIdx == 2) { // Pegar
                            if (clipboardOp != CLIP_NONE && !clipboardSrcPath.empty()) {
                                std::string destPath = joinPath(currentPath, clipboardItemName);
                                bool ok = false;
                                if (pathExists(destPath)) {
                                    ok = false;
                                    toastMessage = tr().toast_copy_err;
                                } else if (clipboardOp == CLIP_COPY) {
                                    ok = copyRecursive(clipboardSrcPath, destPath);
                                    toastMessage = ok ? tr().toast_copied : tr().toast_copy_err;
                                    logcon::push(std::string("FS copiar ") + (ok ? "OK " : "ERR ") + clipboardItemName);
                                } else if (clipboardOp == CLIP_CUT) {
                                    if (clipboardSrcPath == destPath) {
                                        ok = true;
                                        toastMessage = tr().toast_moved;
                                    } else if (pathExists(destPath)) {
                                        ok = false;
                                        toastMessage = tr().toast_copy_err;
                                    } else {
                                        ok = (rename(clipboardSrcPath.c_str(), destPath.c_str()) == 0);
                                        if (!ok) {
                                            ok = copyRecursive(clipboardSrcPath, destPath);
                                            if (ok) deleteRecursive(clipboardSrcPath);
                                        }
                                        toastMessage = ok ? tr().toast_moved : tr().toast_copy_err;
                                        logcon::push(std::string("FS mover ") + (ok ? "OK " : "ERR ") + clipboardItemName);
                                    }
                                    clipboardOp = CLIP_NONE;
                                    clipboardSrcPath = "";
                                }
                                sound::play(ok ? sound::SND_CONFIRM : sound::SND_BACK);
                                toastTimer = 90;
                                files = getFolderItems(currentPath);
                            }
                            showContextModal = false;
                        } else if (contextMenuIdx == 3) { // Renombrar con teclado oficial
                            showContextModal = false;
                            std::string newName;
                            if (showKeyboardRename(cur.name, newName)) {
                                std::string newPath = joinPath(currentPath, newName);
                                if (newName == cur.name) {
                                    // Sin cambios
                                } else if (pathExists(newPath)) {
                                    sound::play(sound::SND_BACK);
                                    toastMessage = tr().toast_rename_err;
                                    toastTimer = 90;
                                } else if (rename(fullPath.c_str(), newPath.c_str()) == 0) {
                                    sound::play(sound::SND_CONFIRM);
                                    toastMessage = tr().toast_renamed;
                                    logcon::push(std::string("FS renombrar OK ") + newName);
                                    files = getFolderItems(currentPath);
                                } else {
                                    sound::play(sound::SND_BACK);
                                    toastMessage = tr().toast_rename_err;
                                    logcon::push(std::string("FS renombrar ERR ") + cur.name);
                                }
                                toastTimer = 90;
                            }
                        } else if (contextMenuIdx == 4) { // Eliminar
                            showContextModal = false;
                            showDeleteConfirmModal = true;
                            sound::play(sound::SND_BACK);
                        } else if (contextMenuIdx == 5) { // Propiedades
                            showContextModal = false;
                            showPropertiesModal = true;
                            sound::play(sound::SND_ACTION);
                        }
                    } else {
                        showContextModal = false;
                    }
                }
            }
            else {
                if (doMoveUp) {
                    if (fileIdx > 0) {
                        fileIdx--;
                        if (fileIdx < scrollOffset) scrollOffset = fileIdx;
                        sound::play(sound::SND_NAV);
                    }
                }
                if (doMoveDown) {
                    if (fileIdx < totalFiles - 1) {
                        fileIdx++;
                        if (fileIdx >= scrollOffset + maxVisibleFiles) scrollOffset = fileIdx - maxVisibleFiles + 1;
                        sound::play(sound::SND_NAV);
                    }
                }

                if (kDown & (HidNpadButton_Plus | HidNpadButton_X)) {
                    if (!files.empty()) {
                        contextMenuIdx = (clipboardOp != CLIP_NONE) ? 2 : 0;
                        showContextModal = true;
                        sound::play(sound::SND_ACTION);
                    }
                }

                if ((kDown & HidNpadButton_Y) && clipboardOp != CLIP_NONE && !clipboardSrcPath.empty()) {
                    std::string destPath = joinPath(currentPath, clipboardItemName);
                    bool ok = false;
                    if (pathExists(destPath)) {
                        ok = false;
                        toastMessage = tr().toast_copy_err;
                    } else if (clipboardOp == CLIP_COPY) {
                        ok = copyRecursive(clipboardSrcPath, destPath);
                        toastMessage = ok ? tr().toast_copied : tr().toast_copy_err;
                        logcon::push(std::string("FS copiar ") + (ok ? "OK " : "ERR ") + clipboardItemName);
                    } else if (clipboardOp == CLIP_CUT) {
                        ok = (rename(clipboardSrcPath.c_str(), destPath.c_str()) == 0);
                        if (!ok) {
                            ok = copyRecursive(clipboardSrcPath, destPath);
                            if (ok) deleteRecursive(clipboardSrcPath);
                        }
                        logcon::push(std::string("FS mover ") + (ok ? "OK " : "ERR ") + clipboardItemName);
                        toastMessage = ok ? tr().toast_moved : tr().toast_copy_err;
                        clipboardOp = CLIP_NONE;
                        clipboardSrcPath = "";
                    }
                    sound::play(ok ? sound::SND_CONFIRM : sound::SND_BACK);
                    toastTimer = 90;
                    files = getFolderItems(currentPath);
                }

                if (kDown & HidNpadButton_Minus) {
                    if (!files.empty() && fileIdx < (int)files.size()) {
                        showPropertiesModal = true;
                        sound::play(sound::SND_ACTION);
                    }
                }

                if (kDown & HidNpadButton_A) {
                    if (!files.empty() && fileIdx < totalFiles) {
                        if (files[fileIdx].isDir) {
                            currentPath = currentPath + (currentPath.back() == '/' ? "" : "/") + files[fileIdx].name + "/";
                            files = getFolderItems(currentPath);
                            fileIdx = 0;
                            scrollOffset = 0;
                            sound::play(sound::SND_CONFIRM);
                        } else {
                            contextMenuIdx = 0;
                            showContextModal = true;
                            sound::play(sound::SND_ACTION);
                        }
                    }
                }

                if (kDown & HidNpadButton_B) {
                    sound::play(sound::SND_BACK);
                    if (currentPath == "sdmc:/") {
                        currentScreen = SCREEN_DASHBOARD;
                    } else {
                        size_t lastSlash = currentPath.find_last_of('/', currentPath.length() - 2);
                        if (lastSlash != std::string::npos) currentPath = currentPath.substr(0, lastSlash + 1);
                        else currentPath = "sdmc:/";
                        files = getFolderItems(currentPath);
                        fileIdx = 0;
                        scrollOffset = 0;
                    }
                }
            }
        }
        // --- PANTALLA 4: CONEXIÓN USB (MTP) ---
        else if (currentScreen == SCREEN_MTP) {
            if (kDown & HidNpadButton_A) {
                sound::play(sound::SND_ACTION);
                if (mtp_ops::running() || mtp_usb::g_ready) {
                    mtp_ops::stop();
                    mtp_usb::teardown();
                    logcon::push("MTP detenido");
                } else {
                    logcon::push("MTP iniciando conexion USB...");
                    if (mtp_usb::setup()) {
                        mtp_ops::start();
                        logcon::push("MTP servidor activo!");
                    } else {
                        logcon::push("MTP error al inicializar USB");
                    }
                }
            }
            if (kDown & HidNpadButton_Y) {
                logcon::clear();
                sound::play(sound::SND_NAV);
            }
            if (kDown & HidNpadButton_B) {
                sound::play(sound::SND_BACK);
                if (mtp_ops::running() || mtp_usb::g_ready) {
                    mtp_ops::stop();
                    mtp_usb::teardown();
                    logcon::push("MTP desconectado");
                }
                currentScreen = SCREEN_DASHBOARD;
            }
        }
        // --- PANTALLA 5: SERVIDOR FTP ---
        else if (currentScreen == SCREEN_FTP) {
            if (kDown & HidNpadButton_Y) {
                logcon::clear();
                sound::play(sound::SND_NAV);
            }
            if (kDown & HidNpadButton_A) {
                if (!ftpRunning) {
                    ftp_server::start(5000);
                    ftpRunning = true;
                    sound::play(sound::SND_CONFIRM);
                    toastMessage = tr().toast_ftp_on;
                    toastTimer = 90;
                } else {
                    ftp_server::stop();
                    ftpRunning = false;
                    sound::play(sound::SND_BACK);
                    toastMessage = tr().toast_ftp_off;
                    toastTimer = 60;
                }
            }
            if (kDown & HidNpadButton_B) {
                sound::play(sound::SND_BACK);
                if (ftpRunning) {
                    ftp_server::stop();
                    ftpRunning = false;
                    logcon::push("FTP desconectado");
                }
                currentScreen = SCREEN_DASHBOARD;
            }
        }
        // --- PANTALLA: SELECTOR DE TEMAS (segun modo: visual o terminal/ranger) ---
        else if (currentScreen == SCREEN_THEME) {
            bool termMode = (uiMode != UI_MODERN);
            if (kDown & HidNpadButton_Y) {
                currentTermThemeIdx = (currentTermThemeIdx + 1) % getTermThemeCount();
                termSelectIdx = currentTermThemeIdx;
                sound::play(sound::SND_ACTION);
                toastMessage = std::string("Tema terminal: ") + TERM_THEMES[currentTermThemeIdx].name;
                toastTimer = 60;
            }
            if (termMode) {
                if (doMoveUp) { termSelectIdx = (termSelectIdx + getTermThemeCount() - 1) % getTermThemeCount(); sound::play(sound::SND_NAV); }
                if (doMoveDown) { termSelectIdx = (termSelectIdx + 1) % getTermThemeCount(); sound::play(sound::SND_NAV); }
                if (kDown & HidNpadButton_A) {
                    currentTermThemeIdx = termSelectIdx;
                    sound::play(sound::SND_CONFIRM);
                    toastMessage = std::string("Tema terminal: ") + TERM_THEMES[currentTermThemeIdx].name;
                    toastTimer = 90;
                }
            } else {
                if (doMoveUp) {
                    themeSelectIdx = (themeSelectIdx + getThemeCount() - 1) % getThemeCount();
                    sound::play(sound::SND_NAV);
                }
                if (doMoveDown) {
                    themeSelectIdx = (themeSelectIdx + 1) % getThemeCount();
                    sound::play(sound::SND_NAV);
                }
                if (kDown & HidNpadButton_A) {
                    setTheme(themeSelectIdx);
                    clearTextCache();
                    sound::play(sound::SND_CONFIRM);
                    toastMessage = std::string(tr().toast_theme) + THEMES[themeSelectIdx].name;
                    toastTimer = 90;
                }
            }
            if (kDown & HidNpadButton_B) {
                sound::play(sound::SND_BACK);
                currentScreen = SCREEN_DASHBOARD;
            }
        }
        // --- PANTALLA 6: IDIOMAS (11 IDIOMAS) ---
        else if (currentScreen == SCREEN_LANG) {
            if (doMoveUp) {
                langSelectIdx = (langSelectIdx + LANG_COUNT - 1) % LANG_COUNT;
                sound::play(sound::SND_NAV);
            }
            if (doMoveDown) {
                langSelectIdx = (langSelectIdx + 1) % LANG_COUNT;
                sound::play(sound::SND_NAV);
            }

            if (kDown & HidNpadButton_A) {
                setLanguage((Language)langSelectIdx);
                clearTextCache();
                sound::play(sound::SND_CONFIRM);
                toastMessage = std::string(tr().toast_lang) + tr().lang_name;
                toastTimer = 90;
            }

            if (kDown & HidNpadButton_B) {
                sound::play(sound::SND_BACK);
                currentScreen = SCREEN_DASHBOARD;
            }
        }
        // --- PANTALLA 7: ACERCA DE (Salir con [A] o [B]) ---
        else if (currentScreen == SCREEN_ABOUT) {
            if (kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus)) {
                sound::play(sound::SND_BACK);
                currentScreen = SCREEN_DASHBOARD;
            }
        }
        // --- PANTALLA 8: MODO TERMINAL ESTILO DBI ---
        else if (currentScreen == SCREEN_SYS) {
            if (doMoveUp) {
                menuIdx = (menuIdx + menuTotalItems - 1) % menuTotalItems;
                sound::play(sound::SND_NAV);
            }
            if (doMoveDown) {
                menuIdx = (menuIdx + 1) % menuTotalItems;
                sound::play(sound::SND_NAV);
            }
            if (kDown & HidNpadButton_A) {
                navigateToMenuIdx(menuIdx, currentScreen, langSelectIdx, currentLanguage,
                                  themeSelectIdx, termSelectIdx, showExitConfirmModal);
            }
            if (kDown & HidNpadButton_B) {
                sound::play(sound::SND_BACK);
                currentScreen = SCREEN_DASHBOARD;
            }
        }

        // =========================================================================
        // MODO DBI: RENDER MINIMALISTA (réplica exacta de DBI)
        // =========================================================================
        // Menú único del frame (títulos + estilo + iconos + destinos): lo usan los 3 modos.
        const ThemeColors& tc = curTheme();
        SDL_Texture* menuIcons[7] = { icoExplorer, icoUsb, icoFtp, icoLang, icoTheme, icoAbout, icoExit };
        MenuDef defs[7];
        fillMenuDefs(defs, tc, menuIcons);

        if ((uiMode == UI_DBI || uiMode == UI_RANGER) && currentScreen != SCREEN_SPLASH) {
            const TerminalTheme& tt = curTermTheme();
            SDL_SetRenderDrawColor(renderer, tt.bg.r, tt.bg.g, tt.bg.b, tt.bg.a);
            SDL_RenderClear(renderer);
            SDL_Color W = tt.fg; 
            SDL_Color B = tt.selectBg;
            SDL_Color Green = tt.accent;

            // ── BORDES Y CABECERAS ESTILO DBI ──────────────────────────────────────────
            // Línea superior con texto intercalado (simulado con líneas separadas o texto encima)
            const char* modeIndicator = (uiMode == UI_DBI) ? "TERMINAL" : "RANGER";
            char headerBuf[64];
            snprintf(headerBuf, sizeof(headerBuf), "-EZFILES v1.3 [%s]-", modeIndicator);
            renderText(renderer, fontJet, headerBuf, 0, 4, W);
            char dateStr[64];
            snprintf(dateStr, sizeof(dateStr), "-%s %s-", cachedDate, cachedTime);
            int dateW = 0, dateH = 0;
            TTF_SizeUTF8(fontJet, dateStr, &dateW, &dateH);
            renderText(renderer, fontJet, dateStr, 1280 - dateW, 4, W);

            // Caja principal
            lineRGBA(renderer, 0, 28, 1280, 28, tt.accent.r, tt.accent.g, tt.accent.b, 255);
            lineRGBA(renderer, 0, 690, 1280, 690, tt.accent.r, tt.accent.g, tt.accent.b, 255);
            lineRGBA(renderer, 2, 28, 2, 690, tt.accent.r, tt.accent.g, tt.accent.b, 255);
            lineRGBA(renderer, 1277, 28, 1277, 690, tt.accent.r, tt.accent.g, tt.accent.b, 255);

            // Título centrado
            const char* title = screenTitle(currentScreen);

            int tW = 0, tH = 0;
            TTF_SizeUTF8(fontJet, title, &tW, &tH);
            renderText(renderer, fontJet, title, 640 - (tW / 2), 34, W);
            lineRGBA(renderer, 0, 60, 1280, 60, tt.accent.r, tt.accent.g, tt.accent.b, 255);

            // Footer
            char sdBuf[128];
            snprintf(sdBuf, sizeof(sdBuf), "-SD: %s/%s-", formatSize(sdFree).c_str(), formatSize(sdTotal).c_str());
            renderText(renderer, fontJet, sdBuf, 0, 694, W);
            char batBuf[32];
            snprintf(batBuf, sizeof(batBuf), "[ %u%% ]-", cachedBatt);
            int bW = 0, bH = 0;
            TTF_SizeUTF8(fontJet, batBuf, &bW, &bH);
            renderText(renderer, fontJet, batBuf, 1280 - bW, 694, Green);

            // ── LISTAS Y CONTENIDO ───────────────────────────────────────────────────
            int dbiY = 66;
            int rowH = 30;  // Aumentado para más espacio entre filas

            if (currentScreen == SCREEN_DASHBOARD || currentScreen == SCREEN_SYS) {
                for (int i = 0; i < 7; i++) {
                    int rY = dbiY + i * rowH;
                    if (menuIdx == i) {
                        boxRGBA(renderer, 4, rY, 1275, rY + rowH, B.r, B.g, B.b, B.a);
                    }
                    renderText(renderer, fontJet, defs[i].title, 12, rY + 4, W);
                }
            }
            else if (currentScreen == SCREEN_EXPLORER) {
                if (uiMode == UI_RANGER) {
                    // --- RANGER MODE (Miller Columns) ---
                    // Cabecera de ruta
                    renderText(renderer, fontJet, currentPath.c_str(), 12, dbiY + 4, Green);
                    lineRGBA(renderer, 0, dbiY + rowH + 4, 1280, dbiY + rowH + 4, tt.accent.r, tt.accent.g, tt.accent.b, 100);
                    
                    int listY = dbiY + rowH + 8;
                    int col1W = 250; // Parent
                    int col2W = 400; // Current
                    int col3X = col1W + col2W + 20; // Preview
                    
                    // Separadores
                    lineRGBA(renderer, col1W, listY, col1W, 690, tt.accent.r, tt.accent.g, tt.accent.b, 100);
                    lineRGBA(renderer, col1W + col2W, listY, col1W + col2W, 690, tt.accent.r, tt.accent.g, tt.accent.b, 100);
                    
                    // Col 1: Parent Dir (si no estamos en root)
                    if (currentPath != "sdmc:/") {
                        size_t lastSlash = currentPath.find_last_of('/');
                        if (lastSlash != std::string::npos) {
                            std::string parentPath = currentPath.substr(0, lastSlash);
                            if (parentPath.empty() || parentPath == "sdmc:") parentPath = "sdmc:/";
                            std::vector<FileItem> parentFiles = getFolderItems(parentPath);
                            for (size_t i = 0; i < parentFiles.size() && i < 20; i++) {
                                int rY = listY + i * rowH;
                                std::string n = parentFiles[i].name;
                                if (n.length() > 20) n = n.substr(0, 17) + "...";
                                renderText(renderer, fontJet, n.c_str(), 12, rY + 4, tt.accent); // Dimmer
                            }
                        }
                    }
                    
                    // Col 2: Current Dir
                    int totalF = (int)files.size();
                    int maxVis = 20;
                    for (int i = 0; i < maxVis && (scrollOffset + i) < totalF; i++) {
                        int fi = scrollOffset + i;
                        int rY = listY + i * rowH;
                        bool sel = (fileIdx == fi);
                        const FileItem& f = files[fi];
                        
                        if (sel) {
                            boxRGBA(renderer, col1W + 4, rY, col1W + col2W - 4, rY + rowH, B.r, B.g, B.b, B.a);
                        }
                        
                        std::string n = (f.isDir ? "/" : "") + f.name;
                        if (n.length() > 30) n = n.substr(0, 27) + "...";
                        renderText(renderer, fontJet, n.c_str(), col1W + 12, rY + 4, sel ? W : W); // Mantenemos W o podemos usar color distinto
                    }
                    
                    // Col 3: Preview
                    if (!files.empty() && fileIdx < totalF) {
                        const FileItem& cur = files[fileIdx];
                        renderText(renderer, fontJet, cur.name.c_str(), col3X, listY + 4, Green);
                        lineRGBA(renderer, col3X, listY + 30, 1280, listY + 30, tt.accent.r, tt.accent.g, tt.accent.b, 100);
                        
                        if (cur.isDir) {
                            std::string targetPath = currentPath + (currentPath.back() == '/' ? "" : "/") + cur.name;
                            std::vector<FileItem> prevFiles = getFolderItems(targetPath);
                            for (size_t i = 0; i < prevFiles.size() && i < 18; i++) {
                                int rY = listY + 36 + i * rowH;
                                std::string n = (prevFiles[i].isDir ? "/" : "") + prevFiles[i].name;
                                if (n.length() > 40) n = n.substr(0, 37) + "...";
                                renderText(renderer, fontJet, n.c_str(), col3X, rY + 4, W);
                            }
                        } else {
                            // Info basica de archivo
                            char info[128];
                            snprintf(info, sizeof(info), "Size: %s\nExt: %s", formatSize(cur.size).c_str(), cur.ext.c_str());
                            renderText(renderer, fontJet, info, col3X, listY + 44, W);
                            // TODO: Add actual text/image preview logic here
                            renderText(renderer, fontJet, "[Preview not available]", col3X, listY + 100, tt.accent);
                        }
                    }
                } else {
                    // --- DBI MODE ---
                    renderText(renderer, fontJet, currentPath.c_str(), 12, dbiY + 4, W);
                    lineRGBA(renderer, 0, dbiY + rowH + 4, 1280, dbiY + rowH + 4, tt.accent.r, tt.accent.g, tt.accent.b, 255);
                    
                    int listY = dbiY + rowH + 8;
                    int totalF = (int)files.size();
                    int maxVis = 20;
                    for (int i = 0; i < maxVis && (scrollOffset + i) < totalF; i++) {
                        int fi = scrollOffset + i;
                        int rY = listY + i * rowH;
                        bool sel = (fileIdx == fi);
                        const FileItem& f = files[fi];
                        
                        if (sel) {
                            boxRGBA(renderer, 4, rY, 1275, rY + rowH, B.r, B.g, B.b, B.a);
                        }
                        
                        char line[256];
                        if (f.isDir) snprintf(line, sizeof(line), "<DIR>     %s", f.name.c_str());
                        else snprintf(line, sizeof(line), "%-9s %s", formatSize(f.size).c_str(), f.name.c_str());
                        
                        renderText(renderer, fontJet, line, 12, rY + 4, W);
                    }
                }

                // Modales en DBI mode (con colores del tema terminal, no fijos)
                if (showContextModal) {
                    boxRGBA(renderer, 340, 160, 940, 460, tt.bg.r, tt.bg.g, tt.bg.b, 255);
                    rectangleRGBA(renderer, 340, 160, 940, 460, W.r, W.g, W.b, 255);
                    const char* ctxOpts[6] = {
                        tr().ctx_copy, tr().ctx_cut, tr().ctx_paste,
                        tr().ctx_rename, tr().ctx_delete, tr().ctx_props
                    };
                    for (int i = 0; i < 6; i++) {
                        int rY = 170 + i * rowH;
                        if (contextMenuIdx == i) boxRGBA(renderer, 342, rY, 938, rY + rowH, B.r, B.g, B.b, B.a);
                        renderText(renderer, fontJet, ctxOpts[i], 350, rY + 4, W);
                    }
                }
            }
            else if (currentScreen == SCREEN_MTP) {
                std::string mtpSt = mtp_ops::running() ? "MTP Server is RUNNING (0x057E:0x201D)"
                    : (mtp_usb::g_ready ? "MTP USB Ready - Press [A] to Start" : "MTP Server is STOPPED");
                renderText(renderer, fontJet, mtpSt.c_str(), 12, dbiY + rowH*1 + 4, mtp_ops::running() ? Green : W);
                renderText(renderer, fontJet, mtp_ops::running() ? "Press [A] to Stop MTP" : "Press [A] to Start MTP", 12, dbiY + rowH*2 + 4, W);
                renderText(renderer, fontJet, "Press [Y] to Clear Bitacora", 12, dbiY + rowH*3 + 4, W);
                renderText(renderer, fontJet, "Press [B] to Exit", 12, dbiY + rowH*4 + 4, W);
            }
            else if (currentScreen == SCREEN_FTP) {
                std::string ftpSt = ftpRunning ? (std::string("FTP Server is RUNNING at ") + ftp_server::getRealIp() + ":5000") : "FTP Server is STOPPED";
                renderText(renderer, fontJet, ftpSt.c_str(), 12, dbiY + rowH*1 + 4, W);
                renderText(renderer, fontJet, "Press [A] to Toggle", 12, dbiY + rowH*2 + 4, W);
                renderText(renderer, fontJet, "Press [B] to Exit", 12, dbiY + rowH*3 + 4, W);
            }
            else if (currentScreen == SCREEN_LANG) {
                int lVisible = 20;
                int lOff = (langSelectIdx > lVisible/2) ? langSelectIdx - lVisible/2 : 0;
                for (int i = 0; i < LANG_COUNT && (i - lOff) < lVisible; i++) {
                    if (i < lOff) continue;
                    int rY = dbiY + (i - lOff) * rowH;
                    if (langSelectIdx == i) boxRGBA(renderer, 4, rY, 1275, rY + rowH, B.r, B.g, B.b, B.a);
                    char line[64];
                    snprintf(line, sizeof(line), "%s  %s", TRANSLATIONS[i].badge, TRANSLATIONS[i].lang_name);
                    renderText(renderer, fontJet, line, 12, rY + 4, W);
                }
            }
            else if (currentScreen == SCREEN_THEME) {
                renderText(renderer, fontJet, "Terminal Themes ([D-PAD] elegir, [A] aplicar, [Y] rapido)", 12, dbiY + 4, Green);
                lineRGBA(renderer, 0, dbiY + rowH, 1280, dbiY + rowH, tt.accent.r, tt.accent.g, tt.accent.b, 100);

                for (int ti = 0; ti < getTermThemeCount(); ti++) {
                    int rY = dbiY + rowH + 8 + ti * rowH;
                    bool hl = (termSelectIdx == ti);
                    bool active = (currentTermThemeIdx == ti);
                    if (hl) boxRGBA(renderer, 4, rY, 900, rY + rowH, B.r, B.g, B.b, B.a);
                    renderText(renderer, fontJet, TERM_THEMES[ti].name, 12, rY + 4, hl ? W : W);
                    if (active) {
                        renderText(renderer, fontJet, "[ACTIVO]", 700, rY + 4, Green);
                    }
                }
                // Muestra de colores del tema resaltado
                const TerminalTheme& tp = TERM_THEMES[termSelectIdx];
                boxRGBA(renderer, 950, dbiY + rowH + 8, 1270, dbiY + rowH + 8 + 3 * rowH, tp.bg.r, tp.bg.g, tp.bg.b, 255);
                rectangleRGBA(renderer, 950, dbiY + rowH + 8, 1270, dbiY + rowH + 8 + 3 * rowH, tp.accent.r, tp.accent.g, tp.accent.b, 255);
                renderText(renderer, fontJet, "AaBbCc", 970, dbiY + rowH + 16, tp.fg);
                boxRGBA(renderer, 970, dbiY + rowH + 48, 1250, dbiY + rowH + 76, tp.selectBg.r, tp.selectBg.g, tp.selectBg.b, 255);
                renderText(renderer, fontJet, "Sel", 978, dbiY + rowH + 52, tp.fg);
            }
            else if (currentScreen == SCREEN_ABOUT) {
                renderText(renderer, fontJet, "EZ FILES v1.3.0", 12, dbiY + rowH*0, Green);
                lineRGBA(renderer, 0, dbiY + rowH - 4, 1280, dbiY + rowH - 4, tt.accent.r, tt.accent.g, tt.accent.b, 80);
                struct { const char* lbl; const char* val; } fields[] = {
                    { tr().about_author_lbl,    tr().about_author },
                    { tr().about_email_lbl,     tr().about_email  },
                    { tr().about_github_lbl,    tr().about_github },
                    { tr().about_web_lbl,       "github.com/lozp1/EzFiles" },
                    { tr().about_suite_lbl,     "EzFiles Andromeda Edition v1.3.0" },
                    { tr().about_portfolio_lbl, "github.com/lozp1" },
                    { tr().about_engine_lbl,    "Horizon OS Native (devkitA64)" },
                    { tr().about_system_lbl,    tr().about_system },
                };
                for (int fi = 0; fi < 8; fi++) {
                    int rY = dbiY + rowH + fi * rowH;
                    char line[256];
                    snprintf(line, sizeof(line), "%-22s %s", fields[fi].lbl, fields[fi].val);
                    renderText(renderer, fontJet, line, 12, rY + 4, fi % 2 == 0 ? W : tt.accent);
                }
            }

            if (toastTimer > 0 && !toastMessage.empty()) {
                boxRGBA(renderer, 0, 650, 1280, 690, tt.bg.r, tt.bg.g, tt.bg.b, 255);
                lineRGBA(renderer, 0, 650, 1280, 650, tt.accent.r, tt.accent.g, tt.accent.b, 255);
                renderText(renderer, fontJet, toastMessage.c_str(), 12, 660, W);
            }

            SDL_RenderPresent(renderer);
            wasTouch = isTouch;
            continue; // Saltar el render gráfico moderno
        }
        // =========================================================================
        // RENDERIZADO VISUAL CON ESTILOS ANDROMEDA (solo si uiMode == UI_MODERN)
        // =========================================================================
        if (uiMode != UI_MODERN) {
            // Modos Terminal y Ranger ya hicieron continue arriba, esto no debería ejecutarse
            SDL_RenderPresent(renderer);
            wasTouch = isTouch;
            continue;
        }
        SDL_SetRenderDrawColor(renderer, tc.BgBase.r, tc.BgBase.g, tc.BgBase.b, 255);
        SDL_RenderClear(renderer);

        // 1. RENDER SPLASH SCREEN
        switch (currentScreen) {
        case SCREEN_SPLASH: {
            float progress = (float)splashFrames / (float)maxSplashFrames;
            float pulse = 0.5f + 0.5f * sinf(splashFrames * 0.12f);
            float floatOffsetY = sinf(splashFrames * 0.08f) * 6.0f;

            filledCircleRGBA(renderer, 640, 260 + (int)floatOffsetY, 180, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, (Uint8)(20 * pulse));

            int cardW = 180;
            int cardH = 180;
            int cardX = 640 - (cardW / 2);
            int cardY = 150 + (int)floatOffsetY;

            drawCleanLogo(renderer, ezfilesLogoTex, cardX, cardY, cardW);

            renderTextCentered(renderer, fontLogo, "EZ FILES", 640, 395, tc.TextPrimary);
            renderTextCentered(renderer, fontBody, "HIGH-SPEED STORAGE & FILE SUITE", 640, 435, tc.AccentCyan);

            int barW = 440;
            int barH = 8;
            int barX = 640 - (barW / 2);
            int barY = 495;
            boxRGBA(renderer, barX, barY, barX + barW, barY + barH, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 255);
            int fillW = (int)(barW * progress);
            if (fillW > 4) {
                boxRGBA(renderer, barX, barY, barX + fillW, barY + barH, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);
            }

            renderTextCentered(renderer, fontSmall, "Iniciando sistema Andromeda Zero-Copy...", 640, 525, tc.TextMuted);
            renderTextCentered(renderer, fontSmall, "Presiona [A] para continuar", 640, 645, tc.TextSecondary);
        }
        // 2. RENDER DASHBOARD PRINCIPAL
        break;
        case SCREEN_DASHBOARD: {
            // Header superior elegante
            boxRGBA(renderer, 0, 0, 1280, 80, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 80, 1280, 80, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

            drawCleanLogo(renderer, ezfilesLogoTex, 36, 16, 48);

            // ORBES AMBIENTALES FLOTANTES (Andromeda orbFloat)
            float orbPhase = animFrame * 0.008f;
            int orb1X = 1180 + (int)(sinf(orbPhase) * 14.0f);
            int orb1Y = 120 + (int)(cosf(orbPhase * 0.7f) * 10.0f);
            int orb2X = 60 + (int)(sinf(orbPhase + 3.14f) * 12.0f);
            int orb2Y = 580 + (int)(cosf(orbPhase * 0.5f) * 8.0f);
            for (int oi = 0; oi < 5; oi++) {
                int or1 = 80 - oi * 15;
                Uint8 oa1 = (Uint8)(14 - oi * 2);
                filledCircleRGBA(renderer, orb1X, orb1Y, or1, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, oa1);
            }
            for (int oi = 0; oi < 5; oi++) {
                int or2 = 70 - oi * 13;
                Uint8 oa2 = (Uint8)(10 - oi * 2);
                filledCircleRGBA(renderer, orb2X, orb2Y, or2, tc.AccentEmerald.r, tc.AccentEmerald.g, tc.AccentEmerald.b, oa2);
            }

            renderText(renderer, fontTitle, tr().app_name, 100, 18, tc.TextPrimary);
            renderText(renderer, fontSmall, tr().status_online, 100, 46, tc.AccentEmerald);

            // BATERÍA, HORA y FECHA cacheadas a 1Hz
            // Indicador de Batería y Reloj en el Header
            char battBuf[32];
            snprintf(battBuf, sizeof(battBuf), "%u%%", cachedBatt);
            renderText(renderer, fontSmall, battBuf, 780, 31, tc.AccentEmerald);
            renderText(renderer, fontSmall, cachedTime, 860, 31, tc.TextPrimary);
            renderText(renderer, fontSmall, cachedDate, 940, 31, tc.TextSecondary);

            // Badge de Tema actual (se cambia en Temas del Sistema)
            std::string themePill = curTheme().name;
            boxRGBA(renderer, 1060, 22, 1140, 58, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 255);
            rectangleRGBA(renderer, 1060, 22, 1140, 58, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderTextCentered(renderer, fontSmall, themePill, 1100, 31, tc.AccentAmber);

            // Badge de Idioma actual
            std::string langPill = std::string(tr().badge) + " " + tr().lang_name;
            boxRGBA(renderer, 1150, 22, 1245, 58, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 255);
            rectangleRGBA(renderer, 1150, 22, 1245, 58, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderTextCentered(renderer, fontSmall, langPill, 1197, 31, tc.AccentCyan);

            renderText(renderer, fontTitle, tr().dash_title, 80, 108, tc.TextPrimary);
            renderText(renderer, fontSmall, tr().dash_sub, 80, 140, tc.TextSecondary);

            int cardStartX = 80;
            int cardW = 1120;
            int cardH = 60;
            int cardSpacing = 8;
            int cardStartY = 175;

            // defs[] ya viene relleno arriba (fuente única para los 3 modos).

            for (int i = 0; i < 7; i++) {
                // Slide-in animation on screen entry
                int slideOff = 0;
                if (screenEnterFrame < 22) {
                    float st = (float)(screenEnterFrame - i * 2) / 18.0f;
                    if (st < 0.0f) st = 0.0f;
                    if (st > 1.0f) st = 1.0f;
                    float ease = st * (2.0f - st);
                    slideOff = (int)((1.0f - ease) * 55);
                }
                int cY = cardStartY + i * (cardH + cardSpacing) + slideOff;
                if (cY + cardH > 665) break; // No dibujar fuera del área (footer en 665)
                bool isSelected = (menuIdx == i);

                SDL_Color cardBg = isSelected ? tc.BgCard : tc.BgSurface;
                boxRGBA(renderer, cardStartX, cY, cardStartX + cardW, cY + cardH, cardBg.r, cardBg.g, cardBg.b, 255);

                if (isSelected) {
                    // Solo borde azul en la seleccion
                    rectangleRGBA(renderer, cardStartX, cY, cardStartX + cardW, cY + cardH, 56, 139, 253, 255);   // Azul GitHub
                    rectangleRGBA(renderer, cardStartX+1, cY+1, cardStartX + cardW-1, cY + cardH-1, 56, 139, 253, 140);
                } else {
                    rectangleRGBA(renderer, cardStartX, cY, cardStartX + cardW, cY + cardH, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 180);
                }

                if (defs[i].icon) {
                    SDL_Rect iDst = { cardStartX + 28, cY + 10, 40, 40 };
                    SDL_RenderCopy(renderer, defs[i].icon, NULL, &iDst);
                }

                SDL_Color titleCol = isSelected ? tc.TextPrimary : tc.TextSecondary;
                renderText(renderer, fontTitle, defs[i].title, cardStartX + 85, cY + 6, titleCol);
                renderText(renderer, fontSmall, defs[i].sub, cardStartX + 85, cY + 34, tc.TextMuted);

            }

            lineRGBA(renderer, 0, 665, 1280, 665, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            // Indicador del modo actual
            const char* modeStr = (uiMode == UI_MODERN) ? "[L] Modo: Visual" : (uiMode == UI_DBI) ? "[L] Modo: Terminal" : "[L] Modo: Ranger";
            renderText(renderer, fontSmall, modeStr, 80, 680, tc.AccentCyan);
        }
        // 3. RENDER EXPLORADOR DE ARCHIVOS
        break;
        case SCREEN_EXPLORER: {
            boxRGBA(renderer, 0, 0, 1280, 75, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 75, 1280, 75, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

            renderText(renderer, fontBody, currentPath, 40, 16, tc.TextPrimary);

            char countBuf[64];
            snprintf(countBuf, sizeof(countBuf), "%d %s", (int)files.size(), tr().exp_items);
            renderText(renderer, fontSmall, countBuf, 40, 44, tc.TextSecondary);

            if (clipboardOp != CLIP_NONE && !clipboardItemName.empty()) {
                std::string clipStr = std::string("[Y] PASTE: ") + clipboardItemName;
                boxRGBA(renderer, 680, 16, 1240, 56, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 255);
                rectangleRGBA(renderer, 680, 16, 1240, 56, tc.AccentAmber.r, tc.AccentAmber.g, tc.AccentAmber.b, 255);
                renderText(renderer, fontSmall, clipStr, 700, 26, tc.AccentAmber);
            } else {
                renderText(renderer, fontSmall, "[+] Opciones  [-] Propiedades", 840, 26, tc.TextMuted);
            }

            int listStartY = 90;
            int itemH = 56;
            int total = (int)files.size();

            if (total == 0) {
                renderTextCentered(renderer, fontBody, tr().exp_empty, 640, 300, tc.TextPrimary);
            } else {
                for (int i = 0; i < maxVisibleFiles; i++) {
                    int idx = scrollOffset + i;
                    if (idx >= total) break;

                    const FileItem& item = files[idx];
                    int itemY = listStartY + i * (itemH + 6);
                    bool isSelected = (idx == fileIdx);

                    SDL_Color rowBg = isSelected ? tc.BgCard : tc.BgSurface;
                    boxRGBA(renderer, 40, itemY, 1240, itemY + itemH, rowBg.r, rowBg.g, rowBg.b, 255);

                    if (isSelected) {
                        rectangleRGBA(renderer, 40, itemY, 1240, itemY + itemH, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);
                        boxRGBA(renderer, 42, itemY + 8, 46, itemY + itemH - 8, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);
                    } else {
                        rectangleRGBA(renderer, 40, itemY, 1240, itemY + itemH, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 160);
                    }

                    SDL_Texture* curIco = icoFileSm;
                    if (item.isDir) curIco = icoFolderSm;
                    else if (item.ext == ".nsp" || item.ext == ".xci") curIco = icoNspSm;
                    else if (item.ext == ".nro") curIco = icoNroSm;
                    else if (item.ext == ".sav") curIco = icoSavSm;

                    if (curIco) {
                        SDL_Rect cDst = { 60, itemY + 10, 36, 36 };
                        SDL_RenderCopy(renderer, curIco, NULL, &cDst);
                    }

                    // REGISTROS EN FUENTE BLANCA PURA
                    std::string displayName = item.name;
                    if (displayName.length() > 50) displayName = displayName.substr(0, 47) + "...";
                    renderText(renderer, fontBody, displayName, 115, itemY + 10, tc.TextPrimary);

                    std::string infoStr = item.isDir ? "<CARPETA>" : formatSize(item.size);
                    infoStr += "   " + formatTime(item.modTime);
                    renderText(renderer, fontSmall, infoStr, 115, itemY + 32, tc.TextSecondary);
                }
            }

            boxRGBA(renderer, 0, 665, 1280, 720, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 665, 1280, 665, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderText(renderer, fontSmall, "[A] Abrir · [B] Volver · [+] Opciones · [-] Propiedades · [Touch] Deslizar", 40, 680, tc.TextSecondary);

            // Modal Contextual
            if (showContextModal) {
                boxRGBA(renderer, 0, 0, 1280, 720, 0, 0, 0, 180);
                int mW = 480, mH = 370;
                int mX = 640 - (mW / 2), mY = 360 - (mH / 2);

                boxRGBA(renderer, mX, mY, mX + mW, mY + mH, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
                rectangleRGBA(renderer, mX, mY, mX + mW, mY + mH, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);

                renderTextCentered(renderer, fontTitle, tr().ctx_title, 640, mY + 18, tc.TextPrimary);
                lineRGBA(renderer, mX + 20, mY + 50, mX + mW - 20, mY + 50, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

                const char* ctxLabels[6] = {
                    tr().ctx_copy,
                    tr().ctx_cut,
                    tr().ctx_paste,
                    tr().ctx_rename,
                    tr().ctx_delete,
                    tr().ctx_props
                };

                for (int i = 0; i < 6; i++) {
                    int optY = mY + 60 + i * 46;
                    bool isOptSel = (contextMenuIdx == i);

                    if (isOptSel) {
                        boxRGBA(renderer, mX + 20, optY, mX + mW - 20, optY + 38, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 255);
                        rectangleRGBA(renderer, mX + 20, optY, mX + mW - 20, optY + 38, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);
                    }

                    SDL_Color optCol = (i == 4) ? tc.AccentRed : (isOptSel ? tc.TextPrimary : tc.TextSecondary);
                    renderText(renderer, fontBody, ctxLabels[i], mX + 40, optY + 8, optCol);
                }
            }

            if (showDeleteConfirmModal) {
                boxRGBA(renderer, 0, 0, 1280, 720, 0, 0, 0, 200);
                int mW = 540, mH = 260;
                int mX = 640 - (mW / 2), mY = 360 - (mH / 2);

                boxRGBA(renderer, mX, mY, mX + mW, mY + mH, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
                rectangleRGBA(renderer, mX, mY, mX + mW, mY + mH, tc.AccentRed.r, tc.AccentRed.g, tc.AccentRed.b, 255);

                renderTextCentered(renderer, fontTitle, tr().del_title, 640, mY + 22, tc.AccentRed);
                lineRGBA(renderer, mX + 20, mY + 58, mX + mW - 20, mY + 58, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

                renderTextCentered(renderer, fontBody, tr().del_msg, 640, mY + 75, tc.TextPrimary);

                if (!files.empty() && fileIdx < (int)files.size()) {
                    renderTextCentered(renderer, fontBody, files[fileIdx].name, 640, mY + 110, tc.AccentAmber);
                }

                boxRGBA(renderer, mX + 40, mY + 180, mX + 240, mY + 225, tc.AccentRed.r, tc.AccentRed.g, tc.AccentRed.b, 255);
                renderTextCentered(renderer, fontSmall, tr().del_confirm, mX + 140, mY + 195, tc.TextPrimary);

                boxRGBA(renderer, mX + 300, mY + 180, mX + 500, mY + 225, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 255);
                rectangleRGBA(renderer, mX + 300, mY + 180, mX + 500, mY + 225, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
                renderTextCentered(renderer, fontSmall, tr().del_cancel, mX + 400, mY + 195, tc.TextSecondary);
            }

            if (showPropertiesModal) {
                boxRGBA(renderer, 0, 0, 1280, 720, 0, 0, 0, 200);
                int mW = 620, mH = 380;
                int mX = 640 - (mW / 2), mY = 360 - (mH / 2);

                boxRGBA(renderer, mX, mY, mX + mW, mY + mH, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
                rectangleRGBA(renderer, mX, mY, mX + mW, mY + mH, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);

                renderTextCentered(renderer, fontTitle, tr().prop_title, 640, mY + 22, tc.TextPrimary);
                lineRGBA(renderer, mX + 24, mY + 58, mX + mW - 24, mY + 58, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

                if (!files.empty() && fileIdx < (int)files.size()) {
                    const FileItem& cur = files[fileIdx];
                    std::string full = currentPath + (currentPath.back() == '/' ? "" : "/") + cur.name;

                    int propY = mY + 80;
                    renderText(renderer, fontSmall, tr().prop_name, mX + 40, propY, tc.TextMuted);
                    renderText(renderer, fontBody, cur.name, mX + 180, propY - 2, tc.TextPrimary);

                    propY += 42;
                    renderText(renderer, fontSmall, tr().prop_path, mX + 40, propY, tc.TextMuted);
                    renderText(renderer, fontBody, full.length() > 42 ? full.substr(0, 39) + "..." : full, mX + 180, propY - 2, tc.TextSecondary);

                    propY += 42;
                    renderText(renderer, fontSmall, tr().prop_type, mX + 40, propY, tc.TextMuted);
                    renderText(renderer, fontBody, cur.isDir ? tr().prop_type_folder : tr().prop_type_file, mX + 180, propY - 2, tc.TextPrimary);

                    propY += 42;
                    renderText(renderer, fontSmall, tr().prop_size, mX + 40, propY, tc.TextMuted);
                    char szBuf[64];
                    snprintf(szBuf, sizeof(szBuf), "%s (%llu bytes)", formatSize(cur.size).c_str(), (unsigned long long)cur.size);
                    renderText(renderer, fontBody, cur.isDir ? "--" : szBuf, mX + 180, propY - 2, tc.AccentEmerald);

                    propY += 42;
                    renderText(renderer, fontSmall, tr().prop_date, mX + 40, propY, tc.TextMuted);
                    renderText(renderer, fontBody, formatTime(cur.modTime), mX + 180, propY - 2, tc.TextSecondary);
                }

                lineRGBA(renderer, mX + 24, mY + 315, mX + mW - 24, mY + 315, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
                renderTextCentered(renderer, fontSmall, "Presiona [B] o [A] para cerrar", 640, mY + 335, tc.AccentCyan);
            }
        }
        // 4. RENDER CONEXIÓN USB (MTP) - PLACEHOLDER + consola estilo DBI
        break;
        case SCREEN_MTP: {
            boxRGBA(renderer, 0, 0, 1280, 80, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 80, 1280, 80, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

            renderText(renderer, fontTitle, tr().mtp_title, 80, 26, tc.TextPrimary);

            // Botones de acción en el encabezado
            const char* mtpBtn = mtp_ops::running() ? "[A] Detener MTP" : "[A] Activar MTP";
            SDL_Color mtpBtnCol = mtp_ops::running() ? tc.AccentAmber : tc.AccentEmerald;
            renderText(renderer, fontBody, mtpBtn, 820, 28, mtpBtnCol);
            renderText(renderer, fontSmall, "[Y] Limpiar Log", 1020, 31, tc.TextMuted);

            // Tarjeta de estado compacta
            int cX = 140, cY = 95, cW = 1000, cH = 110;
            boxRGBA(renderer, cX, cY, cX + cW, cY + cH, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            rectangleRGBA(renderer, cX, cY, cX + cW, cY + cH,
                          mtp_ops::running() ? tc.AccentEmerald.r : tc.BorderSubtle.r,
                          mtp_ops::running() ? tc.AccentEmerald.g : tc.BorderSubtle.g,
                          mtp_ops::running() ? tc.AccentEmerald.b : tc.BorderSubtle.b, 255);

            if (icoUsb) {
                SDL_Rect uDst = { cX + 20, cY + 20, 48, 48 };
                SDL_RenderCopy(renderer, icoUsb, NULL, &uDst);
            }

            renderText(renderer, fontTitle, mtp_ops::running() ? "ESTADO: SERVIDOR ACTIVO (0x057E:0x201D)" : "ESTADO: EN ESPERA",
                       cX + 80, cY + 16, mtp_ops::running() ? tc.AccentEmerald : tc.TextSecondary);

            renderText(renderer, fontBody,
                       mtp_ops::running() ? "Conecta el cable USB al PC para explorar la tarjeta SD."
                                          : "Presiona [A] para activar el respondedor MTP e iniciar el enlace USB.",
                       cX + 80, cY + 48, tc.TextPrimary);

            renderText(renderer, fontSmall,
                       "Compatible con Explorador de Windows, macOS (Android File Transfer) y Linux.",
                       cX + 80, cY + 76, tc.TextMuted);

            // Consola de bitácora expandida (alto 435px, 17 líneas visibles)
            drawLogConsole(renderer, fontSmall, 140, 220, 1000, 435, tc, 17, "BITÁCORA USB MTP");

            lineRGBA(renderer, 0, 665, 1280, 665, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderText(renderer, fontSmall, tr().hint_back, 80, 680, tc.TextMuted);
        }
        // 5. RENDER SERVIDOR FTP + consola estilo DBI
        break;
        case SCREEN_FTP: {
            boxRGBA(renderer, 0, 0, 1280, 80, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 80, 1280, 80, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

            renderText(renderer, fontTitle, tr().ftp_title, 80, 26, tc.TextPrimary);

            int cX = 140, cY = 95, cW = 1000, cH = 320;
            boxRGBA(renderer, cX, cY, cX + cW, cY + cH, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            rectangleRGBA(renderer, cX, cY, cX + cW, cY + cH, ftpRunning ? tc.AccentViolet.r : tc.BorderSubtle.r,
                          ftpRunning ? tc.AccentViolet.g : tc.BorderSubtle.g,
                          ftpRunning ? tc.AccentViolet.b : tc.BorderSubtle.b, 255);

            if (icoFtp) {
                SDL_Rect fDst = { 640 - 20, cY + 10, 40, 40 };
                SDL_RenderCopy(renderer, icoFtp, NULL, &fDst);
            }

            std::string realIp = ftp_server::getRealIp();
            bool hasWifi = (realIp != "0.0.0.0");

            renderTextCentered(renderer, fontTitle, ftpRunning ? tr().ftp_active : tr().ftp_standby, 640, cY + 56,
                               ftpRunning ? tc.AccentViolet : tc.TextSecondary);

            if (hasWifi) {
                std::string ftpUrl = "ftp://" + realIp + ":5000";
                boxRGBA(renderer, 380, cY + 88, 900, cY + 130, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 255);
                rectangleRGBA(renderer, 380, cY + 88, 900, cY + 130, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 200);
                renderTextCentered(renderer, fontTitle, ftpUrl, 640, cY + 98, tc.AccentCyan);

                // Credenciales reales: nickname de la consola (clave = usuario)
                renderTextCentered(renderer, fontBody, "User: " + ftp_server::g_ftpUser + "  |  Password: (igual al usuario)", 640, cY + 142, tc.AccentEmerald);
                renderTextCentered(renderer, fontSmall, "Enter this address in FileZilla or Windows Explorer.", 640, cY + 168, tc.TextPrimary);
            } else {
                renderTextCentered(renderer, fontBody, "RED NO CONECTADA", 640, cY + 100, tc.AccentAmber);
                renderTextCentered(renderer, fontSmall, "Conecta tu Switch al WiFi local para activar el servidor.", 640, cY + 130, tc.TextMuted);
            }

            boxRGBA(renderer, 400, cY + 232, 880, cY + 272, ftpRunning ? tc.AccentViolet.r : tc.BgCard.r,
                    ftpRunning ? tc.AccentViolet.g : tc.BgCard.g,
                    ftpRunning ? tc.AccentViolet.b : tc.BgCard.b, 255);
            rectangleRGBA(renderer, 400, cY + 232, 880, cY + 272, tc.AccentViolet.r, tc.AccentViolet.g, tc.AccentViolet.b, 255);
            renderTextCentered(renderer, fontBody, ftpRunning ? "[A] Detener FTP   [Y] Limpiar log" : "[A] Iniciar FTP   [Y] Limpiar log", 640, cY + 242,
                               ftpRunning ? tc.AccentRed : tc.AccentEmerald);

            drawLogConsole(renderer, fontSmall, 140, 425, 1000, 230, tc, 9, "CONSOLA FTP");

            lineRGBA(renderer, 0, 665, 1280, 665, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderText(renderer, fontSmall, tr().hint_back, 80, 680, tc.TextMuted);
        }
        // 6. RENDER PANTALLA DE IDIOMAS (11 IDIOMAS)
        break;
        case SCREEN_LANG: {
            boxRGBA(renderer, 0, 0, 1280, 80, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 80, 1280, 80, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

            renderText(renderer, fontTitle, tr().menu_lang, 80, 26, tc.TextPrimary);

            renderTextCentered(renderer, fontTitle, tr().menu_lang, 640, 105, tc.TextPrimary);
            renderTextCentered(renderer, fontSmall, tr().menu_lang_sub, 640, 132, tc.TextMuted);

            int startY = 160;
            int rowH = 48;
            int rowW = 680;
            int rX = 640 - (rowW / 2);
            int visibleLangs = 9;
            int langScroll = std::clamp(langSelectIdx - 4, 0, std::max(0, LANG_COUNT - visibleLangs));

            for (int i = 0; i < visibleLangs; i++) {
                int lIdx = langScroll + i;
                if (lIdx >= LANG_COUNT) break;

                int rY = startY + i * (rowH + 6);
                bool isHighlighted = (langSelectIdx == lIdx);
                bool isActiveLang = ((int)currentLanguage == lIdx);

                SDL_Color rowBg = isHighlighted ? tc.BgCard : tc.BgSurface;
                boxRGBA(renderer, rX, rY, rX + rowW, rY + rowH, rowBg.r, rowBg.g, rowBg.b, 255);

                if (isHighlighted) {
                    rectangleRGBA(renderer, rX, rY, rX + rowW, rY + rowH, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);
                    boxRGBA(renderer, rX + 2, rY + 6, rX + 6, rY + rowH - 6, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);
                } else {
                    rectangleRGBA(renderer, rX, rY, rX + rowW, rY + rowH, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 180);
                }

                std::string langLabel = std::string(TRANSLATIONS[lIdx].badge) + "  " + TRANSLATIONS[lIdx].lang_name;
                renderText(renderer, fontBody, langLabel, rX + 28, rY + 12, isHighlighted ? tc.TextPrimary : tc.TextSecondary);

                if (isActiveLang) {
                    boxRGBA(renderer, rX + rowW - 120, rY + 10, rX + rowW - 20, rY + rowH - 10, tc.BgBase.r, tc.BgBase.g, tc.BgBase.b, 255);
                    rectangleRGBA(renderer, rX + rowW - 120, rY + 10, rX + rowW - 20, rY + rowH - 10, tc.AccentEmerald.r, tc.AccentEmerald.g, tc.AccentEmerald.b, 255);
                    renderTextCentered(renderer, fontSmall, "● ACTIVE", rX + rowW - 70, rY + 16, tc.AccentEmerald);
                }
            }

            lineRGBA(renderer, 0, 665, 1280, 665, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderText(renderer, fontSmall, "[A] Aplicar Idioma · [B] Volver al Menú Principal", 80, 680, tc.TextMuted);
        }
        // 7. RENDER SELECTOR DE TEMAS
        break;
        case SCREEN_THEME: {
            boxRGBA(renderer, 0, 0, 1280, 80, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 80, 1280, 80, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderText(renderer, fontTitle, tr().menu_theme, 80, 26, tc.TextPrimary);
            renderText(renderer, fontSmall, tr().menu_theme_sub, 80, 55, tc.TextMuted);

            int themeCount = getThemeCount();
            int trH = 90, trW = 900, trStartX = 190, trStartY = 130;
            for (int ti = 0; ti < themeCount; ti++) {
                int rY = trStartY + ti * (trH + 14);
                bool isSel = (ti == themeSelectIdx);
                bool isActive = (ti == currentThemeIdx());
                SDL_Color bg2 = isSel ? tc.BgCard : tc.BgSurface;
                boxRGBA(renderer, trStartX, rY, trStartX + trW, rY + trH, bg2.r, bg2.g, bg2.b, 255);
                if (isSel) {
                    Uint8 pA = (Uint8)(150 + (int)(105.0f * sinf(animFrame * 0.08f)));
                    rectangleRGBA(renderer, trStartX, rY, trStartX + trW, rY + trH, tc.AccentAmber.r, tc.AccentAmber.g, tc.AccentAmber.b, pA);
                    boxRGBA(renderer, trStartX + 2, rY + 8, trStartX + 6, rY + trH - 8, tc.AccentAmber.r, tc.AccentAmber.g, tc.AccentAmber.b, 255);
                } else {
                    rectangleRGBA(renderer, trStartX, rY, trStartX + trW, rY + trH, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 160);
                }
                // Theme color swatches
                const ThemeColors& tPrev = THEMES[ti];
                boxRGBA(renderer, trStartX + 18, rY + 18, trStartX + 56, rY + trH - 18, tPrev.BgBase.r, tPrev.BgBase.g, tPrev.BgBase.b, 255);
                rectangleRGBA(renderer, trStartX + 18, rY + 18, trStartX + 56, rY + trH - 18, tPrev.AccentCyan.r, tPrev.AccentCyan.g, tPrev.AccentCyan.b, 255);
                SDL_Color swatches[3] = { tPrev.BgSurface, tPrev.AccentCyan, tPrev.AccentEmerald };
                for (int si = 0; si < 3; si++) {
                    int sx = trStartX + 380 + si * 44;
                    boxRGBA(renderer, sx, rY + 26, sx + 34, rY + trH - 26, swatches[si].r, swatches[si].g, swatches[si].b, 255);
                }
                renderText(renderer, fontTitle, tPrev.name, trStartX + 72, rY + 18, isSel ? tc.TextPrimary : tc.TextSecondary);
                renderText(renderer, fontSmall, "BG / Text / Accent", trStartX + 72, rY + 54, tc.TextMuted);
                if (isActive) {
                    boxRGBA(renderer, trStartX + trW - 120, rY + 20, trStartX + trW - 20, rY + trH - 20, tc.BgBase.r, tc.BgBase.g, tc.BgBase.b, 255);
                    rectangleRGBA(renderer, trStartX + trW - 120, rY + 20, trStartX + trW - 20, rY + trH - 20, tc.AccentEmerald.r, tc.AccentEmerald.g, tc.AccentEmerald.b, 255);
                    renderTextCentered(renderer, fontSmall, "ACTIVO", trStartX + trW - 70, rY + 32, tc.AccentEmerald);
                }
            }
            lineRGBA(renderer, 0, 665, 1280, 665, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderText(renderer, fontSmall, "[A] Aplicar Tema  [B] Volver", 80, 680, tc.TextMuted);
        }
        // 8. RENDER ACERCA DE (CON LOS 8 CAMPOS OFICIALES DE ANDROMEDA Y SIN BORDE NARANJA)
        break;
        case SCREEN_ABOUT: {
            boxRGBA(renderer, 0, 0, 1280, 80, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            lineRGBA(renderer, 0, 80, 1280, 80, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

            renderText(renderer, fontTitle, tr().menu_about, 80, 26, tc.TextPrimary);

            int cX = 160, cY = 110, cW = 960, cH = 530;
            boxRGBA(renderer, cX, cY, cX + cW, cY + cH, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            // Borde sutil cian calibrado de Andromeda (sin naranja)
            rectangleRGBA(renderer, cX, cY, cX + cW, cY + cH, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 200);

            drawCleanLogo(renderer, ezfilesLogoTex, cX + 40, cY + 30, 80);

            renderText(renderer, fontTitle, "EZ FILES · ANDROMEDA SUITE", cX + 140, cY + 34, tc.TextPrimary);
            renderText(renderer, fontSmall, "Versión 1.3.0 · Motor Zero-Copy Nativo · devkitA64", cX + 140, cY + 68, tc.AccentCyan);

            lineRGBA(renderer, cX + 40, cY + 125, cX + cW - 40, cY + 125, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);

            int infoY = cY + 145;
            auto renderField = [&](const char* lbl, const char* val, SDL_Color valCol) {
                renderText(renderer, fontSmall, lbl, cX + 40, infoY, tc.TextMuted);
                renderText(renderer, fontBody, val, cX + 280, infoY - 2, valCol);
                infoY += 38;
            };

            renderField(tr().about_author_lbl, tr().about_author, tc.TextPrimary);
            renderField(tr().about_email_lbl, tr().about_email, tc.AccentCyan);
            renderField(tr().about_github_lbl, tr().about_github, tc.AccentEmerald);
            renderField(tr().about_web_lbl, "github.com/lozp1/EzFiles", tc.AccentBlue);
            renderField(tr().about_suite_lbl, "EzFiles Andromeda Edition v1.3.0", tc.AccentViolet);
            renderField(tr().about_portfolio_lbl, "github.com/lozp1", tc.AccentEmerald);
            renderField(tr().about_engine_lbl, "Horizon OS Native (devkitA64)", tc.AccentAmber);
            renderField(tr().about_system_lbl, tr().about_system, tc.TextSecondary);

            lineRGBA(renderer, cX + 40, cY + 465, cX + cW - 40, cY + 465, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderTextCentered(renderer, fontSmall, tr().hint_back, 640, cY + 490, tc.AccentCyan);
        }
        // 9. RENDER MODO BASICO [L] - Sin estilos, fondo negro, texto blanco
        break;
        case SCREEN_SYS: {
            // Fondo completamente negro
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);

            SDL_Color W = { 255, 255, 255, 255 }; // blanco
            SDL_Color G = { 140, 140, 140, 255 }; // gris para subtitulos

            // Cabecera minima
            renderText(renderer, fontJet, "EZ FILES", 40, 20, W);
            renderText(renderer, fontJet, "v1.3.0", 160, 20, G);
            // Linea horizontal simple
            lineRGBA(renderer, 40, 50, 1240, 50, 140, 140, 140, 255);

            // Lista de opciones - texto puro sin estilos
            int startY = 70;
            int rowH   = 72;
            for (int i = 0; i < 7; i++) {
                int rY = startY + i * rowH;
                bool sel = (menuIdx == i);

                if (sel) {
                    renderText(renderer, fontJet, ">", 20, rY + 4, W);
                }

                char line[128];
                snprintf(line, sizeof(line), "  %d. %s", i + 1, defs[i].title);
                renderText(renderer, fontJet, line, 40, rY + 4, sel ? W : G);
            }

            // Pie de pagina
            lineRGBA(renderer, 40, 582, 1240, 582, 140, 140, 140, 255);
            renderText(renderer, fontJet, "[D-PAD] Navegar  [A] Seleccionar  [L] o [B] Modo Grafico", 40, 594, G);
        } break;
        } // switch (currentScreen) - render moderno
        // MODAL DE CONFIRMACION DE SALIDA
        if (showExitConfirmModal) {
            boxRGBA(renderer, 0, 0, 1280, 720, 0, 0, 0, 200);
            int mW = 560, mH = 240;
            int mX = 640 - mW / 2, mY = 360 - mH / 2;
            boxRGBA(renderer, mX, mY, mX + mW, mY + mH, tc.BgSurface.r, tc.BgSurface.g, tc.BgSurface.b, 255);
            rectangleRGBA(renderer, mX, mY, mX + mW, mY + mH, 239, 68, 68, 255);
            renderTextCentered(renderer, fontTitle, tr().exit_confirm_title, 640, mY + 28, tc.TextPrimary);
            lineRGBA(renderer, mX + 24, mY + 68, mX + mW - 24, mY + 68, tc.BorderSubtle.r, tc.BorderSubtle.g, tc.BorderSubtle.b, 255);
            renderTextCentered(renderer, fontBody, tr().exit_confirm_msg, 640, mY + 108, tc.TextSecondary);
            SDL_Color redCol = { 239, 68, 68, 255 };
            renderTextCentered(renderer, fontSmall, tr().exit_confirm_yes, 640 - 130, mY + 168, redCol);
            renderTextCentered(renderer, fontSmall, tr().exit_confirm_no, 640 + 130, mY + 168, tc.TextMuted);
        }

        // NOTIFICACIÓN TOAST
        if (toastTimer > 0 && !toastMessage.empty()) {
            int tW = 440;
            int tH = 46;
            int tX = 640 - (tW / 2);
            int tY = 600;
            boxRGBA(renderer, tX, tY, tX + tW, tY + tH, tc.BgCard.r, tc.BgCard.g, tc.BgCard.b, 240);
            rectangleRGBA(renderer, tX, tY, tX + tW, tY + tH, tc.AccentCyan.r, tc.AccentCyan.g, tc.AccentCyan.b, 255);
            renderTextCentered(renderer, fontSmall, toastMessage, 640, tY + 14, tc.TextPrimary);
        }

        SDL_RenderPresent(renderer);
        wasTouch = isTouch;
    }

    ftp_server::stop();
    mtp_ops::stop();
    sound::closeAudio();
    clearTextCache();
    mtp_usb::teardown();

    if (ezfilesLogoTex) SDL_DestroyTexture(ezfilesLogoTex);
    if (icoExplorer) SDL_DestroyTexture(icoExplorer);
    if (icoUsb) SDL_DestroyTexture(icoUsb);
    if (icoFtp) SDL_DestroyTexture(icoFtp);
    if (icoLang) SDL_DestroyTexture(icoLang);
    if (icoAbout) SDL_DestroyTexture(icoAbout);
    if (icoTheme) SDL_DestroyTexture(icoTheme);
    if (icoExit)  SDL_DestroyTexture(icoExit);
    if (icoFolderSm) SDL_DestroyTexture(icoFolderSm);
    if (icoFileSm) SDL_DestroyTexture(icoFileSm);
    if (icoNspSm) SDL_DestroyTexture(icoNspSm);
    if (icoNroSm) SDL_DestroyTexture(icoNroSm);
    if (icoSavSm) SDL_DestroyTexture(icoSavSm);

    if (fontLogo) TTF_CloseFont(fontLogo);
    if (fontTitle) TTF_CloseFont(fontTitle);
    if (fontBody) TTF_CloseFont(fontBody);
    if (fontSmall) TTF_CloseFont(fontSmall);
    if (fontJet)   TTF_CloseFont(fontJet);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    socketExit();
    nifmExit();
    mtp_usb::teardown(); // Limpia usb:ds si estaba activo
    accountExit();
    psmExit();
    romfsExit();

    return 0;
}
