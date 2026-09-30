#include <switch.h>
#include <borealis.hpp>
#include <nanovg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>

#include "mtp_usb.hpp"
#include "mtp_ops.hpp"
#include "ftp_server.hpp"
#include "config.hpp"
#include "sys_clock.hpp"
#include "qrcodegen.hpp"
#include <borealis/views/cells/cell_radio.hpp>

using namespace brls::literals;

// Portapapeles global del explorador
inline std::vector<std::string> g_clipboardPaths;
inline bool g_clipboardIsCut = false;

// Declaración para reinicialización dinámica al cambiar tema o idioma
void reloadMainActivity(int initialTab = 0, bool reopenSettings = false);

class ExplorerTab;
inline ExplorerTab* g_explorerTab = nullptr;
inline brls::TabFrame* g_mainTabFrame = nullptr;
inline brls::AppletFrame* g_appletFrame = nullptr;
inline std::string g_initialExplorerPath = "sdmc:/";

inline int g_currentTabIndex = 0;

inline void switchToTab(int tabIndex) {
    if (!g_mainTabFrame) return;
    g_currentTabIndex = tabIndex;
    g_mainTabFrame->focusTab(tabIndex);
}

inline void cycleTab(int dir) {
    int next = (g_currentTabIndex + dir) % 6;
    if (next < 0) next += 6;
    switchToTab(next);
}

inline bool getStorageInfo(const char* path, u64& freeBytes, u64& totalBytes) {
    brls::Logger::info("getStorageInfo: antes de statvfs para {}", path);
    struct statvfs s = {};  // inicializar a cero, CRITICO
    if (statvfs(path, &s) != 0) {
        brls::Logger::info("getStorageInfo: statvfs fallo para {}", path);
        freeBytes = 0;
        totalBytes = 0;
        return false;
    }
    brls::Logger::info("getStorageInfo: statvfs OK - bsize={} frsize={} blocks={} bavail={}",
        (unsigned long)s.f_bsize, (unsigned long)s.f_frsize,
        (unsigned long)s.f_blocks, (unsigned long)s.f_bavail);

    // CRITICO: usar u64 en AMBOS operandos antes de multiplicar
    // Si solo se castea uno, la multiplicacion ocurre en 32 bits y desborda
    u64 frsize = (s.f_frsize > 0) ? (u64)s.f_frsize : (u64)s.f_bsize;
    if (frsize == 0) {
        brls::Logger::error("getStorageInfo: frsize y bsize son 0, abortando");
        freeBytes = 0;
        totalBytes = 0;
        return false;
    }

    totalBytes = (u64)s.f_blocks * frsize;
    u64 rawFree = (u64)s.f_bavail * frsize;

    // Sanity check: free no puede ser mayor que total
    freeBytes = (rawFree <= totalBytes) ? rawFree : totalBytes;

    brls::Logger::info("getStorageInfo: total={} free={} (raw free={})",
        totalBytes, freeBytes, rawFree);
    return true;
}

inline bool getNandStorageInfo(u64& freeBytes, u64& totalBytes) {
    (void)freeBytes;
    (void)totalBytes;
    return false;
}

inline void applyAppTheme(const std::string& themeName) {
    bool hasWp = (!AppConfig::get().wallpaperPath.empty() && access(AppConfig::get().wallpaperPath.c_str(), F_OK) == 0);

    if (themeName == "Light") {
        brls::Theme::getLightTheme().addColor("brls/background", hasWp ? nvgRGBA(242, 242, 242, 0) : nvgRGB(242, 242, 242));
        brls::Theme::getLightTheme().addColor("brls/clear", nvgRGB(242, 242, 242));
        brls::Theme::getLightTheme().addColor("brls/sidebar/background", hasWp ? nvgRGBA(255, 255, 255, 220) : nvgRGB(255, 255, 255));
        brls::Theme::getLightTheme().addColor("brls/sidebar/separator", nvgRGB(220, 224, 230));
        brls::Theme::getLightTheme().addColor("brls/applet_frame/separator", nvgRGB(180, 185, 195));
        brls::Theme::getLightTheme().addColor("brls/highlight/background", nvgRGB(225, 230, 240));
        brls::Theme::getLightTheme().addColor("brls/accent", nvgRGB(2, 132, 199));
        brls::Theme::getLightTheme().addColor("brls/text", nvgRGB(17, 24, 39));
        brls::Theme::getLightTheme().addColor("brls/text_disabled", nvgRGB(107, 114, 128));
        brls::Theme::getLightTheme().addColor("brls/sidebar/active_item", nvgRGB(2, 132, 199));
        brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::LIGHT);
    } else if (themeName == "AMOLED") {
        brls::Theme::getDarkTheme().addColor("brls/background", hasWp ? nvgRGBA(0, 0, 0, 0) : nvgRGB(0, 0, 0));
        brls::Theme::getDarkTheme().addColor("brls/clear", nvgRGB(0, 0, 0));
        brls::Theme::getDarkTheme().addColor("brls/sidebar/background", hasWp ? nvgRGBA(10, 10, 10, 220) : nvgRGB(8, 8, 8));
        brls::Theme::getDarkTheme().addColor("brls/sidebar/separator", nvgRGB(28, 28, 30));
        brls::Theme::getDarkTheme().addColor("brls/applet_frame/separator", nvgRGB(40, 40, 45));
        brls::Theme::getDarkTheme().addColor("brls/highlight/background", nvgRGB(22, 22, 24));
        brls::Theme::getDarkTheme().addColor("brls/accent", nvgRGB(0, 242, 254));
        brls::Theme::getDarkTheme().addColor("brls/text", nvgRGB(255, 255, 255));
        brls::Theme::getDarkTheme().addColor("brls/text_disabled", nvgRGB(156, 163, 175));
        brls::Theme::getDarkTheme().addColor("brls/sidebar/active_item", nvgRGB(0, 242, 254));
        brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    } else { // "Dark"
        brls::Theme::getDarkTheme().addColor("brls/background", hasWp ? nvgRGBA(45, 45, 45, 0) : nvgRGB(45, 45, 45));
        brls::Theme::getDarkTheme().addColor("brls/clear", nvgRGB(45, 45, 45));
        brls::Theme::getDarkTheme().addColor("brls/sidebar/background", hasWp ? nvgRGBA(35, 38, 44, 220) : nvgRGB(35, 38, 44));
        brls::Theme::getDarkTheme().addColor("brls/sidebar/separator", nvgRGB(60, 64, 72));
        brls::Theme::getDarkTheme().addColor("brls/applet_frame/separator", nvgRGB(80, 85, 95));
        brls::Theme::getDarkTheme().addColor("brls/highlight/background", nvgRGB(40, 44, 52));
        brls::Theme::getDarkTheme().addColor("brls/accent", nvgRGB(56, 189, 248));
        brls::Theme::getDarkTheme().addColor("brls/text", nvgRGB(255, 255, 255));
        brls::Theme::getDarkTheme().addColor("brls/text_disabled", nvgRGB(156, 163, 175));
        brls::Theme::getDarkTheme().addColor("brls/sidebar/active_item", nvgRGB(56, 189, 248));
        brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    }
}

// --- AppletFrame con soporte para Fondo de Pantalla y Opacidad Dinámica ---
class WallpaperAppletFrame : public brls::AppletFrame {
public:
    WallpaperAppletFrame() : brls::AppletFrame() {}
    WallpaperAppletFrame(brls::View* contentView) : brls::AppletFrame(contentView) {
        this->setHeaderVisibility(brls::Visibility::GONE);
        this->setFooterVisibility(brls::Visibility::VISIBLE);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        std::string wpPath = AppConfig::get().wallpaperPath;
        if (!wpPath.empty() && access(wpPath.c_str(), F_OK) == 0) {
            if (m_lastWpPath != wpPath) {
                if (m_wpImage > 0) {
                    nvgDeleteImage(vg, m_wpImage);
                    m_wpImage = 0;
                }
                m_wpImage = nvgCreateImage(vg, wpPath.c_str(), 0);
                m_lastWpPath = wpPath;
            }

            if (m_wpImage > 0) {
                float op = AppConfig::get().wallpaperOpacity;
                if (op < 0.0f) op = 0.0f;
                if (op > 1.0f) op = 1.0f;

                // 1. Proyectar la imagen de fondo en toda la pantalla
                NVGpaint imgPaint = nvgImagePattern(vg, x, y, width, height, 0.0f, m_wpImage, op);
                nvgBeginPath(vg);
                nvgRect(vg, x, y, width, height);
                nvgFillPaint(vg, imgPaint);
                nvgFill(vg);

                // 2. Modulación de opacidad y tinte de alto contraste
                if (brls::Application::getPlatform()->getThemeVariant() == brls::ThemeVariant::LIGHT) {
                    NVGcolor overlayColor = nvgRGBA(245, 245, 245, (int)(175 + (1.0f - op) * 80.0f));
                    nvgBeginPath(vg);
                    nvgRect(vg, x, y, width, height);
                    nvgFillColor(vg, overlayColor);
                    nvgFill(vg);
                } else {
                    NVGcolor overlayColor = nvgRGBA(20, 20, 20, (int)((1.0f - op) * 220.0f));
                    nvgBeginPath(vg);
                    nvgRect(vg, x, y, width, height);
                    nvgFillColor(vg, overlayColor);
                    nvgFill(vg);
                }
            }
        } else {
            if (m_wpImage > 0) {
                nvgDeleteImage(vg, m_wpImage);
                m_wpImage = 0;
                m_lastWpPath = "";
            }
        }

        brls::AppletFrame::draw(vg, x, y, width, height, style, ctx);
    }

    static brls::View* create() {
        return new WallpaperAppletFrame();
    }

private:
    int m_wpImage = 0;
    std::string m_lastWpPath = "";
};

// --- Actividad contenedora a pantalla completa para cada sección ---
class FeatureActivity : public brls::Activity {
public:
    FeatureActivity(brls::View* contentView)
        : brls::Activity(new WallpaperAppletFrame(contentView))
    {
    }
};

// Utilidades para copia y movimiento por bloques con progreso
inline bool copyFileChunked(const std::string& src, const std::string& dst, std::function<void(size_t)> onProgress) {
    if (src == dst) return true;
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    std::unique_ptr<char[]> buf(new char[65536]);
    size_t n;
    bool ok = true;
    while ((n = fread(buf.get(), 1, 65536, in)) > 0) {
        if (fwrite(buf.get(), 1, n, out) != n) {
            ok = false;
            break;
        }
        if (onProgress) onProgress(n);
    }
    fclose(in);
    fclose(out);
    if (!ok) unlink(dst.c_str());
    return ok;
}

inline bool copyDirectoryRecursive(const std::string& srcDir, const std::string& dstDir, std::function<void(size_t, const std::string&)> onProgress) {
    mkdir(dstDir.c_str(), 0777);
    DIR* d = opendir(srcDir.c_str());
    if (!d) return false;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        std::string sPath = srcDir + "/" + ent->d_name;
        std::string dPath = dstDir + "/" + ent->d_name;
        struct stat st;
        if (stat(sPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                copyDirectoryRecursive(sPath, dPath, onProgress);
            } else {
                if (onProgress) onProgress(0, ent->d_name);
                copyFileChunked(sPath, dPath, [&](size_t bytes) {
                    if (onProgress) onProgress(bytes, ent->d_name);
                });
            }
        }
    }
    closedir(d);
    return true;
}

// Ejecuta la copia o movimiento en segundo plano con diálogo de progreso nativo
inline void runFileOperation(const std::string& opTitle, const std::vector<std::string>& srcPaths, const std::string& destDir, bool isCut, std::function<void()> onDone) {
    if (srcPaths.empty()) return;

    brls::Box* container = new brls::Box(brls::Axis::COLUMN);
    container->setWidth(560);
    container->setPadding(20, 24, 20, 24);

    brls::Label* lblTitle = new brls::Label();
    lblTitle->setText(opTitle);
    lblTitle->setFontSize(20);
    lblTitle->setTextColor(brls::Application::getTheme()["brls/text"]);
    lblTitle->setMarginBottom(12);
    container->addView(lblTitle);

    brls::Label* lblCurrent = new brls::Label();
    lblCurrent->setText(opTitle);
    lblCurrent->setFontSize(14);
    lblCurrent->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    lblCurrent->setMarginBottom(14);
    container->addView(lblCurrent);

    brls::Box* barBg = new brls::Box();
    barBg->setWidth(512);
    barBg->setHeight(10);
    barBg->setCornerRadius(5);
    barBg->setBackgroundColor(nvgRGB(50, 55, 65));
    barBg->setMarginBottom(12);

    brls::Box* barFill = new brls::Box();
    barFill->setWidth(0);
    barFill->setHeight(10);
    barFill->setCornerRadius(5);
    barFill->setBackgroundColor(nvgRGB(0, 242, 254));
    barBg->addView(barFill);
    container->addView(barBg);

    brls::Label* lblStats = new brls::Label();
    lblStats->setText("0% · Calculando...");
    lblStats->setFontSize(13);
    lblStats->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    container->addView(lblStats);

    brls::Dialog* d = new brls::Dialog(container);
    d->setCancelable(false);
    d->open();

    auto isDialogAlive = std::make_shared<std::atomic<bool>>(true);

    std::thread([srcPaths, destDir, isCut, onDone, d, lblCurrent, barFill, lblStats, isDialogAlive]() {
        u64 totalBytes = 0;
        for (const auto& p : srcPaths) {
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) totalBytes += st.st_size;
        }

        u64 transferred = 0;
        auto tStart = std::chrono::steady_clock::now();
        auto tLastUpdate = tStart;

        for (size_t i = 0; i < srcPaths.size(); i++) {
            std::string src = srcPaths[i];
            while (src.length() > 1 && src.back() == '/') src.pop_back();
            size_t slash = src.find_last_of('/');
            std::string name = (slash != std::string::npos) ? src.substr(slash + 1) : src;
            std::string dst = destDir + (destDir.back() == '/' ? "" : "/") + name;

            if (*isDialogAlive) {
                brls::sync([lblCurrent, name, i, srcPaths, isDialogAlive]() {
                    if (*isDialogAlive) {
                        lblCurrent->setText("[" + std::to_string(i + 1) + "/" + std::to_string(srcPaths.size()) + "] " + name);
                    }
                });
            }

            struct stat st;
            bool isDir = (stat(src.c_str(), &st) == 0 && S_ISDIR(st.st_mode));

            if (!isCut && src == dst) {
                size_t dot = name.find_last_of('.');
                if (dot != std::string::npos && !isDir) {
                    dst = destDir + (destDir.back() == '/' ? "" : "/") + name.substr(0, dot) + " (Copia)" + name.substr(dot);
                } else {
                    dst = destDir + (destDir.back() == '/' ? "" : "/") + name + " (Copia)";
                }
            }

            if (isCut) {
                if (src == dst) continue;
                if (rename(src.c_str(), dst.c_str()) == 0) {
                    if (!isDir) transferred += st.st_size;
                    continue;
                }
            }

            if (isDir) {
                copyDirectoryRecursive(src, dst, [&](size_t b, const std::string& curF) {
                    transferred += b;
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration<double>(now - tLastUpdate).count() >= 0.1) {
                        double dt = std::chrono::duration<double>(now - tStart).count();
                        double spd = dt > 0.05 ? (double)transferred / (dt * 1024.0 * 1024.0) : 0;
                        float pct = (totalBytes > 0) ? (float)transferred / (float)totalBytes : 0.5f;
                        if (pct > 1.0f) pct = 1.0f;
                        if (*isDialogAlive) {
                            brls::sync([barFill, lblStats, pct, transferred, totalBytes, spd, isDialogAlive]() {
                                if (*isDialogAlive) {
                                    barFill->setWidth(pct * 512.0f);
                                    char b[128];
                                    snprintf(b, sizeof(b), "%.1f%% · %.1f MB / %.1f MB (%.1f MB/s)",
                                             pct * 100.0f, transferred / (1024.0*1024.0), totalBytes / (1024.0*1024.0), spd);
                                    lblStats->setText(b);
                                }
                            });
                        }
                        tLastUpdate = now;
                    }
                });
                if (isCut) mtp_ops::mtpDelRec(src);
            } else {
                copyFileChunked(src, dst, [&](size_t b) {
                    transferred += b;
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration<double>(now - tLastUpdate).count() >= 0.1) {
                        double dt = std::chrono::duration<double>(now - tStart).count();
                        double spd = dt > 0.05 ? (double)transferred / (dt * 1024.0 * 1024.0) : 0;
                        float pct = (totalBytes > 0) ? (float)transferred / (float)totalBytes : 0.0f;
                        if (pct > 1.0f) pct = 1.0f;
                        if (*isDialogAlive) {
                            brls::sync([barFill, lblStats, pct, transferred, totalBytes, spd, isDialogAlive]() {
                                if (*isDialogAlive) {
                                    barFill->setWidth(pct * 512.0f);
                                    char b[128];
                                    snprintf(b, sizeof(b), "%.1f%% · %.1f MB / %.1f MB (%.1f MB/s)",
                                             pct * 100.0f, transferred / (1024.0*1024.0), totalBytes / (1024.0*1024.0), spd);
                                    lblStats->setText(b);
                                }
                            });
                        }
                        tLastUpdate = now;
                    }
                });
                if (isCut) unlink(src.c_str());
            }
        }

        // Dar un breve retraso para asegurar que la animación de apertura del diálogo no colisione con el cierre
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        brls::sync([d, onDone, isDialogAlive]() {
            *isDialogAlive = false;
            d->close([onDone]() {
                if (onDone) onDone();
                brls::Application::notify("hints/op_finished"_i18n);
            });
        });
    }).detach();
}

// Teclado nativo de Horizon OS (Swkbd)
inline std::string showHorizonKeyboard(const std::string& headerText, const std::string& initialText) {
    char outText[256] = {0};
    SwkbdConfig kbd;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetHeaderText(&kbd, headerText.c_str());
        swkbdConfigSetInitialText(&kbd, initialText.c_str());
        swkbdShow(&kbd, outText, sizeof(outText));
        swkbdClose(&kbd);
        if (outText[0] != '\0') return std::string(outText);
    }
    return initialText;
}

// Checkmark vectorial nativo estilo Nintendo Switch (círculo esmeralda/turquesa con tilde blanca)
class CheckmarkView : public brls::View {
public:
    CheckmarkView(float radius = 11.0f) : m_radius(radius) {
        this->setWidth(radius * 2.0f);
        this->setHeight(radius * 2.0f);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;

        // Círculo sólido color esmeralda (#00E6A8)
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, m_radius);
        nvgFillColor(vg, nvgRGB(0, 230, 168));
        nvgFill(vg);

        // Tilde / Checkmark vectorial en trazo blanco
        nvgBeginPath(vg);
        nvgStrokeColor(vg, nvgRGB(255, 255, 255));
        nvgStrokeWidth(vg, 2.2f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);

        nvgMoveTo(vg, cx - m_radius * 0.45f, cy + m_radius * 0.05f);
        nvgLineTo(vg, cx - m_radius * 0.10f, cy + m_radius * 0.42f);
        nvgLineTo(vg, cx + m_radius * 0.48f, cy - m_radius * 0.38f);
        nvgStroke(vg);
    }

private:
    float m_radius;
};

// Generador y renderizador vectorial de Código QR (para conexión móvil y donaciones)
class QrCodeView : public brls::View {
public:
    QrCodeView(const std::string& text, float size = 180.0f) : m_text(text), m_qrSize(size) {
        this->setWidth(size);
        this->setHeight(size);
        generateQr();
    }

    void setText(const std::string& text) {
        if (m_text != text) {
            m_text = text;
            generateQr();
        }
    }

    void generateQr() {
        if (m_text.empty()) {
            m_modules.clear();
            m_size = 0;
            return;
        }
        try {
            qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(m_text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            m_size = qr.getSize();
            m_modules.resize(m_size * m_size);
            for (int y = 0; y < m_size; y++) {
                for (int x = 0; x < m_size; x++) {
                    m_modules[y * m_size + x] = qr.getModule(x, y);
                }
            }
        } catch (...) {
            m_modules.clear();
            m_size = 0;
        }
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        if (m_modules.empty() || m_size <= 0) return;

        // Fondo blanco con bordes redondeados
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, 8.0f);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);

        float pad = 8.0f;
        float drawArea = width - pad * 2.0f;
        float moduleSize = drawArea / (float)m_size;

        nvgBeginPath(vg);
        for (int row = 0; row < m_size; row++) {
            for (int col = 0; col < m_size; col++) {
                if (m_modules[row * m_size + col]) {
                    nvgRect(vg, x + pad + col * moduleSize, y + pad + row * moduleSize, moduleSize + 0.2f, moduleSize + 0.2f);
                }
            }
        }
        nvgFillColor(vg, nvgRGB(10, 10, 10));
        nvgFill(vg);
    }

private:
    std::string m_text;
    float m_qrSize;
    int m_size = 0;
    std::vector<bool> m_modules;
};

// Checkbox estilo Horizon OS para selección múltiple
class SelectionBoxView : public brls::View {
public:
    SelectionBoxView(bool selected = false, float size = 22.0f) : m_selected(selected), m_size(size) {
        this->setWidth(size);
        this->setHeight(size);
    }

    void setSelected(bool sel) {
        m_selected = sel;
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        float r = 4.0f;
        if (m_selected) {
            // Fondo lleno turquesa / esmeralda (#00E6A8)
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, r);
            nvgFillColor(vg, nvgRGB(0, 230, 168));
            nvgFill(vg);

            // Tilde blanca nítida
            nvgBeginPath(vg);
            nvgStrokeColor(vg, nvgRGB(255, 255, 255));
            nvgStrokeWidth(vg, 2.2f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgMoveTo(vg, x + width * 0.22f, y + height * 0.52f);
            nvgLineTo(vg, x + width * 0.44f, y + height * 0.74f);
            nvgLineTo(vg, x + width * 0.80f, y + height * 0.28f);
            nvgStroke(vg);
        } else {
            // Marco redondeado sutil (#5A6270)
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, r);
            nvgStrokeColor(vg, nvgRGBA(140, 145, 155, 160));
            nvgStrokeWidth(vg, 1.8f);
            nvgStroke(vg);
        }
    }

private:
    bool m_selected = false;
    float m_size;
};

// --- Vector Icons para el Menú Contextual (Resolución nativa NanoVG) ---
enum class ActionIconType {
    COPY,
    CUT,
    PASTE,
    RENAME,
    DELETE,
    PROPERTIES,
    UNSELECT,
    CANCEL
};

class ActionIconView : public brls::View {
public:
    ActionIconView(ActionIconType type, NVGcolor color = nvgRGB(255, 255, 255))
        : m_type(type), m_color(color) {
        this->setWidth(24);
        this->setHeight(24);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        nvgSave(vg);
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;
        NVGcolor col = m_color;
        if (col.a == 0.0f) {
            col = brls::Application::getTheme()["brls/text"];
        }

        switch (m_type) {
            case ActionIconType::COPY: {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + 3.0f, y + 2.0f, 12.0f, 14.0f, 2.0f);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgStroke(vg);

                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + 8.0f, y + 7.0f, 13.0f, 15.0f, 2.0f);
                nvgFillColor(vg, brls::Application::getTheme()["brls/background"]);
                nvgFill(vg);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgStroke(vg);
                break;
            }
            case ActionIconType::CUT: {
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgBeginPath(vg);
                nvgCircle(vg, x + 7.0f, y + 17.0f, 3.2f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgCircle(vg, x + 17.0f, y + 17.0f, 3.2f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 8.0f, y + 14.0f);
                nvgLineTo(vg, x + 18.0f, y + 4.5f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 16.0f, y + 14.0f);
                nvgLineTo(vg, x + 6.0f, y + 4.5f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgCircle(vg, cx, y + 11.5f, 1.2f);
                nvgFillColor(vg, col);
                nvgFill(vg);
                break;
            }
            case ActionIconType::PASTE: {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + 4.0f, y + 4.5f, 16.0f, 17.0f, 2.5f);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + 8.0f, y + 1.5f, 8.0f, 4.5f, 1.5f);
                nvgFillColor(vg, col);
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 8.0f, y + 11.0f);
                nvgLineTo(vg, x + 16.0f, y + 11.0f);
                nvgMoveTo(vg, x + 8.0f, y + 15.0f);
                nvgLineTo(vg, x + 14.0f, y + 15.0f);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
                break;
            }
            case ActionIconType::RENAME: {
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 2.0f);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 5.0f, y + 18.0f);
                nvgLineTo(vg, x + 17.0f, y + 6.0f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 4.0f, y + 21.0f);
                nvgLineTo(vg, x + 20.0f, y + 21.0f);
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
                break;
            }
            case ActionIconType::DELETE: {
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 4.0f, y + 7.0f);
                nvgLineTo(vg, x + 20.0f, y + 7.0f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 9.5f, y + 7.0f);
                nvgLineTo(vg, x + 9.5f, y + 4.5f);
                nvgLineTo(vg, x + 14.5f, y + 4.5f);
                nvgLineTo(vg, x + 14.5f, y + 7.0f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + 6.0f, y + 8.5f, 12.0f, 13.0f, 2.0f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 10.0f, y + 11.5f);
                nvgLineTo(vg, x + 10.0f, y + 18.0f);
                nvgMoveTo(vg, x + 14.0f, y + 11.5f);
                nvgLineTo(vg, x + 14.0f, y + 18.0f);
                nvgStrokeWidth(vg, 1.4f);
                nvgStroke(vg);
                break;
            }
            case ActionIconType::PROPERTIES: {
                nvgBeginPath(vg);
                nvgCircle(vg, cx, cy, 9.0f);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgCircle(vg, cx, cy - 4.0f, 1.3f);
                nvgFillColor(vg, col);
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, cx, cy - 1.0f);
                nvgLineTo(vg, cx, cy + 4.5f);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 2.0f);
                nvgStroke(vg);
                break;
            }
            case ActionIconType::UNSELECT: {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + 4.0f, y + 4.0f, 16.0f, 16.0f, 3.0f);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, x + 7.5f, cy);
                nvgLineTo(vg, x + 16.5f, cy);
                nvgStroke(vg);
                break;
            }
            case ActionIconType::CANCEL: {
                nvgBeginPath(vg);
                nvgCircle(vg, cx, cy, 9.0f);
                nvgStrokeColor(vg, col);
                nvgStrokeWidth(vg, 1.8f);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, cx - 3.5f, cy - 3.5f);
                nvgLineTo(vg, cx + 3.5f, cy + 3.5f);
                nvgMoveTo(vg, cx + 3.5f, cy - 3.5f);
                nvgLineTo(vg, cx - 3.5f, cy + 3.5f);
                nvgStroke(vg);
                break;
            }
        }
        nvgRestore(vg);
    }

private:
    ActionIconType m_type;
    NVGcolor m_color;
};

// Fila moderna de archivo o directorio estilo consola con soporte para selección múltiple
class FileRowCell : public brls::Box {
public:
    FileRowCell(const std::string& iconPath, const std::string& name, const std::string& subText, const std::string& badgeText, bool isFolder, bool isSelected = false, bool showCheckbox = true)
        : m_isSelected(isSelected) {
        this->setFocusable(true);
        this->setHeight(56);
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(0, 18, 0, 18);
        this->setCornerRadius(6);

        // Checkbox de selección a la izquierda del icono
        if (showCheckbox) {
            m_selectionBox = new SelectionBoxView(isSelected, 22.0f);
            m_selectionBox->setMarginRight(14);
            this->addView(m_selectionBox);
        }

        // Icono a la izquierda con selección automática de tema
        std::string finalIcon = iconPath;
        if (brls::Application::getPlatform()->getThemeVariant() == brls::ThemeVariant::LIGHT) {
            size_t dot = iconPath.find_last_of('.');
            if (dot != std::string::npos) {
                std::string darkPath = iconPath.substr(0, dot) + "_dark" + iconPath.substr(dot);
                if (access(darkPath.c_str(), F_OK) == 0) finalIcon = darkPath;
            }
        }
        brls::Image* img = new brls::Image();
        img->setImageFromFile(finalIcon);
        img->setWidth(28);
        img->setHeight(28);
        img->setMarginRight(14);
        this->addView(img);

        // Columna vertical: Nombre + Fecha / Subtítulo
        brls::Box* col = new brls::Box(brls::Axis::COLUMN);
        col->setGrow(1.0f);
        col->setJustifyContent(brls::JustifyContent::CENTER);

        brls::Label* lblName = new brls::Label();
        lblName->setText(name);
        lblName->setFontSize(17);
        col->addView(lblName);

        if (!subText.empty()) {
            brls::Label* lblSub = new brls::Label();
            lblSub->setText(subText);
            lblSub->setFontSize(13);
            lblSub->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
            lblSub->setMarginTop(2);
            col->addView(lblSub);
        }
        this->addView(col);

        // Badge pill estilizado a la derecha
        brls::Box* badge = new brls::Box();
        badge->setPadding(4, 10, 4, 10);
        badge->setCornerRadius(4);
        if (isFolder) {
            badge->setBackgroundColor(nvgRGBA(56, 189, 248, 35));
        } else {
            badge->setBackgroundColor(nvgRGBA(128, 128, 128, 25));
        }

        brls::Label* lblBadge = new brls::Label();
        lblBadge->setText(badgeText);
        lblBadge->setFontSize(13);
        if (isFolder) {
            lblBadge->setTextColor(nvgRGB(56, 189, 248));
        } else {
            lblBadge->setTextColor(brls::Application::getTheme()["brls/text"]);
        }
        badge->addView(lblBadge);
        this->addView(badge);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        if (m_isSelected) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, 6.0f);
            nvgFillColor(vg, nvgRGBA(0, 230, 168, 35));
            nvgFill(vg);

            nvgBeginPath(vg);
            nvgRoundedRect(vg, x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f, 6.0f);
            nvgStrokeColor(vg, nvgRGB(0, 230, 168));
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);
        }
        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

    void setSelected(bool sel) {
        m_isSelected = sel;
        if (m_selectionBox) m_selectionBox->setSelected(sel);
    }

    bool isSelected() const { return m_isSelected; }

private:
    bool m_isSelected = false;
    SelectionBoxView* m_selectionBox = nullptr;
};

// --- Tab 1: Explorador de Archivos ---
class ExplorerTab : public brls::Box {
public:
    ExplorerTab() {
        g_explorerTab = this;
        this->inflateFromXMLFile("romfs:/xml/view_explorer.xml");
        currentPath = g_initialExplorerPath;
        g_initialExplorerPath = "sdmc:/";
        refreshList();

        // Botón B: Subir de carpeta o limpiar selección múltiple o regresar al Menú Principal si está en la raíz
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) {
            if (!m_selectedPaths.empty()) {
                m_selectedPaths.clear();
                refreshList();
                return true;
            }
            if (currentPath != "sdmc:/" && currentPath != "sdmc:" && currentPath != "/" && currentPath != "partitions:") {
                size_t slash = currentPath.find_last_of('/', currentPath.length() - 2);
                if (slash != std::string::npos) {
                    currentPath = currentPath.substr(0, slash + 1);
                } else {
                    currentPath = "sdmc:/";
                }
                m_selectedPaths.clear();
                refreshList();
                return true;
            }
            // En raíz o particiones: regresar limpiamente al Menú Principal
            brls::Application::popActivity();
            return true;
        });

        // Botón Y: Pegar elementos del portapapeles en el directorio actual
        this->registerAction("hints/paste"_i18n, brls::BUTTON_Y, [this](brls::View*) {
            if (!g_clipboardPaths.empty()) {
                pasteClipboard();
                return true;
            }
            return false;
        });
    }

    void willAppear(bool resetState = false) override {
        brls::Box::willAppear(resetState);
        brls::Box* boxFiles = dynamic_cast<brls::Box*>(this->getView("boxFiles"));
        if (boxFiles && !boxFiles->getChildren().empty()) {
            brls::Application::giveFocus(boxFiles->getChildren()[0]);
        }
    }

    ~ExplorerTab() {
        if (g_explorerTab == this) g_explorerTab = nullptr;
    }

    static brls::View* create() {
        return new ExplorerTab();
    }

    void showPartitions() {
        currentPath = "partitions:";
        refreshList();
    }

    void refreshList() {
        brls::Box* boxFiles = dynamic_cast<brls::Box*>(this->getView("boxFiles"));
        brls::Label* lblCurrentPath = dynamic_cast<brls::Label*>(this->getView("lblCurrentPath"));
        brls::Label* lblItemCount = dynamic_cast<brls::Label*>(this->getView("lblItemCount"));

        if (!boxFiles) return;
        boxFiles->clearViews();

        brls::ScrollingFrame* scrollFiles = dynamic_cast<brls::ScrollingFrame*>(this->getView("scrollFiles"));
        if (scrollFiles) {
            scrollFiles->setContentOffsetY(0.0f, false);
        }

        // Configuración de la barra superior de acciones rápidas
        brls::Box* btnTopSelect = dynamic_cast<brls::Box*>(this->getView("btnTopSelect"));
        brls::Label* lblTopSelectText = dynamic_cast<brls::Label*>(this->getView("lblTopSelectText"));
        brls::Box* btnTopPaste = dynamic_cast<brls::Box*>(this->getView("btnTopPaste"));
        brls::Label* lblTopPasteText = dynamic_cast<brls::Label*>(this->getView("lblTopPasteText"));
        brls::Box* btnTopOptions = dynamic_cast<brls::Box*>(this->getView("btnTopOptions"));

        if (btnTopSelect) {
            if (lblTopSelectText) {
                lblTopSelectText->setText(m_selectedPaths.empty() ? "[X] Seleccionar" : ("[X] Limpiar (" + std::to_string(m_selectedPaths.size()) + ")"));
            }
            btnTopSelect->registerClickAction([this](brls::View*) {
                if (!m_selectedPaths.empty()) {
                    m_selectedPaths.clear();
                    refreshList();
                } else {
                    brls::Application::notify("hints/select_hint"_i18n);
                }
                return true;
            });
        }

        if (btnTopPaste) {
            if (lblTopPasteText) {
                lblTopPasteText->setText("[Y] Pegar");
            }
            btnTopPaste->registerClickAction([this](brls::View*) {
                pasteClipboard();
                return true;
            });
        }

        if (btnTopOptions) {
            btnTopOptions->registerClickAction([this](brls::View*) {
                showFileActionsDialog(currentPath, "Opciones de Carpeta", true, 0, 0);
                return true;
            });
        }

        if (currentPath == "partitions:") {
            if (lblCurrentPath) lblCurrentPath->setText("Particiones del Sistema (MTP / DBI)");
            struct PartEntry {
                std::string name;
                std::string path;
                std::string desc;
                std::string icon;
            };
            std::vector<PartEntry> parts = {
                { "1: SD Card", "sdmc:/", "Almacenamiento microSD completo", "romfs:/img/icon_folder_sm.png" },
                { "2: 1: Nand (USER)", "", "Partición interna de usuario (NAND)", "romfs:/img/icon_folder_sm.png" },
                { "3: 2: Nand (SYSTEM)", "", "Horizon OS System (Solo lectura)", "romfs:/img/icon_folder_sm.png" },
                { "4: 3: Installed Games", "games:", "Títulos instalados en microSD / NAND", "romfs:/img/icon_nsp_sm.png" },
                { "5: 4: MicroSD Install", "sdmc:/switch/FileZzz/install_sd/", "Instalación directa a tarjeta SD", "romfs:/img/icon_nsp_sm.png" },
                { "6: 5: NAND Install", "sdmc:/switch/FileZzz/install_nand/", "Instalación directa a memoria interna", "romfs:/img/icon_nsp_sm.png" },
                { "7: 6: Saves", "sdmc:/switch/FileZzz/saves/", "Respaldos de partidas guardadas", "romfs:/img/icon_sav_sm.png" },
                { "8: 7: Album", "sdmc:/Nintendo/Album/", "Capturas y grabaciones", "romfs:/img/icon_folder_sm.png" }
            };

            int count = 0;
            for (const auto& p : parts) {
                FileRowCell* cell = new FileRowCell(p.icon, p.name, p.desc, "[ VIRTUAL ]", true);
                if (p.name == "4: 3: Installed Games") {
                    cell->registerClickAction([](brls::View*) {
                        switchToTab(1);
                        return true;
                    });
                } else if (!p.path.empty()) {
                    std::string target = p.path;
                    cell->registerClickAction([this, target](brls::View*) {
                        currentPath = target;
                        refreshList();
                        return true;
                    });
                } else {
                    std::string pName = p.name;
                    std::string pDesc = p.desc;
                    cell->registerClickAction([pName, pDesc](brls::View*) {
                        brls::Dialog* d = new brls::Dialog(pName + "\n" + pDesc + "\n(Partición activa y montada en conexión MTP USB)");
                        d->addButton("hints/ok"_i18n, []() {});
                        d->open();
                        return true;
                    });
                }
                boxFiles->addView(cell);
                count++;
            }
            if (lblItemCount) lblItemCount->setText(std::to_string(count) + " particiones");
            return;
        }

        DIR* dir = opendir(currentPath.c_str());
        if (!dir) return;

        if (lblCurrentPath) lblCurrentPath->setText(currentPath);

        struct dirent* entry;
        m_totalItems = 0;
        std::vector<std::pair<std::string, bool>> items;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            items.push_back({entry->d_name, entry->d_type == DT_DIR});
        }
        closedir(dir);

        // Ordenar primero carpetas alfabéticamente y luego archivos
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        for (const auto& it : items) {
            std::string name = it.first;
            bool isDir = it.second;
            std::string fullPath = currentPath + (currentPath.back() == '/' ? "" : "/") + name;

            struct stat st;
            u64 sz = 0;
            time_t mt = 0;
            if (stat(fullPath.c_str(), &st) == 0) {
                sz = st.st_size;
                mt = st.st_mtime;
            }

            char dateBuf[64] = "";
            if (mt > 0) {
                struct tm* tmInfo = localtime(&mt);
                if (tmInfo) strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d  %H:%M", tmInfo);
            }

            std::string iconPath = "romfs:/img/icon_file_sm.png";
            std::string badgeText;

            if (isDir) {
                iconPath = "romfs:/img/icon_folder_sm.png";
                badgeText = "hints/folder"_i18n;
            } else {
                size_t dot = name.find_last_of('.');
                if (dot != std::string::npos) {
                    std::string ext = name.substr(dot);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".nsp" || ext == ".nsz" || ext == ".xci") iconPath = "romfs:/img/icon_nsp_sm.png";
                    else if (ext == ".nro") iconPath = "romfs:/img/icon_nro_sm.png";
                    else if (ext == ".sav" || ext == ".dat") iconPath = "romfs:/img/icon_sav_sm.png";
                }

                char sBuf[32];
                if (sz < 1024) snprintf(sBuf, sizeof(sBuf), "%llu B", (unsigned long long)sz);
                else if (sz < 1024*1024) snprintf(sBuf, sizeof(sBuf), "%.1f KB", sz / 1024.0);
                else if (sz < 1024*1024*1024) snprintf(sBuf, sizeof(sBuf), "%.2f MB", sz / (1024.0*1024.0));
                else snprintf(sBuf, sizeof(sBuf), "%.2f GB", sz / (1024.0*1024.0*1024.0));
                badgeText = sBuf;
            }

            bool isSelected = (m_selectedPaths.count(fullPath) > 0);
            FileRowCell* cell = new FileRowCell(iconPath, name, dateBuf, badgeText, isDir, isSelected, true);

            if (isDir) {
                cell->registerClickAction([this, fullPath, cell](brls::View*) {
                    if (!m_selectedPaths.empty()) {
                        toggleSelection(fullPath, cell);
                    } else {
                        currentPath = fullPath + "/";
                        m_selectedPaths.clear();
                        refreshList();
                    }
                    return true;
                });
            } else {
                cell->registerClickAction([this, fullPath, name, sz, mt, cell](brls::View*) {
                    if (!m_selectedPaths.empty()) {
                        toggleSelection(fullPath, cell);
                    } else {
                        showFileProperties(fullPath, name, false, sz, mt);
                    }
                    return true;
                });
            }

            // Atajo con Botón X para alternar selección múltiple (oculto en footer para mantenerlo limpio)
            cell->registerAction("hints/select"_i18n, brls::BUTTON_X, [this, fullPath, cell](brls::View*) {
                toggleSelection(fullPath, cell);
                return true;
            }, true);

            // Atajo con Botón + (START) para menú contextual de opciones (oculto en footer)
            cell->registerAction("hints/options"_i18n, brls::BUTTON_START, [this, fullPath, name, isDir, sz, mt](brls::View*) {
                showFileActionsDialog(fullPath, name, isDir, sz, mt);
                return true;
            }, true);

            // Atajo con Botón Y para pegar si hay elementos en el portapapeles (oculto en footer)
            if (!g_clipboardPaths.empty()) {
                cell->registerAction("hints/paste"_i18n, brls::BUTTON_Y, [this](brls::View*) {
                    pasteClipboard();
                    return true;
                }, true);
            }

            boxFiles->addView(cell);
            m_totalItems++;
        }

        updateItemCountLabel();
        if (!boxFiles->getChildren().empty()) {
            brls::Application::giveFocus(boxFiles->getChildren()[0]);
        } else {
            brls::Box* btnTopOptions = dynamic_cast<brls::Box*>(this->getView("btnTopOptions"));
            if (btnTopOptions) brls::Application::giveFocus(btnTopOptions);
        }
    }

    void toggleSelection(const std::string& fullPath, FileRowCell* cell) {
        if (m_selectedPaths.count(fullPath)) {
            m_selectedPaths.erase(fullPath);
            if (cell) cell->setSelected(false);
        } else {
            m_selectedPaths.insert(fullPath);
            if (cell) cell->setSelected(true);
        }
        updateItemCountLabel();
    }

    void updateItemCountLabel() {
        brls::Label* lblItemCount = dynamic_cast<brls::Label*>(this->getView("lblItemCount"));
        if (!lblItemCount) return;
        if (m_selectedPaths.empty()) {
            lblItemCount->setText(std::to_string(m_totalItems) + " " + "hints/items"_i18n);
        } else {
            lblItemCount->setText(std::to_string(m_totalItems) + " " + "hints/items"_i18n + " (" + std::to_string(m_selectedPaths.size()) + " " + "hints/selected"_i18n + ")");
        }
    }

    void pasteClipboard() {
        if (g_clipboardPaths.empty()) return;
        if (currentPath == "partitions:") {
            brls::Application::notify("No se puede pegar en la vista de particiones.");
            return;
        }
        std::vector<std::string> paths = g_clipboardPaths;
        bool isCut = g_clipboardIsCut;
        std::string destDir = currentPath;

        // Si es cortar/mover un archivo/carpeta único en el mismo sistema de archivos, mover directamente
        if (isCut && paths.size() == 1) {
            std::string src = paths[0];
            while (src.length() > 1 && src.back() == '/') src.pop_back();
            size_t slash = src.find_last_of('/');
            std::string name = (slash != std::string::npos) ? src.substr(slash + 1) : src;
            std::string dst = destDir + (destDir.back() == '/' ? "" : "/") + name;
            if (src == dst) {
                g_clipboardPaths.clear();
                g_clipboardIsCut = false;
                m_selectedPaths.clear();
                refreshList();
                return;
            }
            if (rename(src.c_str(), dst.c_str()) == 0) {
                g_clipboardPaths.clear();
                g_clipboardIsCut = false;
                m_selectedPaths.clear();
                refreshList();
                brls::Application::notify("hints/op_finished"_i18n);
                return;
            }
        }

        runFileOperation(isCut ? "hints/moving"_i18n : "hints/copying"_i18n, paths, destDir, isCut, [this, isCut]() {
            if (isCut) {
                g_clipboardPaths.clear();
                g_clipboardIsCut = false;
            }
            m_selectedPaths.clear();
            refreshList();
        });
    }

    void showFileProperties(const std::string& fullPath, const std::string& name, bool isDir, u64 sz, time_t mtime) {
        brls::Box* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(640);
        card->setPadding(20, 24, 20, 24);

        // Encabezado con icono grande (72x72) y títulos alineados
        brls::Box* topRow = new brls::Box(brls::Axis::ROW);
        topRow->setAlignItems(brls::AlignItems::CENTER);
        topRow->setMarginBottom(16);

        std::string iconPath = isDir ? "romfs:/img/icon_folder_sm.png" : "romfs:/img/icon_file_sm.png";
        if (!isDir) {
            size_t dot = name.find_last_of('.');
            if (dot != std::string::npos) {
                std::string ext = name.substr(dot);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".nsp" || ext == ".nsz" || ext == ".xci") iconPath = "romfs:/img/icon_nsp_sm.png";
                else if (ext == ".nro") iconPath = "romfs:/img/icon_nro_sm.png";
                else if (ext == ".sav" || ext == ".dat") iconPath = "romfs:/img/icon_sav_sm.png";
            }
        }
        if (brls::Application::getPlatform()->getThemeVariant() == brls::ThemeVariant::LIGHT) {
            size_t dot = iconPath.find_last_of('.');
            if (dot != std::string::npos) {
                std::string darkPath = iconPath.substr(0, dot) + "_dark" + iconPath.substr(dot);
                if (access(darkPath.c_str(), F_OK) == 0) iconPath = darkPath;
            }
        }

        brls::Image* img = new brls::Image();
        img->setImageFromFile(iconPath);
        img->setWidth(72);
        img->setHeight(72);
        img->setMarginRight(18);
        topRow->addView(img);

        brls::Box* topCol = new brls::Box(brls::Axis::COLUMN);
        topCol->setGrow(1.0f);

        brls::Label* lblName = new brls::Label();
        lblName->setText(name);
        lblName->setFontSize(20);
        lblName->setTextColor(brls::Application::getTheme()["brls/text"]);
        topCol->addView(lblName);

        std::string subTitle = isDir ? "hints/prop_folder_type"_i18n : "hints/prop_file_type"_i18n;
        size_t dot = name.find_last_of('.');
        if (!isDir && dot != std::string::npos) {
            std::string ext = name.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
            subTitle += " (" + ext + ")";
        }
        brls::Label* lblSub = new brls::Label();
        lblSub->setText(subTitle);
        lblSub->setFontSize(14);
        lblSub->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        lblSub->setMarginTop(4);
        topCol->addView(lblSub);

        topRow->addView(topCol);
        card->addView(topRow);

        // Separador sutil
        brls::Box* sep = new brls::Box();
        sep->setWidthPercentage(100.0f);
        sep->setHeight(1);
        sep->setBackgroundColor(brls::Application::getTheme()["brls/sidebar/separator"]);
        sep->setMarginBottom(14);
        card->addView(sep);

        auto addDetailRow = [&](const std::string& key, const std::string& val) {
            brls::Box* r = new brls::Box(brls::Axis::ROW);
            r->setWidthPercentage(100.0f);
            r->setHeight(36);
            r->setAlignItems(brls::AlignItems::CENTER);
            r->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

            brls::Label* lKey = new brls::Label();
            lKey->setText(key);
            lKey->setFontSize(15);
            lKey->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
            r->addView(lKey);

            brls::Label* lVal = new brls::Label();
            lVal->setText(val);
            lVal->setFontSize(15);
            lVal->setTextColor(brls::Application::getTheme()["brls/text"]);
            r->addView(lVal);

            card->addView(r);
        };

        addDetailRow("hints/prop_path"_i18n, fullPath);

        char szBuf[64];
        if (isDir) {
            int itemsCount = 0;
            DIR* d = opendir(fullPath.c_str());
            if (d) {
                struct dirent* de;
                while ((de = readdir(d)) != NULL) {
                    if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) itemsCount++;
                }
                closedir(d);
            }
            addDetailRow("hints/prop_content"_i18n, std::to_string(itemsCount) + " " + "hints/items"_i18n);
        } else {
            if (sz < 1024) snprintf(szBuf, sizeof(szBuf), "%llu bytes", (unsigned long long)sz);
            else if (sz < 1024*1024) snprintf(szBuf, sizeof(szBuf), "%.2f KB (%llu bytes)", sz / 1024.0, (unsigned long long)sz);
            else if (sz < 1024*1024*1024) snprintf(szBuf, sizeof(szBuf), "%.2f MB (%llu bytes)", sz / (1024.0*1024.0), (unsigned long long)sz);
            else snprintf(szBuf, sizeof(szBuf), "%.2f GB (%llu bytes)", sz / (1024.0*1024.0*1024.0), (unsigned long long)sz);
            addDetailRow("hints/prop_size"_i18n, szBuf);
        }

        char dateBuf[64] = "";
        if (mtime > 0) {
            struct tm* tmInfo = localtime(&mtime);
            if (tmInfo) strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d  %H:%M:%S", tmInfo);
        } else {
            snprintf(dateBuf, sizeof(dateBuf), "%s", "hints/unknown"_i18n.c_str());
        }
        addDetailRow("hints/prop_modified"_i18n, dateBuf);
        addDetailRow("hints/prop_permissions"_i18n, "hints/prop_rw"_i18n);

        brls::Dialog* d = new brls::Dialog(card);
        d->addButton("hints/ok"_i18n, []() {});
        d->open();
    }

    // Menú contextual orgánico nativo de Horizon OS (Ancho fijo de 640px con iconos vectoriales)
    void showFileActionsDialog(const std::string& fullPath, const std::string& name, bool isDir, u64 sz, time_t mtime) {
        brls::Box* menuBox = new brls::Box(brls::Axis::COLUMN);
        menuBox->setWidth(640);
        menuBox->setPadding(20, 24, 16, 24);

        bool isMulti = (m_selectedPaths.size() > 1 && m_selectedPaths.count(fullPath) > 0);
        std::string titleStr = isMulti ? (std::to_string(m_selectedPaths.size()) + " " + "hints/selected"_i18n) : name;

        // Cabecera elegante
        brls::Box* hdr = new brls::Box(brls::Axis::ROW);
        hdr->setWidthPercentage(100.0f);
        hdr->setAlignItems(brls::AlignItems::CENTER);
        hdr->setMarginBottom(14);

        brls::Image* img = new brls::Image();
        img->setImageFromFile(isDir ? "romfs:/img/icon_folder_sm.png" : "romfs:/img/icon_file_sm.png");
        img->setWidth(30);
        img->setHeight(30);
        img->setMarginRight(14);
        hdr->addView(img);

        brls::Label* lblHdr = new brls::Label();
        lblHdr->setText(titleStr);
        lblHdr->setFontSize(20);
        lblHdr->setTextColor(brls::Application::getTheme()["brls/text"]);
        hdr->addView(lblHdr);
        menuBox->addView(hdr);

        brls::Box* sep = new brls::Box();
        sep->setWidth(592);
        sep->setHeight(1);
        sep->setBackgroundColor(brls::Application::getTheme()["brls/sidebar/separator"]);
        sep->setMarginBottom(14);
        menuBox->addView(sep);

        brls::Dialog* d = new brls::Dialog(menuBox);

        auto addActionRow = [&](const std::string& text, ActionIconType iconType, NVGcolor iconColor, std::function<void()> cb, bool isDanger = false) {
            brls::Box* row = new brls::Box(brls::Axis::ROW);
            row->setFocusable(true);
            row->setWidth(592);
            row->setHeight(52);
            row->setAlignItems(brls::AlignItems::CENTER);
            row->setPadding(0, 18, 0, 18);
            row->setCornerRadius(6);
            row->setMarginBottom(6);

            ActionIconView* ic = new ActionIconView(iconType, iconColor);
            ic->setMarginRight(16);
            row->addView(ic);

            brls::Label* lbl = new brls::Label();
            lbl->setText(text);
            lbl->setFontSize(17);
            lbl->setTextColor(isDanger ? nvgRGB(239, 68, 68) : brls::Application::getTheme()["brls/text"]);
            lbl->setGrow(1.0f);
            row->addView(lbl);

            row->registerClickAction([d, cb](brls::View*) {
                d->close([cb]() {
                    cb();
                });
                return true;
            });

            menuBox->addView(row);
        };

        if (isMulti) {
            addActionRow("hints/unselect_all"_i18n, ActionIconType::UNSELECT, nvgRGB(156, 163, 175), [this]() {
                m_selectedPaths.clear();
                refreshList();
            });
        }

        // Copiar
        std::string copyLabel = isMulti ? ("hints/copy"_i18n + " (" + std::to_string(m_selectedPaths.size()) + ")") : "hints/copy"_i18n;
        addActionRow(copyLabel, ActionIconType::COPY, nvgRGB(59, 130, 246), [this, fullPath, isMulti]() {
            g_clipboardPaths.clear();
            if (isMulti) {
                for (const auto& p : m_selectedPaths) g_clipboardPaths.push_back(p);
            } else {
                g_clipboardPaths.push_back(fullPath);
            }
            g_clipboardIsCut = false;
            m_selectedPaths.clear();
            refreshList();
            brls::Application::notify("hints/copied_notify"_i18n);
        });

        // Cortar
        std::string cutLabel = isMulti ? ("hints/cut"_i18n + " (" + std::to_string(m_selectedPaths.size()) + ")") : "hints/cut"_i18n;
        addActionRow(cutLabel, ActionIconType::CUT, nvgRGB(245, 158, 11), [this, fullPath, isMulti]() {
            g_clipboardPaths.clear();
            if (isMulti) {
                for (const auto& p : m_selectedPaths) g_clipboardPaths.push_back(p);
            } else {
                g_clipboardPaths.push_back(fullPath);
            }
            g_clipboardIsCut = true;
            m_selectedPaths.clear();
            refreshList();
            brls::Application::notify("hints/cut_notify"_i18n);
        });

        // Pegar aquí (si hay elementos en el portapapeles)
        if (!g_clipboardPaths.empty()) {
            std::string pasteLabel = (g_clipboardPaths.size() > 1) ?
                ("hints/paste_here"_i18n + " (" + std::to_string(g_clipboardPaths.size()) + ")") :
                "hints/paste_here"_i18n;
            addActionRow(pasteLabel, ActionIconType::PASTE, nvgRGB(16, 185, 129), [this]() {
                pasteClipboard();
            });
        }

        // Renombrar (solo si es elemento único)
        if (!isMulti) {
            addActionRow("hints/rename"_i18n, ActionIconType::RENAME, nvgRGB(139, 92, 246), [this, fullPath, name]() {
                brls::sync([this, fullPath, name]() {
                    std::string newName = showHorizonKeyboard("hints/rename_title"_i18n, name);
                    if (!newName.empty() && newName != name) {
                        std::string newPath = currentPath + (currentPath.back() == '/' ? "" : "/") + newName;
                        rename(fullPath.c_str(), newPath.c_str());
                        refreshList();
                    }
                });
            });
        }

        // Eliminar
        std::string delLabel = isMulti ? ("hints/delete"_i18n + " (" + std::to_string(m_selectedPaths.size()) + ")") : "hints/delete"_i18n;
        addActionRow(delLabel, ActionIconType::DELETE, nvgRGB(239, 68, 68), [this, fullPath, name, isDir, isMulti]() {
            brls::sync([this, fullPath, name, isDir, isMulti]() {
                std::string q = isMulti ? ("hints/delete_multi_confirm"_i18n)
                                        : ("hints/delete_confirm"_i18n + " '" + name + "'?");
                brls::Dialog* confirm = new brls::Dialog(q);
                confirm->addButton("hints/cancel"_i18n, []() {});
                confirm->addButton("hints/delete"_i18n, [this, fullPath, isDir, isMulti]() {
                    if (isMulti) {
                        for (const auto& p : m_selectedPaths) {
                            struct stat st;
                            if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) mtp_ops::mtpDelRec(p);
                            else unlink(p.c_str());
                        }
                        m_selectedPaths.clear();
                    } else {
                        if (isDir) mtp_ops::mtpDelRec(fullPath);
                        else unlink(fullPath.c_str());
                    }
                    refreshList();
                    brls::Application::notify("hints/op_finished"_i18n);
                });
                confirm->open();
            });
        }, true);

        // Propiedades (solo si es elemento único)
        if (!isMulti) {
            addActionRow("hints/properties"_i18n, ActionIconType::PROPERTIES, nvgRGB(6, 182, 212), [this, fullPath, name, isDir, sz, mtime]() {
                brls::sync([this, fullPath, name, isDir, sz, mtime]() {
                    showFileProperties(fullPath, name, isDir, sz, mtime);
                });
            });
        }

        addActionRow("hints/cancel"_i18n, ActionIconType::CANCEL, nvgRGB(156, 163, 175), [d]() {
            d->close();
        });

        d->setCancelable(true);
        d->open();
    }

private:
    std::string currentPath;
    std::set<std::string> m_selectedPaths;
    int m_totalItems = 0;
};

// --- Fila visual de Juego Instalado (Captura media_1790379533583.png) ---
class GameRowCell : public brls::Box {
public:
    GameRowCell(const std::string& name, const std::string& version, const std::string& author,
                u64 titleId, u64 sizeBytes, u64 nandBytes, u64 sdBytes, const std::vector<u8>& iconBytes, bool isSd) {
        this->setFocusable(true);
        this->setHeight(78);
        this->setWidthPercentage(100.0f);
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(0, 14, 0, 14);
        this->setCornerRadius(4);

        // Icono cuadrado del juego (60x60) con esquinas redondeadas
        brls::Image* img = new brls::Image();
        if (!iconBytes.empty()) {
            img->setImageFromMem(iconBytes.data(), iconBytes.size());
        } else {
            bool isLight = (brls::Application::getPlatform()->getThemeVariant() == brls::ThemeVariant::LIGHT);
            img->setImageFromFile(isLight ? "romfs:/img/icon_nsp_sm_dark.png" : "romfs:/img/icon_nsp_sm.png");
        }
        img->setWidth(60);
        img->setHeight(60);
        img->setCornerRadius(8);
        img->setMarginRight(16);
        this->addView(img);

        // Columna vertical central: Nombre + Subtítulos
        brls::Box* col = new brls::Box(brls::Axis::COLUMN);
        col->setGrow(1.0f);
        col->setJustifyContent(brls::JustifyContent::CENTER);

        brls::Label* lblName = new brls::Label();
        lblName->setText(name);
        lblName->setFontSize(16);
        lblName->setTextColor(brls::Application::getTheme()["brls/text"]);
        col->addView(lblName);

        brls::Label* lblSub = new brls::Label();
        lblSub->setText("hints/last_used"_i18n);
        lblSub->setFontSize(12);
        lblSub->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        lblSub->setMarginTop(2);
        col->addView(lblSub);

        // Fila de almacenamiento [Consola] ---  [microSD] 15.7 GB
        brls::Box* storRow = new brls::Box(brls::Axis::ROW);
        storRow->setAlignItems(brls::AlignItems::CENTER);
        storRow->setMarginTop(3);

        auto formatSizeStr = [](u64 bytes, char* out, size_t outSz) {
            if (bytes == 0) {
                snprintf(out, outSz, "---");
            } else if (bytes < 1024 * 1024) {
                snprintf(out, outSz, "%.1f KB", bytes / 1024.0);
            } else if (bytes < 1024ULL * 1024 * 1024) {
                snprintf(out, outSz, "%.1f MB", bytes / (1024.0 * 1024.0));
            } else {
                snprintf(out, outSz, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
            }
        };

        char szNand[32];
        formatSizeStr(nandBytes, szNand, sizeof(szNand));
        char szSd[32];
        formatSizeStr(sdBytes, szSd, sizeof(szSd));

        brls::Label* lblNand = new brls::Label();
        lblNand->setText("[Consola] " + std::string(szNand));
        lblNand->setFontSize(11);
        lblNand->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        lblNand->setMarginRight(15);
        storRow->addView(lblNand);

        brls::Label* lblSd = new brls::Label();
        lblSd->setText("[microSD] " + std::string(szSd));
        lblSd->setFontSize(11);
        lblSd->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        storRow->addView(lblSd);

        col->addView(storRow);
        this->addView(col);

        // Lado derecho: Tamaño total en cian brillante estilo Switch nativo
        char szTotal[32];
        if (sizeBytes == 0) {
            snprintf(szTotal, sizeof(szTotal), "0.0 MB");
        } else if (sizeBytes < 1024ULL * 1024 * 1024) {
            snprintf(szTotal, sizeof(szTotal), "%.1f MB", sizeBytes / (1024.0 * 1024.0));
        } else {
            snprintf(szTotal, sizeof(szTotal), "%.1f GB", sizeBytes / (1024.0 * 1024.0 * 1024.0));
        }

        brls::Label* lblSize = new brls::Label();
        lblSize->setText(szTotal);
        lblSize->setFontSize(22);
        lblSize->setTextColor(nvgRGB(0, 230, 168)); // Cian / Verde Horizon
        lblSize->setMarginLeft(10);
        this->addView(lblSize);
    }
};

// --- Tab 2: Gestión de Datos y Programas Instalados (Directo dos columnas) ---
class GamesTab : public brls::Box {
public:
    GamesTab() {
        brls::Logger::info("GamesTab: Iniciando constructor");
        m_alive = std::make_shared<std::atomic<bool>>(true);
        try {
            brls::Logger::info("GamesTab: inflando XML");
            this->inflateFromXMLFile("romfs:/xml/view_games.xml");
            
            brls::Logger::info("GamesTab: actualizando gauges");
            updateStorageGauges();

            brls::Logger::info("GamesTab: registrando accion B");
            this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
                brls::Application::popActivity();
                return true;
            });

            brls::Logger::info("GamesTab: buscando boxGamesList");
            brls::Box* boxGamesList = dynamic_cast<brls::Box*>(this->getView("boxGamesList"));
            if (boxGamesList) {
                boxGamesList->clearViews();
                brls::Box* loadingBox = new brls::Box();
                loadingBox->setFocusable(true);
                loadingBox->setHideHighlightBackground(true);
                loadingBox->setHideHighlightBorder(true);
                loadingBox->setWidthPercentage(100.0f);
                loadingBox->setHeight(50);

                brls::Label* lblLoading = new brls::Label();
                lblLoading->setText("Cargando títulos instalados...");
                lblLoading->setFontSize(16);
                lblLoading->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
                lblLoading->setMarginTop(30);
                loadingBox->addView(lblLoading);
                boxGamesList->addView(loadingBox);
            }
            brls::Logger::info("GamesTab: constructor finalizado con exito");
        } catch (const std::exception& e) {
            brls::Logger::error("Excepcion en GamesTab constructor: {}", e.what());
        } catch (...) {
            brls::Logger::error("Excepcion desconocida en GamesTab constructor");
        }
    }

    void willAppear(bool resetState = false) override {
        brls::Box::willAppear(resetState);
        brls::Logger::info("GamesTab: willAppear llamado (m_loaded={})", m_loaded);

        if (!m_loaded) {
            m_loaded = true;
#ifdef __SWITCH__
            brls::Logger::info("GamesTab: comprobando modo applet en willAppear");
            if (appletGetAppletType() != AppletType_Application) {
                brls::Logger::info("GamesTab: modo applet detectado, llamando setupAppletWarning");
                setupAppletWarning();
                return;
            }
#endif
            brls::Logger::info("GamesTab: llamando loadGamesAsync en willAppear");
            loadGamesAsync();
        }
        brls::Logger::info("GamesTab: willAppear finalizado exitosamente");
    }

    ~GamesTab() override {
        brls::Logger::info("GamesTab: Destructor llamado");
        if (m_alive) {
            *m_alive = false;
        }
    }

    void setupAppletWarning() {
        brls::Logger::info("GamesTab: Entrando a setupAppletWarning");
        brls::Box* boxGamesList = dynamic_cast<brls::Box*>(this->getView("boxGamesList"));
        if (!boxGamesList) return;
        boxGamesList->clearViews();

        brls::Label* errTitle = new brls::Label();
        errTitle->setText("Modo Álbum (Memoria restringida)");
        errTitle->setFontSize(20);
        errTitle->setTextColor(nvgRGB(245, 158, 11));
        errTitle->setMarginBottom(12);
        boxGamesList->addView(errTitle);

        brls::Label* errDesc = new brls::Label();
        errDesc->setText("El acceso a la lista de títulos instalados está bloqueado en modo Álbum por las protecciones de memoria de la consola.\n\nPara gestionar tus juegos con acceso completo:\nInicia FileZzz mediante Title Takeover (mantén presionado el botón R al iniciar cualquier juego instalado en el menú principal).");
        errDesc->setFontSize(14);
        errDesc->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        errDesc->setMarginBottom(24);
        boxGamesList->addView(errDesc);

        brls::Box* btnBack = new brls::Box();
        btnBack->setFocusable(true);
        btnBack->setWidth(280);
        btnBack->setHeight(46);
        btnBack->setCornerRadius(6);
        btnBack->setBackgroundColor(nvgRGB(2, 132, 199));
        btnBack->setJustifyContent(brls::JustifyContent::CENTER);
        btnBack->setAlignItems(brls::AlignItems::CENTER);

        brls::Label* lblBack = new brls::Label();
        lblBack->setText("Volver al Menú Principal (B)");
        lblBack->setFontSize(15);
        lblBack->setTextColor(nvgRGB(255, 255, 255));
        btnBack->addView(lblBack);

        btnBack->registerClickAction([](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
        boxGamesList->addView(btnBack);
        brls::Application::giveFocus(btnBack);
        brls::Logger::info("GamesTab: Saliendo de setupAppletWarning");
    }

    struct LoadedGameInfo {
        std::string titleName;
        std::string version;
        std::string author;
        u64 titleId = 0;
        u64 gameSize = 0;
        u64 nandSize = 0;
        u64 sdSize = 0;
        std::vector<u8> iconBytes;
        bool isSd = true;
    };

    void updateStorageGauges() {
        try {
            brls::Logger::info("GamesTab: Entrando a updateStorageGauges");
            brls::Box* barNandFill = dynamic_cast<brls::Box*>(this->getView("barNandFill"));
            brls::Label* lblNandFree = dynamic_cast<brls::Label*>(this->getView("lblNandFree"));
            brls::Box* barSdFill = dynamic_cast<brls::Box*>(this->getView("barSdFill"));
            brls::Label* lblSdFree = dynamic_cast<brls::Label*>(this->getView("lblSdFree"));

            u64 nandFree = 0, nandTotal = 0;
            brls::Logger::info("GamesTab: Llamando getNandStorageInfo");
            if (getNandStorageInfo(nandFree, nandTotal) && nandTotal > 0) {
                float nandUsedPct = (float)(nandTotal - nandFree) / (float)nandTotal;
                if (nandUsedPct < 0.0f) nandUsedPct = 0.0f;
                if (nandUsedPct > 1.0f) nandUsedPct = 1.0f;
                if (barNandFill) barNandFill->setWidth((int)(320.0f * nandUsedPct));
                if (lblNandFree) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.1f GB", nandFree / (1024.0 * 1024.0 * 1024.0));
                    lblNandFree->setText(buf);
                }
            } else {
                if (lblNandFree) lblNandFree->setText("---");
            }

            u64 sdFree = 0, sdTotal = 0;
            brls::Logger::info("GamesTab: Llamando getStorageInfo sdmc:/");
            bool hasSd = getStorageInfo("sdmc:/", sdFree, sdTotal);
            brls::Logger::info("GamesTab: getStorageInfo retorno {}", hasSd);
            if (hasSd && sdTotal > 0) {
                // guarda de underflow: free nunca puede ser mayor que total
                u64 sdUsed = (sdFree <= sdTotal) ? (sdTotal - sdFree) : 0;
                float sdUsedPct = (float)sdUsed / (float)sdTotal;
                // clamp a [0.0, 1.0] por si acaso
                if (sdUsedPct < 0.0f) sdUsedPct = 0.0f;
                if (sdUsedPct > 1.0f) sdUsedPct = 1.0f;
                brls::Logger::info("GamesTab: SD used={} pct={:.2f}", sdUsed, sdUsedPct);
                if (barSdFill) {
                    brls::Logger::info("GamesTab: setWidth barSdFill");
                    barSdFill->setWidth((int)(320.0f * sdUsedPct));
                }
                if (lblSdFree) {
                    brls::Logger::info("GamesTab: setText lblSdFree");
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.1f GB", sdFree / (1024.0 * 1024.0 * 1024.0));
                    lblSdFree->setText(buf);
                }
            }
            brls::Logger::info("GamesTab: Saliendo de updateStorageGauges");
        } catch (...) {
            brls::Logger::error("GamesTab: Excepción en updateStorageGauges");
        }
    }

    struct GamesThreadContext {
        GamesTab* self;
        std::shared_ptr<std::atomic<bool>> alive;
    };

    void loadGamesAsync() {
        brls::Logger::info("GamesTab: Entrando a loadGamesAsync");
        brls::Box* boxGamesList = dynamic_cast<brls::Box*>(this->getView("boxGamesList"));
        if (!boxGamesList) {
            brls::Logger::info("GamesTab: loadGamesAsync abortado, no boxGamesList");
            return;
        }

        brls::Logger::info("GamesTab: Configurando hilo pthread con 256KB de stack");
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 256 * 1024);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        auto* ctx = new GamesThreadContext{this, m_alive};
        int prc = pthread_create(&th, &attr, [](void* arg) -> void* {
            auto* ctx = static_cast<GamesThreadContext*>(arg);
            GamesTab* self = ctx->self;
            auto alive = ctx->alive;
            delete ctx;

            brls::Logger::info("GamesTab-Thread: Hilo pthread iniciado con exito (Stack 256KB)!");
            try {
                if (!*alive) {
                    brls::Logger::info("GamesTab-Thread: alive es false al iniciar hilo");
                    return nullptr;
                }

#ifdef __SWITCH__
                brls::Logger::info("GamesTab-Thread: llamando a nsInitialize()");
                Result rc = nsInitialize();
                char resBuf[32];
                snprintf(resBuf, sizeof(resBuf), "0x%X", (unsigned int)rc);
                brls::Logger::info("GamesTab-Thread: nsInitialize() retorno {}", resBuf);
                if (R_FAILED(rc)) {
                    brls::Logger::error("GamesTab-Thread: nsInitialize fallo");
                    if (!*alive) return nullptr;
                    brls::sync([self, alive]() {
                        if (!*alive) return;
                        brls::Box* boxGamesList = dynamic_cast<brls::Box*>(self->getView("boxGamesList"));
                        if (boxGamesList) {
                            boxGamesList->clearViews();
                            brls::Label* err = new brls::Label();
                            err->setText("No se pudo conectar al servicio de gestión de títulos.");
                            err->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
                            err->setMarginBottom(16);
                            boxGamesList->addView(err);

                            brls::Box* btnBack = new brls::Box();
                            btnBack->setFocusable(true);
                            btnBack->setWidth(280);
                            btnBack->setHeight(46);
                            btnBack->setCornerRadius(6);
                            btnBack->setBackgroundColor(nvgRGB(2, 132, 199));
                            btnBack->setJustifyContent(brls::JustifyContent::CENTER);
                            btnBack->setAlignItems(brls::AlignItems::CENTER);
                            brls::Label* lblBack = new brls::Label();
                            lblBack->setText("Volver al Menú Principal (B)");
                            lblBack->setFontSize(15);
                            lblBack->setTextColor(nvgRGB(255, 255, 255));
                            btnBack->addView(lblBack);
                            btnBack->registerClickAction([](brls::View*) {
                                brls::Application::popActivity();
                                return true;
                            });
                            boxGamesList->addView(btnBack);
                            brls::Application::giveFocus(btnBack);
                        }
                    });
                    return nullptr;
                }

                brls::Logger::info("GamesTab-Thread: llamando a nsListApplicationRecord");
                std::vector<NsApplicationRecord> records(128);
                s32 entryCount = 0;
                rc = nsListApplicationRecord(records.data(), 128, 0, &entryCount);
                snprintf(resBuf, sizeof(resBuf), "0x%X", (unsigned int)rc);
                brls::Logger::info("GamesTab-Thread: nsListApplicationRecord retorno {}, count={}", resBuf, entryCount);
                if (R_FAILED(rc) || entryCount <= 0) {
                    brls::Logger::warning("GamesTab-Thread: sin titulos o fallo nsListApplicationRecord");
                    nsExit();
                    if (!*alive) return nullptr;
                    brls::sync([self, alive]() {
                        if (!*alive) return;
                        brls::Box* boxGamesList = dynamic_cast<brls::Box*>(self->getView("boxGamesList"));
                        if (boxGamesList) {
                            boxGamesList->clearViews();
                            brls::Label* emptyLbl = new brls::Label();
                            emptyLbl->setText("No se encontraron juegos o programas instalados.");
                            emptyLbl->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
                            emptyLbl->setMarginBottom(16);
                            boxGamesList->addView(emptyLbl);

                            brls::Box* btnBack = new brls::Box();
                            btnBack->setFocusable(true);
                            btnBack->setWidth(280);
                            btnBack->setHeight(46);
                            btnBack->setCornerRadius(6);
                            btnBack->setBackgroundColor(nvgRGB(2, 132, 199));
                            btnBack->setJustifyContent(brls::JustifyContent::CENTER);
                            btnBack->setAlignItems(brls::AlignItems::CENTER);
                            brls::Label* lblBack = new brls::Label();
                            lblBack->setText("Volver al Menú Principal (B)");
                            lblBack->setFontSize(15);
                            lblBack->setTextColor(nvgRGB(255, 255, 255));
                            btnBack->addView(lblBack);
                            btnBack->registerClickAction([](brls::View*) {
                                brls::Application::popActivity();
                                return true;
                            });
                            boxGamesList->addView(btnBack);
                            brls::Application::giveFocus(btnBack);
                        }
                    });
                    return nullptr;
                }

                std::vector<LoadedGameInfo> games;
                brls::Logger::info("GamesTab-Thread: reservando controlData con memalign 4KB");
                NsApplicationControlData* controlData = (NsApplicationControlData*)memalign(0x1000, sizeof(NsApplicationControlData));
                if (!controlData) {
                    controlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
                }
                char ptrBuf[32];
                snprintf(ptrBuf, sizeof(ptrBuf), "%p", (void*)controlData);
                brls::Logger::info("GamesTab-Thread: controlData reservado en puntero {}", ptrBuf);

                for (s32 i = 0; i < entryCount; i++) {
                    if (!*alive) {
                        brls::Logger::info("GamesTab-Thread: alive cancelado durante bucle");
                        break;
                    }
                    u64 titleId = records[i].application_id;
                    if (titleId == 0) continue;

                    char tidStr[32];
                    snprintf(tidStr, sizeof(tidStr), "0x%016llX", (unsigned long long)titleId);
                    brls::Logger::info("GamesTab-Thread: [{}/{}] Procesando titleId={}", i + 1, entryCount, tidStr);

                    LoadedGameInfo g;
                    g.titleId = titleId;

                    if (controlData) {
                        memset(controlData, 0, sizeof(NsApplicationControlData));
                        u64 actualSize = 0;
                        brls::Logger::info("GamesTab-Thread: [{}/{}] Llamando nsGetApplicationControlData...", i + 1, entryCount);
                        Result crc = nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, controlData, sizeof(NsApplicationControlData), &actualSize);
                        char crcStr[32];
                        snprintf(crcStr, sizeof(crcStr), "0x%X", (unsigned int)crc);
                        brls::Logger::info("GamesTab-Thread: [{}/{}] nsGetApplicationControlData retorno {}, actualSize={}", i + 1, entryCount, crcStr, actualSize);
                        if (R_SUCCEEDED(crc)) {
                            NacpLanguageEntry* langEntry = nullptr;
                            if (R_SUCCEEDED(nacpGetLanguageEntry(&controlData->nacp, &langEntry)) && langEntry) {
                                if (langEntry->name[0] != '\0') {
                                    g.titleName = langEntry->name;
                                    g.author = langEntry->author;
                                }
                            }
                            g.version = controlData->nacp.display_version;
                            if (actualSize > sizeof(controlData->nacp)) {
                                size_t iconSz = (size_t)(actualSize - sizeof(controlData->nacp));
                                if (iconSz > 0x20000) iconSz = 0x20000;
                                if (iconSz > 0) {
                                    g.iconBytes.assign((u8*)controlData->icon, (u8*)controlData->icon + iconSz);
                                }
                            }
                        }
                    }

                    if (g.titleName.empty()) {
                        char tb[64];
                        snprintf(tb, sizeof(tb), "Título [%016llX]", (unsigned long long)titleId);
                        g.titleName = tb;
                    }

                    if (g.titleName.rfind("Autorun", 0) == 0 || g.titleName.rfind("autorun", 0) == 0) {
                        g.titleName = "App - " + g.titleName;
                    }

                    alignas(16) NsApplicationOccupiedSize occSize = {};
                    memset(&occSize, 0, sizeof(occSize));
                    brls::Logger::info("GamesTab-Thread: [{}/{}] Llamando nsCalculateApplicationOccupiedSize...", i + 1, entryCount);
                    Result occRc = nsCalculateApplicationOccupiedSize(titleId, &occSize);
                    char occStr[32];
                    snprintf(occStr, sizeof(occStr), "0x%X", (unsigned int)occRc);
                    brls::Logger::info("GamesTab-Thread: [{}/{}] nsCalculateApplicationOccupiedSize retorno {}", i + 1, entryCount, occStr);
                    if (R_SUCCEEDED(occRc)) {
                        struct AppOccupiedEntity {
                            u8 storageId;
                            u8 pad[7];
                            u64 appSize;
                            u64 patchSize;
                            u64 aocSize;
                        };
                        const auto* entities = reinterpret_cast<const AppOccupiedEntity*>(occSize.unk_x0);
                        u64 nandTotal = 0;
                        u64 sdTotal = 0;
                        u64 grandTotal = 0;
                        for (int e = 0; e < 4; ++e) {
                            u64 entSum = entities[e].appSize + entities[e].patchSize + entities[e].aocSize;
                            grandTotal += entSum;
                            if (entities[e].storageId == NcmStorageId_SdCard) {
                                sdTotal += entSum;
                            } else if (entities[e].storageId == NcmStorageId_BuiltInUser || entities[e].storageId == NcmStorageId_BuiltInSystem) {
                                nandTotal += entSum;
                            }
                        }
                        g.gameSize = grandTotal;
                        g.nandSize = nandTotal;
                        g.sdSize = sdTotal;
                        g.isSd = (sdTotal >= nandTotal);
                    }

                    char szBuf[32];
                    snprintf(szBuf, sizeof(szBuf), "%llu", (unsigned long long)g.gameSize);
                    brls::Logger::info("GamesTab-Thread: [{}/{}] Finalizado '{}' (total: {} bytes, nand: {} bytes, sd: {} bytes, icon: {} bytes)",
                        i + 1, entryCount, g.titleName, szBuf, (unsigned long long)g.nandSize, (unsigned long long)g.sdSize, g.iconBytes.size());
                    games.push_back(std::move(g));
                }

                if (controlData) {
                    free(controlData);
                }
                nsExit();
                brls::Logger::info("GamesTab-Thread: nsExit completado, total juegos: {}", games.size());

                if (!*alive) return nullptr;
                brls::sync([self, alive, loaded = std::move(games)]() {
                    if (!*alive) return;
                    brls::Logger::info("GamesTab-UI: actualizando interfaz con {} juegos", loaded.size());
                    brls::Box* boxGamesList = dynamic_cast<brls::Box*>(self->getView("boxGamesList"));
                    if (!boxGamesList) return;
                    boxGamesList->clearViews();

                    for (const auto& g : loaded) {
                        GameRowCell* cell = new GameRowCell(g.titleName, g.version, g.author, g.titleId, g.gameSize, g.nandSize, g.sdSize, g.iconBytes, g.isSd);
                        std::string tn = g.titleName;
                        std::string ver = g.version;
                        std::string auth = g.author;
                        u64 tid = g.titleId;
                        u64 sz = g.gameSize;
                        u64 nsz = g.nandSize;
                        u64 sdsz = g.sdSize;
                        std::vector<u8> ic = g.iconBytes;
                        bool isSd = g.isSd;
                        cell->registerClickAction([self, tn, ver, auth, tid, sz, nsz, sdsz, ic, isSd](brls::View*) {
                            self->showGameDetails(tn, ver, auth, tid, sz, nsz, sdsz, ic, isSd);
                            return true;
                        });
                        boxGamesList->addView(cell);

                        brls::Box* sep = new brls::Box();
                        sep->setWidthPercentage(100.0f);
                        sep->setHeight(1);
                        sep->setBackgroundColor(brls::Application::getTheme()["brls/sidebar/separator"]);
                        boxGamesList->addView(sep);
                    }

                    if (!boxGamesList->getChildren().empty()) {
                        brls::Application::giveFocus(boxGamesList->getChildren()[0]);
                    }
                    brls::Logger::info("GamesTab-UI: interfaz de juegos renderizada");
                });
#endif
            } catch (const std::exception& e) {
                brls::Logger::error("Excepcion en hilo de GamesTab: {}", e.what());
            } catch (...) {
                brls::Logger::error("Excepcion desconocida en hilo de GamesTab");
            }
            return nullptr;
        }, ctx);

        pthread_attr_destroy(&attr);
        if (prc == 0) {
            brls::Logger::info("GamesTab: hilo pthread lanzado exitosamente");
        } else {
            brls::Logger::error("GamesTab: error al crear pthread ({})", prc);
        }
    }

    void showGameDetails(const std::string& name, const std::string& version, const std::string& author,
                         u64 titleId, u64 sizeBytes, u64 nandBytes, u64 sdBytes, const std::vector<u8>& iconBytes, bool isSd) {
        brls::Box* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(680);
        card->setPadding(16, 20, 16, 20);

        brls::Box* topRow = new brls::Box(brls::Axis::ROW);
        topRow->setAlignItems(brls::AlignItems::CENTER);
        topRow->setMarginBottom(14);

        brls::Image* img = new brls::Image();
        if (!iconBytes.empty()) {
            img->setImageFromMem(iconBytes.data(), iconBytes.size());
        } else {
            bool isLight = (brls::Application::getPlatform()->getThemeVariant() == brls::ThemeVariant::LIGHT);
            img->setImageFromFile(isLight ? "romfs:/img/icon_nsp_sm_dark.png" : "romfs:/img/icon_nsp_sm.png");
        }
        img->setWidth(90);
        img->setHeight(90);
        img->setCornerRadius(14);
        img->setMarginRight(18);
        topRow->addView(img);

        brls::Box* topCol = new brls::Box(brls::Axis::COLUMN);
        topCol->setGrow(1.0f);

        brls::Label* lblName = new brls::Label();
        lblName->setText(name);
        lblName->setFontSize(20);
        lblName->setTextColor(brls::Application::getTheme()["brls/text"]);
        topCol->addView(lblName);

        brls::Label* lblAuth = new brls::Label();
        lblAuth->setText(author.empty() ? "Desarrollador desconocido" : author);
        lblAuth->setFontSize(14);
        lblAuth->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        lblAuth->setMarginTop(4);
        topCol->addView(lblAuth);

        topRow->addView(topCol);
        card->addView(topRow);

        brls::Box* sep = new brls::Box();
        sep->setWidthPercentage(100.0f);
        sep->setHeight(1);
        sep->setBackgroundColor(brls::Application::getTheme()["brls/sidebar/separator"]);
        sep->setMarginBottom(12);
        card->addView(sep);

        auto addDetailRow = [&](const std::string& key, const std::string& val) {
            brls::Box* r = new brls::Box(brls::Axis::ROW);
            r->setWidthPercentage(100.0f);
            r->setHeight(36);
            r->setAlignItems(brls::AlignItems::CENTER);
            r->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

            brls::Label* lKey = new brls::Label();
            lKey->setText(key);
            lKey->setFontSize(15);
            lKey->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
            r->addView(lKey);

            brls::Label* lVal = new brls::Label();
            lVal->setText(val);
            lVal->setFontSize(15);
            lVal->setTextColor(brls::Application::getTheme()["brls/text"]);
            r->addView(lVal);

            card->addView(r);
        };

        char idBuf[32];
        snprintf(idBuf, sizeof(idBuf), "%016llX", (unsigned long long)titleId);
        addDetailRow("Title ID", idBuf);
        addDetailRow("Versión", version.empty() ? "1.0.0" : version);

        char szBuf[64];
        if (sizeBytes == 0) snprintf(szBuf, sizeof(szBuf), "0.0 MB");
        else if (sizeBytes < 1024*1024) snprintf(szBuf, sizeof(szBuf), "%.2f KB", sizeBytes / 1024.0);
        else if (sizeBytes < 1024ULL*1024*1024) snprintf(szBuf, sizeof(szBuf), "%.2f MB", sizeBytes / (1024.0*1024.0));
        else snprintf(szBuf, sizeof(szBuf), "%.2f GB", sizeBytes / (1024.0*1024.0*1024.0));
        addDetailRow("hints/prop_size"_i18n, szBuf);

        char szNandBuf[64];
        if (nandBytes == 0) snprintf(szNandBuf, sizeof(szNandBuf), "---");
        else if (nandBytes < 1024ULL*1024*1024) snprintf(szNandBuf, sizeof(szNandBuf), "%.2f MB", nandBytes / (1024.0*1024.0));
        else snprintf(szNandBuf, sizeof(szNandBuf), "%.2f GB", nandBytes / (1024.0*1024.0*1024.0));
        addDetailRow("hints/nand_storage"_i18n, szNandBuf);

        char szSdBuf[64];
        if (sdBytes == 0) snprintf(szSdBuf, sizeof(szSdBuf), "---");
        else if (sdBytes < 1024ULL*1024*1024) snprintf(szSdBuf, sizeof(szSdBuf), "%.2f MB", sdBytes / (1024.0*1024.0));
        else snprintf(szSdBuf, sizeof(szSdBuf), "%.2f GB", sdBytes / (1024.0*1024.0*1024.0));
        addDetailRow("hints/sd_storage"_i18n, szSdBuf);
        addDetailRow("hints/prop_path"_i18n, isSd ? "hints/sd_storage"_i18n : "hints/nand_storage"_i18n);

        brls::Dialog* d = new brls::Dialog(card);
        d->addButton("hints/ok"_i18n, []() {});
        d->open();
    }

    static brls::View* create() {
        return new GamesTab();
    }

private:
    std::shared_ptr<std::atomic<bool>> m_alive;
    bool m_loaded = false;
};

// --- Tab 3: USB MTP Responder ---
class MtpTab : public brls::Box {
public:
    MtpTab() {
        this->inflateFromXMLFile("romfs:/xml/view_mtp.xml");

        btnToggleMtp = dynamic_cast<brls::Box*>(this->getView("btnToggleMtp"));
        lblMtpBtnText = dynamic_cast<brls::Label*>(this->getView("lblMtpBtnText"));
        lblMtpStatus = dynamic_cast<brls::Label*>(this->getView("lblMtpStatus"));
        lblMtpFilename = dynamic_cast<brls::Label*>(this->getView("lblMtpFilename"));
        boxMtpProgressFill = dynamic_cast<brls::Box*>(this->getView("boxMtpProgressFill"));
        lblMtpStats = dynamic_cast<brls::Label*>(this->getView("lblMtpStats"));
        lblLogContent = dynamic_cast<brls::Label*>(this->getView("lblLogContent"));

        if (btnToggleMtp) {
            btnToggleMtp->registerClickAction([this](brls::View*) {
                if (mtp_ops::running()) {
                    mtp_ops::stop();
                    mtp_usb::teardown();
                    updateUIState(false);
                } else {
                    if (mtp_usb::setup()) {
                        mtp_ops::start();
                        updateUIState(true);
                    }
                }
                return true;
            });
        }

        // Botón B para volver al Menú Principal
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });

        updateUIState(mtp_ops::running());

        // Actualización de telemetría y bitácora periódica (4 Hz)
        updateTimer.setCallback([this]() {
            updateTelemetry();
        });
        updateTimer.start(250);
    }

    ~MtpTab() {
        updateTimer.stop();
    }

    static brls::View* create() {
        return new MtpTab();
    }

    void updateUIState(bool running) {
        if (lblMtpStatus) {
            if (running) {
                lblMtpStatus->setText("hints/mtp_status_active"_i18n);
                lblMtpStatus->setTextColor(nvgRGB(16, 185, 129));
            } else {
                lblMtpStatus->setText("hints/mtp_status_idle"_i18n);
                lblMtpStatus->setTextColor(nvgRGB(156, 163, 175));
            }
        }
        if (lblMtpBtnText) {
            lblMtpBtnText->setText(running ? "hints/mtp_btn_stop"_i18n : "hints/mtp_btn_start"_i18n);
            lblMtpBtnText->setTextColor(nvgRGB(255, 255, 255));
        }
        if (btnToggleMtp) {
            btnToggleMtp->setBackgroundColor(running ? nvgRGB(239, 68, 68) : nvgRGB(2, 132, 199));
        }
    }

    void updateTelemetry() {
        bool running = mtp_ops::running();
        updateUIState(running);

        if (mtp_ops::g_telemetry.active) {
            float pct = (mtp_ops::g_telemetry.totalBytes > 0 && mtp_ops::g_telemetry.totalBytes != 0xFFFFFFFFFFFFFFFFULL) ?
                (float)((double)mtp_ops::g_telemetry.transferredBytes / (double)mtp_ops::g_telemetry.totalBytes * 100.0) : 0.0f;
            if (pct > 100.0f) pct = 100.0f;

            if (lblMtpFilename) {
                lblMtpFilename->setText(mtp_ops::g_telemetry.filename);
                lblMtpFilename->setTextColor(nvgRGB(56, 189, 248));
            }
            if (boxMtpProgressFill) {
                boxMtpProgressFill->setWidthPercentage(pct);
            }
            if (lblMtpStats) {
                char sBuf[256];
                snprintf(sBuf, sizeof(sBuf), "%.1f MB / %.1f MB (%.1f%%) · Velocidad: %.1f MB/s · %s",
                    mtp_ops::g_telemetry.transferredBytes / (1024.0 * 1024.0),
                    mtp_ops::g_telemetry.totalBytes / (1024.0 * 1024.0),
                    pct, (float)mtp_ops::g_telemetry.speedMBs,
                    mtp_ops::g_telemetry.isUpload ? "Recibiendo" : "Enviando");
                lblMtpStats->setText(sBuf);
            }
        } else {
            if (lblMtpFilename) {
                lblMtpFilename->setText(running ? "Servidor MTP listo para recibir archivos" : "En espera de conexión USB...");
                lblMtpFilename->setTextColor(brls::Application::getTheme()["brls/text"]);
            }
            if (boxMtpProgressFill) {
                boxMtpProgressFill->setWidthPercentage(0.0f);
            }
            if (lblMtpStats) {
                lblMtpStats->setText(running ? "Conexión USB activa. Puedes transferir juegos, respaldos o capturas." :
                                               "Conecta el cable USB al PC o teléfono para transferir archivos.");
            }
        }

        if (lblLogContent) {
            auto logs = mtp_usb::getLogs();
            std::string fullLog = "";
            size_t startIdx = logs.size() > 10 ? logs.size() - 10 : 0;
            for (size_t i = startIdx; i < logs.size(); i++) {
                if (!fullLog.empty()) fullLog += "\n";
                fullLog += logs[i];
            }
            lblLogContent->setText(fullLog);
        }
    }

private:
    brls::RepeatingTimer updateTimer;
    brls::Box* btnToggleMtp = nullptr;
    brls::Label* lblMtpBtnText = nullptr;
    brls::Label* lblMtpStatus = nullptr;
    brls::Label* lblMtpFilename = nullptr;
    brls::Box* boxMtpProgressFill = nullptr;
    brls::Label* lblMtpStats = nullptr;
    brls::Label* lblLogContent = nullptr;
};

// --- Tab 3: Servidor FTP (Capturas media_1790379582166.png & media_1790379631979.png) ---
class FtpTab : public brls::Box {
public:
    FtpTab() {
        this->inflateFromXMLFile("romfs:/xml/view_ftp.xml");

        lblFtpStatusVal = dynamic_cast<brls::Label*>(this->getView("lblFtpStatusVal"));
        lblFtpIpVal = dynamic_cast<brls::Label*>(this->getView("lblFtpIpVal"));
        lblFtpPortVal = dynamic_cast<brls::Label*>(this->getView("lblFtpPortVal"));
        lblFtpUserVal = dynamic_cast<brls::Label*>(this->getView("lblFtpUserVal"));
        lblFtpPassVal = dynamic_cast<brls::Label*>(this->getView("lblFtpPassVal"));
        lblFtpAnonVal = dynamic_cast<brls::Label*>(this->getView("lblFtpAnonVal"));
        btnFtpAction = dynamic_cast<brls::Box*>(this->getView("btnFtpAction"));
        lblFtpBtnText = dynamic_cast<brls::Label*>(this->getView("lblFtpBtnText"));
        boxFtpQrContainer = dynamic_cast<brls::Box*>(this->getView("boxFtpQrContainer"));

        // QR Code móvil integrado
        m_qrView = new QrCodeView("", 200.0f);
        if (boxFtpQrContainer) {
            boxFtpQrContainer->addView(m_qrView);
        }

        // Fila Puerto: Click para editar
        brls::Box* rowFtpPort = dynamic_cast<brls::Box*>(this->getView("rowFtpPort"));
        if (rowFtpPort) {
            rowFtpPort->registerClickAction([this](brls::View*) {
                int curPort = AppConfig::get().ftpPort > 0 ? AppConfig::get().ftpPort : 5000;
                std::string res = showHorizonKeyboard("Puerto FTP (1024-65535)", std::to_string(curPort));
                if (!res.empty()) {
                    try {
                        int p = std::stoi(res);
                        if (p >= 1024 && p <= 65535) {
                            AppConfig::get().ftpPort = p;
                            AppConfig::get().save();
                            updateFtpState(ftp_server::running());
                        }
                    } catch (...) {}
                }
                return true;
            });
        }

        // Fila Usuario: Click para editar
        brls::Box* rowFtpUser = dynamic_cast<brls::Box*>(this->getView("rowFtpUser"));
        if (rowFtpUser) {
            rowFtpUser->registerClickAction([this](brls::View*) {
                std::string res = showHorizonKeyboard("Nombre de Usuario FTP", AppConfig::get().ftpUsername);
                if (!res.empty()) {
                    AppConfig::get().ftpUsername = res;
                    AppConfig::get().save();
                    updateFtpState(ftp_server::running());
                }
                return true;
            });
        }

        // Fila Contraseña: Click para editar
        brls::Box* rowFtpPass = dynamic_cast<brls::Box*>(this->getView("rowFtpPass"));
        if (rowFtpPass) {
            rowFtpPass->registerClickAction([this](brls::View*) {
                std::string res = showHorizonKeyboard("Contraseña FTP", AppConfig::get().ftpPassword);
                if (!res.empty()) {
                    AppConfig::get().ftpPassword = res;
                    AppConfig::get().save();
                    updateFtpState(ftp_server::running());
                }
                return true;
            });
        }

        // Fila Inicio Anónimo: Click para alternar
        brls::Box* rowFtpAnon = dynamic_cast<brls::Box*>(this->getView("rowFtpAnon"));
        if (rowFtpAnon) {
            rowFtpAnon->registerClickAction([this](brls::View*) {
                AppConfig::get().ftpAnonLogin = !AppConfig::get().ftpAnonLogin;
                AppConfig::get().save();
                updateFtpState(ftp_server::running());
                return true;
            });
        }

        // Botón Iniciar / Detener Servidor (estilo Guardar cian)
        if (btnFtpAction) {
            btnFtpAction->registerClickAction([this](brls::View*) {
                if (ftp_server::running()) {
                    ftp_server::stop();
                    updateFtpState(false);
                } else {
                    int port = AppConfig::get().ftpPort > 0 ? AppConfig::get().ftpPort : 5000;
                    ftp_server::g_listenPort = port;
                    ftp_server::g_ftpUser = AppConfig::get().ftpUsername;
                    ftp_server::g_ftpPass = AppConfig::get().ftpPassword;
                    ftp_server::start(port);
                    updateFtpState(true);
                }
                return true;
            });
        }

        // Botón B para volver al Menú Principal
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });

        updateFtpState(ftp_server::running());
    }

    static brls::View* create() {
        return new FtpTab();
    }

    void updateFtpState(bool running) {
        if (lblFtpStatusVal) {
            lblFtpStatusVal->setText(running ? "hints/ftp_connected"_i18n : "hints/ftp_disconnected"_i18n);
            lblFtpStatusVal->setTextColor(running ? nvgRGB(0, 230, 168) : nvgRGB(160, 160, 160));
        }

        std::string ip = ftp_server::getRealIp();
        int port = AppConfig::get().ftpPort > 0 ? AppConfig::get().ftpPort : 5000;

        if (lblFtpIpVal) lblFtpIpVal->setText(ip.empty() ? "0.0.0.0" : ip);
        if (lblFtpPortVal) lblFtpPortVal->setText(std::to_string(port));
        if (lblFtpUserVal) lblFtpUserVal->setText(AppConfig::get().ftpUsername);
        if (lblFtpPassVal) {
            std::string pass = AppConfig::get().ftpPassword;
            lblFtpPassVal->setText(pass.empty() ? "(---)" : std::string(pass.length(), '*'));
        }
        if (lblFtpAnonVal) {
            lblFtpAnonVal->setText(AppConfig::get().ftpAnonLogin ? "hints/on"_i18n : "hints/off"_i18n);
        }

        if (lblFtpBtnText) {
            lblFtpBtnText->setText(running ? "hints/ftp_btn_stop"_i18n : "hints/ftp_btn_start"_i18n);
            lblFtpBtnText->setTextColor(nvgRGB(255, 255, 255));
        }
        if (btnFtpAction) {
            btnFtpAction->setBackgroundColor(running ? nvgRGB(239, 68, 68) : nvgRGB(2, 132, 199));
        }

        if (m_qrView) {
            if (running && !ip.empty() && ip != "0.0.0.0") {
                std::string qrUrl = "ftp://" + AppConfig::get().ftpUsername + ":" + AppConfig::get().ftpPassword + "@" + ip + ":" + std::to_string(port);
                m_qrView->setText(qrUrl);
            } else {
                m_qrView->setText("ftp://192.168.0.1:5000");
            }
        }
    }

private:
    brls::Label* lblFtpStatusVal = nullptr;
    brls::Label* lblFtpIpVal = nullptr;
    brls::Label* lblFtpPortVal = nullptr;
    brls::Label* lblFtpUserVal = nullptr;
    brls::Label* lblFtpPassVal = nullptr;
    brls::Label* lblFtpAnonVal = nullptr;
    brls::Box* btnFtpAction = nullptr;
    brls::Label* lblFtpBtnText = nullptr;
    brls::Box* boxFtpQrContainer = nullptr;
    QrCodeView* m_qrView = nullptr;
};

// --- Fila de Selección de Tema con Previsualización (Screenshot 6 Style) ---
class ThemeOptionRow : public brls::Box {
public:
    ThemeOptionRow(const std::string& themeKey, const std::string& labelText, NVGcolor previewBg, NVGcolor previewBorder)
        : m_themeKey(themeKey) {
        this->setFocusable(true);
        this->setHeight(56);
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setPadding(0, 16, 0, 16);
        this->setCornerRadius(6);

        // Rectángulo de previsualización (Screenshot 6)
        brls::Box* preview = new brls::Box();
        preview->setWidth(52);
        preview->setHeight(32);
        preview->setCornerRadius(4);
        preview->setBackgroundColor(previewBg);
        preview->setBorderColor(previewBorder);
        preview->setBorderThickness(1.5f);
        preview->setMarginRight(18);
        this->addView(preview);

        // Nombre del tema
        brls::Label* lbl = new brls::Label();
        lbl->setText(labelText);
        lbl->setFontSize(16);
        lbl->setGrow(1.0f);
        this->addView(lbl);

        // Checkmark verde estilo Switch usando CheckmarkView vectorial idéntico a LanguageCell
        m_check = new CheckmarkView(11.0f);
        m_check->setVisibility(AppConfig::get().theme == themeKey ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        this->addView(m_check);
    }

    void setSelected(bool sel) {
        if (m_check) m_check->setVisibility(sel ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

    std::string getThemeKey() const { return m_themeKey; }

private:
    std::string m_themeKey;
    CheckmarkView* m_check = nullptr;
};

// --- Tab 4: Ajustes ---
class WallpaperListView : public brls::Box {
public:
    WallpaperListView() {
        this->setHeight(brls::Application::windowHeight);
        this->setWidth(brls::Application::windowWidth);
        this->setAxis(brls::Axis::COLUMN);
        
        // El fondo del stack view se debe ver como la applet native
        this->setBackgroundColor(brls::Application::getTheme()["brls/background"]);

        brls::Header* header = new brls::Header();
        header->setTitle("hints/wallpaper_select"_i18n);
        header->setSubtitle("sdmc:/switch/FileZzz/wallpapers/");
        header->setMarginLeft(50);
        header->setMarginTop(20);
        this->addView(header);

        brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setMarginLeft(50);
        scroll->setMarginRight(50);
        
        brls::Box* list = new brls::Box(brls::Axis::COLUMN);

        // Opción "Por defecto"
        brls::RadioCell* cellDefault = new brls::RadioCell();
        cellDefault->title->setText("hints/wallpaper_default"_i18n);
        cellDefault->setSelected(AppConfig::get().wallpaperPath.empty());
        cellDefault->registerClickAction([this](brls::View*) {
            AppConfig::get().wallpaperPath = "";
            AppConfig::get().save();
            applyAppTheme(AppConfig::get().theme);
            brls::Application::popActivity();
            return true;
        });
        list->addView(cellDefault);

        // Escanear directorios de fondos (FileZzz y EzFiles para compatibilidad)
        std::vector<std::string> wpDirs = {
            "sdmc:/switch/FileZzz/wallpapers/",
            "sdmc:/switch/EzFiles/wallpapers/"
        };
        mkdir("sdmc:/switch", 0777);
        mkdir("sdmc:/switch/FileZzz", 0777);
        mkdir("sdmc:/switch/FileZzz/wallpapers", 0777);

        std::set<std::string> seenFiles;
        for (const auto& wpDir : wpDirs) {
            DIR* dir = opendir(wpDir.c_str());
            if (dir) {
                struct dirent* ent;
                while ((ent = readdir(dir)) != nullptr) {
                    std::string name = ent->d_name;
                    if (name == "." || name == "..") continue;
                    if (seenFiles.count(name)) continue;
                    if (name.find(".jpg") != std::string::npos || name.find(".png") != std::string::npos ||
                        name.find(".jpeg") != std::string::npos) {
                        seenFiles.insert(name);
                        brls::RadioCell* cell = new brls::RadioCell();
                        cell->title->setText(name);
                        std::string fullPath = wpDir + name;
                        cell->setSelected(AppConfig::get().wallpaperPath == fullPath);
                        cell->registerClickAction([this, fullPath](brls::View*) {
                            AppConfig::get().wallpaperPath = fullPath;
                            AppConfig::get().save();
                            applyAppTheme(AppConfig::get().theme);
                            brls::Application::popActivity();
                            return true;
                        });
                        list->addView(cell);
                    }
                }
                closedir(dir);
            }
        }

        scroll->setContentView(list);
        this->addView(scroll);

        // Botón B para volver
        this->registerAction("hints/back"_i18n, brls::BUTTON_BACK, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }
};

// --- Celda de Selección de Idioma (Captura media_1790379421997.png) ---
class LanguageCell : public brls::Box {
public:
    LanguageCell(const std::string& name, bool isSelected) : m_isSelected(isSelected) {
        this->setFocusable(true);
        this->setHeight(56);
        this->setWidthPercentage(100.0f);
        this->setAxis(brls::Axis::ROW);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        this->setPadding(0, 24, 0, 24);
        this->setCornerRadius(6);

        m_label = new brls::Label();
        m_label->setText(name);
        m_label->setFontSize(18);
        m_label->setTextColor(isSelected ? nvgRGB(0, 230, 168) : nvgRGB(255, 255, 255));
        m_label->setGrow(1.0f);
        this->addView(m_label);

        m_check = new CheckmarkView(11.0f);
        m_check->setVisibility(isSelected ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
        this->addView(m_check);
    }

    void setSelected(bool sel) {
        m_isSelected = sel;
        if (m_label) m_label->setTextColor(sel ? nvgRGB(0, 230, 168) : nvgRGB(255, 255, 255));
        if (m_check) m_check->setVisibility(sel ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    }

private:
    bool m_isSelected = false;
    brls::Label* m_label = nullptr;
    CheckmarkView* m_check = nullptr;
};

class LanguageListView : public brls::Box {
public:
    LanguageListView() {
        this->setHeight(brls::Application::windowHeight);
        this->setWidth(brls::Application::windowWidth);
        this->setAxis(brls::Axis::COLUMN);
        this->setPaddingTop(30);
        this->setPaddingLeft(80);
        this->setPaddingRight(80);
        this->setPaddingBottom(20);
        this->setBackgroundColor(brls::Application::getTheme()["brls/background"]);

        // Encabezado con línea horizontal divisoria idéntico a media_1790379421997.png
        brls::Box* headerBox = new brls::Box(brls::Axis::COLUMN);
        headerBox->setWidthPercentage(100.0f);
        headerBox->setMarginBottom(16);

        brls::Label* lblHeader = new brls::Label();
        lblHeader->setText("hints/settings_language"_i18n);
        lblHeader->setFontSize(26);
        lblHeader->setTextColor(brls::Application::getTheme()["brls/text"]);
        lblHeader->setMarginBottom(12);
        headerBox->addView(lblHeader);

        brls::Box* divLine = new brls::Box();
        divLine->setWidthPercentage(100.0f);
        divLine->setHeight(1);
        divLine->setBackgroundColor(brls::Application::getTheme()["brls/sidebar/separator"]);
        headerBox->addView(divLine);

        this->addView(headerBox);

        // Contenedor scrollable a todo lo ancho para las opciones
        brls::ScrollingFrame* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1.0f);
        scroll->setWidthPercentage(100.0f);
        
        brls::Box* list = new brls::Box(brls::Axis::COLUMN);
        list->setWidthPercentage(100.0f);

        struct LangItem {
            std::string code;
            std::string name;
        };

        std::vector<LangItem> langs = {
            { "en", "English" }, { "fr", "Français" }, { "de", "Deutsch" },
            { "es", "Español" }, { "it", "Italiano" }, { "nl", "Nederlands" },
            { "pt", "Português" }, { "ru", "Русский" }, { "ja", "日本語" },
            { "zh-Hans", "简体中文" }, { "zh-Hant", "繁體中文" }, { "ko", "한국어" }
        };

        std::string currentLang = AppConfig::get().language;
        if (currentLang.empty()) currentLang = "es";

        for (const auto& l : langs) {
            LanguageCell* cell = new LanguageCell(l.name, currentLang == l.code);
            cell->registerClickAction([this, l](brls::View*) {
                AppConfig::get().language = l.code;
                AppConfig::get().save();

                std::string loc = "es-419";
                if (l.code == "en") loc = "en-US";
                else if (l.code == "es") loc = "es-419";
                else loc = l.code;
                
                brls::Platform::APP_LOCALE_DEFAULT = loc;
                brls::Application::setLocale(loc);

                reloadMainActivity(4, true); // Recarga segura manteniendo SettingsTab abierto
                return true;
            });
            list->addView(cell);

            // Separador fino estilo Horizon OS entre opciones
            brls::Box* sep = new brls::Box();
            sep->setWidthPercentage(100.0f);
            sep->setHeight(1);
            sep->setBackgroundColor(nvgRGBA(120, 120, 120, 60));
            list->addView(sep);
        }

        scroll->setContentView(list);
        this->addView(scroll);

        this->registerAction("hints/back"_i18n, brls::BUTTON_BACK, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }
};

class SettingsTab : public brls::Box {
public:
    SettingsTab() {
        this->inflateFromXMLFile("romfs:/xml/view_settings.xml");

        brls::Box* boxThemesList = dynamic_cast<brls::Box*>(this->getView("boxThemesList"));
        rowWallpaper = dynamic_cast<brls::Box*>(this->getView("rowWallpaper"));
        lblWallpaperVal = dynamic_cast<brls::Label*>(this->getView("lblWallpaperVal"));
        rowWallpaperOpacity = dynamic_cast<brls::Box*>(this->getView("rowWallpaperOpacity"));
        lblWallpaperOpacityVal = dynamic_cast<brls::Label*>(this->getView("lblWallpaperOpacityVal"));
        rowLanguage = dynamic_cast<brls::Box*>(this->getView("rowLanguage"));
        lblLanguageVal = dynamic_cast<brls::Label*>(this->getView("lblLanguageVal"));
        rowFtpPort = dynamic_cast<brls::Box*>(this->getView("rowFtpPort"));
        lblFtpPortVal = dynamic_cast<brls::Label*>(this->getView("lblFtpPortVal"));
        rowFtpUser = dynamic_cast<brls::Box*>(this->getView("rowFtpUser"));
        lblFtpUserVal = dynamic_cast<brls::Label*>(this->getView("lblFtpUserVal"));
        rowFtpPass = dynamic_cast<brls::Box*>(this->getView("rowFtpPass"));
        lblFtpPassVal = dynamic_cast<brls::Label*>(this->getView("lblFtpPassVal"));

        if (boxThemesList) {
            boxThemesList->clearViews();
            themeRows.clear();

            auto addTheme = [&](const std::string& key, const std::string& label, NVGcolor bg, NVGcolor border) {
                ThemeOptionRow* row = new ThemeOptionRow(key, label, bg, border);
                row->registerClickAction([this, key](brls::View*) {
                    AppConfig::get().theme = key;
                    AppConfig::get().save();
                    applyAppTheme(key);
                    reloadMainActivity(4, true); // Recarga segura manteniendo SettingsTab abierto
                    return true;
                });
                themeRows.push_back(row);
                boxThemesList->addView(row);
            };

            addTheme("Light", "hints/theme_light"_i18n, nvgRGB(245, 245, 245), nvgRGB(180, 180, 180));
            addTheme("Dark", "hints/theme_dark"_i18n, nvgRGB(45, 45, 45), nvgRGB(90, 90, 90));
            addTheme("AMOLED", "hints/theme_amoled"_i18n, nvgRGB(0, 0, 0), nvgRGB(50, 50, 50));
        }

        if (rowWallpaper) {
            std::string wp = AppConfig::get().wallpaperPath;
            if (wp.empty()) {
                if (lblWallpaperVal) lblWallpaperVal->setText("hints/wallpaper_default"_i18n);
            } else {
                size_t pos = wp.find_last_of('/');
                std::string fname = (pos != std::string::npos) ? wp.substr(pos + 1) : wp;
                if (lblWallpaperVal) lblWallpaperVal->setText(fname);
            }

            rowWallpaper->registerClickAction([](brls::View*) {
                brls::Application::pushActivity(new FeatureActivity(new WallpaperListView()));
                return true;
            });
        }

        if (rowWallpaperOpacity) {
            int percentage = (int)(AppConfig::get().wallpaperOpacity * 100);
            if (lblWallpaperOpacityVal) lblWallpaperOpacityVal->setText(std::to_string(percentage) + "%");

            rowWallpaperOpacity->registerClickAction([this](brls::View*) {
                float op = AppConfig::get().wallpaperOpacity;
                if (op >= 1.0f) op = 0.75f;
                else if (op >= 0.75f) op = 0.5f;
                else if (op >= 0.5f) op = 0.25f;
                else if (op >= 0.25f) op = 0.0f;
                else op = 1.0f;

                AppConfig::get().wallpaperOpacity = op;
                AppConfig::get().save();
                int pct = (int)(op * 100);
                if (lblWallpaperOpacityVal) {
                    lblWallpaperOpacityVal->setText(std::to_string(pct) + "%");
                }
                return true;
            });
        }

        if (rowLanguage) {
            std::string curLang = AppConfig::get().language;
            std::string langDisplay = "Español";
            if (curLang == "en") langDisplay = "English";
            else if (curLang == "fr") langDisplay = "Français";
            else if (curLang == "de") langDisplay = "Deutsch";
            else if (curLang == "it") langDisplay = "Italiano";
            else if (curLang == "nl") langDisplay = "Nederlands";
            else if (curLang == "pt") langDisplay = "Português";
            else if (curLang == "ru") langDisplay = "Русский";
            else if (curLang == "ja") langDisplay = "日本語";
            else if (curLang == "zh-Hans") langDisplay = "简体中文";
            else if (curLang == "zh-Hant") langDisplay = "繁體中文";
            else if (curLang == "ko") langDisplay = "한국어";
            if (lblLanguageVal) lblLanguageVal->setText(langDisplay);

            rowLanguage->registerClickAction([this](brls::View*) {
                openLanguageSelector();
                return true;
            });
        }

        if (rowFtpPort) {
            int curPort = AppConfig::get().ftpPort > 0 ? AppConfig::get().ftpPort : 5000;
            if (lblFtpPortVal) lblFtpPortVal->setText(std::to_string(curPort));
            rowFtpPort->registerClickAction([this](brls::View*) {
                int port = AppConfig::get().ftpPort > 0 ? AppConfig::get().ftpPort : 5000;
                std::string res = showHorizonKeyboard("Puerto FTP (1024-65535)", std::to_string(port));
                if (!res.empty()) {
                    int p = atoi(res.c_str());
                    if (p >= 1024 && p <= 65535) {
                        AppConfig::get().ftpPort = p;
                        AppConfig::get().save();
                        if (lblFtpPortVal) lblFtpPortVal->setText(std::to_string(p));
                    }
                }
                return true;
            });
        }

        if (rowFtpUser) {
            if (lblFtpUserVal) lblFtpUserVal->setText(AppConfig::get().ftpUsername);
            rowFtpUser->registerClickAction([this](brls::View*) {
                std::string res = showHorizonKeyboard("Usuario del Servidor FTP", AppConfig::get().ftpUsername);
                if (!res.empty()) {
                    AppConfig::get().ftpUsername = res;
                    AppConfig::get().save();
                    if (lblFtpUserVal) lblFtpUserVal->setText(res);
                }
                return true;
            });
        }

        if (rowFtpPass) {
            std::string pass = AppConfig::get().ftpPassword.empty() ? "switch" : AppConfig::get().ftpPassword;
            if (lblFtpPassVal) lblFtpPassVal->setText(std::string(pass.length(), '*'));
            rowFtpPass->registerClickAction([this](brls::View*) {
                std::string curPass = AppConfig::get().ftpPassword.empty() ? "switch" : AppConfig::get().ftpPassword;
                std::string res = showHorizonKeyboard("Contraseña del Servidor FTP", curPass);
                if (!res.empty()) {
                    AppConfig::get().ftpPassword = res;
                    AppConfig::get().save();
                    if (lblFtpPassVal) lblFtpPassVal->setText(std::string(res.length(), '*'));
                }
                return true;
            });
        }

        // Botón B para volver al Menú Principal
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }

    void openLanguageSelector() {
        brls::Application::pushActivity(new FeatureActivity(new LanguageListView()));
    }

    static brls::View* create() {
        return new SettingsTab();
    }

private:
    std::vector<ThemeOptionRow*> themeRows;
    brls::Box* rowWallpaper = nullptr;
    brls::Label* lblWallpaperVal = nullptr;
    brls::Box* rowWallpaperOpacity = nullptr;
    brls::Label* lblWallpaperOpacityVal = nullptr;
    brls::Box* rowLanguage = nullptr;
    brls::Label* lblLanguageVal = nullptr;
    brls::Box* rowFtpPort = nullptr;
    brls::Label* lblFtpPortVal = nullptr;
    brls::Box* rowFtpUser = nullptr;
    brls::Label* lblFtpUserVal = nullptr;
    brls::Box* rowFtpPass = nullptr;
    brls::Label* lblFtpPassVal = nullptr;
};

// --- Tab 5: Acerca de ---
class AboutTab : public brls::Box {
public:
    AboutTab() {
        this->inflateFromXMLFile("romfs:/xml/view_about.xml");

        // Vincular código QR de Donaciones / PayPal
        brls::Box* boxCoffeeQr = dynamic_cast<brls::Box*>(this->getView("boxCoffeeQr"));
        if (boxCoffeeQr) {
            QrCodeView* qr = new QrCodeView("https://paypal.me/francopaololg", 200.0f);
            boxCoffeeQr->addView(qr);
        }

        // Vincular botón Buscar Actualizaciones
        brls::Box* btnCheckUpdates = dynamic_cast<brls::Box*>(this->getView("btnCheckUpdates"));
        if (btnCheckUpdates) {
            btnCheckUpdates->registerClickAction([](brls::View*) {
                brls::Application::notify("FileZzz v1.3.0 está en su versión más reciente.");
                return true;
            });
        }

        // Botón B para volver al Menú Principal
        this->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
    }

    static brls::View* create() {
        return new AboutTab();
    }
};

// --- Pantalla de Inicio / Splash (animada estilo Switch nativo) ---
class SplashActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_FILE("romfs:/xml/view_splash.xml");

    SplashActivity() {}

    void onContentAvailable() override {
        brls::Activity::onContentAvailable();

        if (this->getContentView()) {
            this->getContentView()->setFocusable(true);
            this->getContentView()->setHideHighlight(true);
            brls::Application::giveFocus(this->getContentView());
        }

        // Carga opcional de logotipo personalizado desde SD si existe
        if (access("sdmc:/switch/FileZzz/logo.png", F_OK) == 0) {
            brls::Image* img = dynamic_cast<brls::Image*>(this->getView("splashLogo"));
            if (img) img->setImageFromFile("sdmc:/switch/FileZzz/logo.png");
        }

        auto dismissed = std::make_shared<bool>(false);
        auto dismiss = [dismissed](brls::View*) {
            if (!*dismissed) {
                *dismissed = true;
                brls::Application::popActivity(brls::TransitionAnimation::FADE);
            }
            return true;
        };

        this->registerAction("", brls::BUTTON_A, dismiss);
        this->registerAction("", brls::BUTTON_B, dismiss);
        if (this->getContentView()) {
            this->getContentView()->registerClickAction([dismiss](brls::View* v) {
                return dismiss(v);
            });
        }

        // Transición automática suave estilo Switch nativo a los 1100 ms
        brls::delay(1100, [dismissed]() {
            if (!*dismissed) {
                *dismissed = true;
                brls::Application::popActivity(brls::TransitionAnimation::FADE);
            }
        });
    }
};

// --- HeroTab: Tarjeta de bienvenida y descripción para cada sección del menú principal ---
class HeroTab : public brls::Box {
public:
    HeroTab() {
        this->inflateFromXMLFile("romfs:/xml/view_hero.xml");
        this->setGrow(1.0f);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->setJustifyContent(brls::JustifyContent::CENTER);
        m_icon = dynamic_cast<brls::Image*>(this->getView("heroIcon"));
        m_title = dynamic_cast<brls::Label*>(this->getView("heroTitle"));
        m_desc = dynamic_cast<brls::Label*>(this->getView("heroDesc"));
        m_btn = dynamic_cast<brls::Box*>(this->getView("btnOpenFeature"));
        m_btnText = dynamic_cast<brls::Label*>(this->getView("lblOpenText"));
    }

    void setup(const std::string& iconPath, const std::string& title, const std::string& desc, const std::string& btnText, std::function<void()> onClick) {
        if (m_icon) m_icon->setImageFromFile(iconPath);
        if (m_title) m_title->setText(title);
        if (m_desc) m_desc->setText(desc);
        if (m_btnText) m_btnText->setText(btnText);
        if (m_btn) {
            m_btn->registerClickAction([onClick](brls::View*) {
                if (onClick) onClick();
                return true;
            });
        }
    }

    void willAppear(bool resetState = false) override {
        brls::Box::willAppear(resetState);
        std::string tabId = this->id;
        if (tabId == "heroTabExplorer") {
            setup("romfs:/img/icon_explorer.png", "hints/tab_explorer"_i18n,
                  "hints/tab_explorer_desc"_i18n,
                  "hints/tab_explorer_btn"_i18n, []() {
                      brls::Application::pushActivity(new FeatureActivity(new ExplorerTab()));
                  });
        } else if (tabId == "heroTabGames") {
            setup("romfs:/img/icon_nsp_sm.png", "hints/tab_games"_i18n,
                  "hints/tab_games_desc"_i18n,
                  "hints/tab_games_btn"_i18n, []() {
                      try {
                          brls::Application::pushActivity(new FeatureActivity(new GamesTab()));
                      } catch (...) {
                          brls::Logger::error("Excepción al abrir GamesTab");
                      }
                  });
        } else if (tabId == "heroTabMtp") {
            setup("romfs:/img/icon_usb.png", "hints/tab_mtp"_i18n,
                  "hints/tab_mtp_desc"_i18n,
                  "hints/tab_mtp_btn"_i18n, []() {
                      brls::Application::pushActivity(new FeatureActivity(new MtpTab()));
                  });
        } else if (tabId == "heroTabFtp") {
            setup("romfs:/img/icon_ftp.png", "hints/tab_ftp"_i18n,
                  "hints/tab_ftp_desc"_i18n,
                  "hints/tab_ftp_btn"_i18n, []() {
                      brls::Application::pushActivity(new FeatureActivity(new FtpTab()));
                  });
        } else if (tabId == "heroTabSettings") {
            setup("romfs:/img/icon_theme.png", "hints/tab_settings"_i18n,
                  "hints/tab_settings_desc"_i18n,
                  "hints/tab_settings_btn"_i18n, []() {
                      brls::Application::pushActivity(new FeatureActivity(new SettingsTab()));
                  });
        } else if (tabId == "heroTabAbout") {
            setup("romfs:/img/icon_about.png", "hints/tab_about"_i18n,
                  "hints/tab_about_desc"_i18n,
                  "hints/tab_about_btn"_i18n, []() {
                      brls::Application::pushActivity(new FeatureActivity(new AboutTab()));
                  });
        }
    }

    static brls::View* create() {
        return new HeroTab();
    }

private:
    brls::Image* m_icon = nullptr;
    brls::Label* m_title = nullptr;
    brls::Label* m_desc = nullptr;
    brls::Box* m_btn = nullptr;
    brls::Label* m_btnText = nullptr;
};

// --- Actividad Principal (con Menú Principal y Vistas a Pantalla Completa) ---
class MainActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_FILE("romfs:/xml/main_tabs.xml");

    MainActivity(int initialTab = 0)
        : m_initialTab(initialTab) {}

    void openFeature(int index) {
        try {
            switch (index) {
                case 0: brls::Application::pushActivity(new FeatureActivity(new ExplorerTab())); break;
                case 1: brls::Application::pushActivity(new FeatureActivity(new GamesTab())); break;
                case 2: brls::Application::pushActivity(new FeatureActivity(new MtpTab())); break;
                case 3: brls::Application::pushActivity(new FeatureActivity(new FtpTab())); break;
                case 4: brls::Application::pushActivity(new FeatureActivity(new SettingsTab())); break;
                case 5: brls::Application::pushActivity(new FeatureActivity(new AboutTab())); break;
            }
        } catch (const std::exception& e) {
            brls::Logger::error("Excepción en openFeature({}): {}", index, e.what());
        } catch (...) {
            brls::Logger::error("Excepción desconocida en openFeature({})", index);
        }
    }

    void onContentAvailable() override {
        brls::Activity::onContentAvailable();

        brls::AppletFrame* applet = dynamic_cast<brls::AppletFrame*>(this->getContentView());
        g_appletFrame = applet;

        g_mainTabFrame = dynamic_cast<brls::TabFrame*>(this->getView("mainTabFrame"));
        if (g_mainTabFrame) {
            auto* sidebar = dynamic_cast<brls::Sidebar*>(g_mainTabFrame->getView("brls/tab_frame/sidebar"));
            if (sidebar) {
                const std::vector<std::string> tabKeys = {
                    "hints/tab_explorer"_i18n,
                    "hints/tab_games"_i18n,
                    "hints/tab_mtp"_i18n,
                    "hints/tab_ftp"_i18n,
                    "hints/tab_settings"_i18n,
                    "hints/tab_about"_i18n
                };
                const std::vector<std::string> tabIcons = {
                    "romfs:/img/icon_explorer.png",
                    "romfs:/img/icon_nsp_sm.png",
                    "romfs:/img/icon_usb.png",
                    "romfs:/img/icon_ftp.png",
                    "romfs:/img/icon_theme.png",
                    "romfs:/img/icon_about.png"
                };
                for (size_t i = 0; i < tabKeys.size(); i++) {
                    auto* it = sidebar->getItem((int)i);
                    if (it) {
                        it->setLabel(tabKeys[i]);
                        it->setIcon(tabIcons[i]);
                        it->getActiveEvent()->subscribe([i](brls::View*) {
                            g_currentTabIndex = (int)i;
                        });
                        it->registerClickAction([this, i](brls::View*) {
                            openFeature((int)i);
                            return true;
                        });
                    }
                }
            }

            auto setupHero = [this](const std::string& id, const std::string& icon, const std::string& title, const std::string& desc, const std::string& btn, int tabIdx) {
                auto* hero = dynamic_cast<HeroTab*>(this->getView(id));
                if (hero) {
                    hero->setup(icon, title, desc, btn, [this, tabIdx]() {
                        openFeature(tabIdx);
                    });
                }
            };

            setupHero("heroTabExplorer", "romfs:/img/icon_explorer.png", "hints/tab_explorer"_i18n,
                      "hints/tab_explorer_desc"_i18n,
                      "hints/tab_explorer_btn"_i18n, 0);
            setupHero("heroTabGames", "romfs:/img/icon_nsp_sm.png", "hints/tab_games"_i18n,
                      "hints/tab_games_desc"_i18n,
                      "hints/tab_games_btn"_i18n, 1);
            setupHero("heroTabMtp", "romfs:/img/icon_usb.png", "hints/tab_mtp"_i18n,
                      "hints/tab_mtp_desc"_i18n,
                      "hints/tab_mtp_btn"_i18n, 2);
            setupHero("heroTabFtp", "romfs:/img/icon_ftp.png", "hints/tab_ftp"_i18n,
                      "hints/tab_ftp_desc"_i18n,
                      "hints/tab_ftp_btn"_i18n, 3);
            setupHero("heroTabSettings", "romfs:/img/icon_theme.png", "hints/tab_settings"_i18n,
                      "hints/tab_settings_desc"_i18n,
                      "hints/tab_settings_btn"_i18n, 4);
            setupHero("heroTabAbout", "romfs:/img/icon_about.png", "hints/tab_about"_i18n,
                      "hints/tab_about_desc"_i18n,
                      "hints/tab_about_btn"_i18n, 5);

            if (m_initialTab > 0) {
                switchToTab(m_initialTab);
            }
        }

        // Atajos globales de cambio de pestañas con L / R y ZL / ZR
        this->registerAction("hints/prev_tab"_i18n, brls::BUTTON_LB, [](brls::View*) {
            cycleTab(-1);
            return true;
        });
        this->registerAction("hints/next_tab"_i18n, brls::BUTTON_RB, [](brls::View*) {
            cycleTab(1);
            return true;
        });
        this->registerAction("", brls::BUTTON_LT, [](brls::View*) {
            cycleTab(-1);
            return true;
        }, true);
        this->registerAction("", brls::BUTTON_RT, [](brls::View*) {
            cycleTab(1);
            return true;
        }, true);

        // Atajo formal de salida con botón '-' (Minus / BUTTON_BACK)
        this->registerAction("hints/exit"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
            brls::Dialog* d = new brls::Dialog("hints/exit_hint"_i18n);
            d->addButton("hints/cancel"_i18n, []() {});
            d->addButton("hints/exit"_i18n, []() {
                brls::Application::quit();
            });
            d->open();
            return true;
        });
    }

    static MainActivity* create(int initialTab = 0) {
        return new MainActivity(initialTab);
    }

private:
    int m_initialTab = 0;
};

// Implementación de reloadMainActivity movida más abajo para acceder a MainActivity y SettingsTab
void reloadMainActivity(int initialTab, bool reopenSettings) {
    brls::sync([initialTab, reopenSettings]() {
        brls::Application::clear();
        brls::Application::pushActivity(new MainActivity(initialTab), brls::TransitionAnimation::NONE);
        if (reopenSettings) {
            brls::Application::pushActivity(new FeatureActivity(new SettingsTab()), brls::TransitionAnimation::NONE);
        }
    });
}

int main(int argc, char* argv[]) {
    // Cargar configuración guardada
    AppConfig::get().load();

    // Configurar bitácora persistente en SD para diagnóstico
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/FileZzz", 0777);
    FILE* logf = fopen("sdmc:/switch/FileZzz/filezzz.log", "w");
    if (logf) {
        setvbuf(logf, NULL, _IONBF, 0);
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        brls::Logger::setLogOutput(logf);
        brls::Logger::info("FileZzz Borealis iniciando...");
    }

    // Configurar idioma predeterminado
    std::string lang = AppConfig::get().language;
    std::string initialLocale = "es-419";
    if (lang == "en") initialLocale = "en-US";
    else if (lang == "es") initialLocale = "es-419";
    else if (!lang.empty()) initialLocale = lang;
    
    brls::Platform::APP_LOCALE_DEFAULT = initialLocale;

    if (!brls::Application::init()) {
        brls::Logger::error("No se pudo inicializar Borealis");
        if (logf) fclose(logf);
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("FileZzz");

    // Configurar tema guardado (Claro, Oscuro, AMOLED)
    applyAppTheme(AppConfig::get().theme);

    // Desactivar quit global con '+' para usar '+' para opciones y '-' para salir con diálogo
    brls::Application::setGlobalQuit(false);

    // Registrar vistas personalizadas del XML
    brls::Application::registerXMLView("WallpaperAppletFrame", WallpaperAppletFrame::create);
    brls::Application::registerXMLView("HeroTab", HeroTab::create);
    brls::Application::registerXMLView("ExplorerTab", ExplorerTab::create);
    brls::Application::registerXMLView("GamesTab", GamesTab::create);
    brls::Application::registerXMLView("MtpTab", MtpTab::create);
    brls::Application::registerXMLView("FtpTab", FtpTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
    brls::Application::registerXMLView("AboutTab", AboutTab::create);

    try {
        // MainActivity es la actividad raíz limpia
        brls::Application::pushActivity(MainActivity::create(0));
        // SplashActivity animado estilo Switch en primer plano que se desvanece suavemente
        brls::Application::pushActivity(new SplashActivity(), brls::TransitionAnimation::NONE);
        while (brls::Application::mainLoop());
    } catch (const std::exception& e) {
        brls::Logger::error("Excepción interceptada: {}", e.what());
    }

    // Cierre limpio de servicios de fondo
    if (mtp_ops::running()) {
        mtp_ops::stop();
        mtp_usb::teardown();
    }
    if (ftp_server::running()) {
        ftp_server::stop();
    }

    if (logf) {
        brls::Logger::info("FileZzz cerrado correctamente.");
        fclose(logf);
    }

    return EXIT_SUCCESS;
}
