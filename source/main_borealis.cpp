#include <switch.h>
#include <borealis.hpp>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <algorithm>

#include "mtp_usb.hpp"
#include "mtp_ops.hpp"
#include "ftp_server.hpp"
#include "config.hpp"

// Portapapeles global del explorador
inline std::string g_clipboardPath = "";
inline bool g_clipboardIsCut = false;

// Utilidad para copiar archivos en bloque
inline bool copyFile(const std::string& src, const std::string& dst) {
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in); fclose(out);
            unlink(dst.c_str());
            return false;
        }
    }
    fclose(in);
    fclose(out);
    return true;
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

// --- Tab 1: Explorador de Archivos ---
class ExplorerTab : public brls::Box {
public:
    ExplorerTab() {
        this->inflateFromXMLFile("romfs:/xml/view_explorer.xml");
        currentPath = "sdmc:/";
        refreshList();
    }

    static brls::View* create() {
        return new ExplorerTab();
    }

    void refreshList() {
        brls::Box* boxFiles = dynamic_cast<brls::Box*>(this->getView("boxFiles"));
        brls::Label* lblCurrentPath = dynamic_cast<brls::Label*>(this->getView("lblCurrentPath"));
        brls::Label* lblItemCount = dynamic_cast<brls::Label*>(this->getView("lblItemCount"));

        if (!boxFiles) return;
        boxFiles->clearViews();

        DIR* dir = opendir(currentPath.c_str());
        if (!dir) return;

        if (lblCurrentPath) lblCurrentPath->setText(currentPath);

        // Barra de pegado si hay un elemento en portapapeles
        if (!g_clipboardPath.empty()) {
            size_t slash = g_clipboardPath.find_last_of('/');
            std::string clipName = (slash != std::string::npos) ? g_clipboardPath.substr(slash + 1) : g_clipboardPath;
            auto pasteItem = new brls::DetailCell();
            pasteItem->setText(std::string(">> PEGAR AQUI: ") + clipName);
            pasteItem->setDetailText(g_clipboardIsCut ? "[Mover archivo]" : "[Copiar archivo]");
            pasteItem->setTextColor(nvgRGB(16, 185, 129));
            pasteItem->registerClickAction([this, clipName](brls::View*) {
                std::string destPath = currentPath + (currentPath.back() == '/' ? "" : "/") + clipName;
                if (g_clipboardIsCut) {
                    if (rename(g_clipboardPath.c_str(), destPath.c_str()) != 0) {
                        copyFile(g_clipboardPath, destPath);
                        unlink(g_clipboardPath.c_str());
                    }
                } else {
                    copyFile(g_clipboardPath, destPath);
                }
                g_clipboardPath = "";
                g_clipboardIsCut = false;
                refreshList();
                return true;
            });
            boxFiles->addView(pasteItem);
        }

        struct dirent* entry;
        int count = 0;
        
        if (currentPath != "sdmc:/" && currentPath != "sdmc:") {
            auto upItem = new brls::DetailCell();
            upItem->setText("..");
            upItem->setDetailText("<SUBIR NIVEL>");
            upItem->registerClickAction([this](brls::View*) {
                size_t slash = currentPath.find_last_of('/', currentPath.length() - 2);
                if (slash != std::string::npos) {
                    currentPath = currentPath.substr(0, slash + 1);
                } else {
                    currentPath = "sdmc:/";
                }
                refreshList();
                return true;
            });
            boxFiles->addView(upItem);
        }

        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            std::string name = entry->d_name;
            bool isDir = (entry->d_type == DT_DIR);
            std::string fullPath = currentPath + (currentPath.back() == '/' ? "" : "/") + name;
            
            auto cell = new brls::DetailCell();
            cell->setText(name);

            if (isDir) {
                cell->setDetailText("<CARPETA>");
                cell->registerClickAction([this, fullPath](brls::View*) {
                    currentPath = fullPath + "/";
                    refreshList();
                    return true;
                });
            } else {
                struct stat st;
                u64 sz = 0;
                if (stat(fullPath.c_str(), &st) == 0) sz = st.st_size;
                char sBuf[32];
                if (sz < 1024) snprintf(sBuf, sizeof(sBuf), "%llu B", (unsigned long long)sz);
                else if (sz < 1024*1024) snprintf(sBuf, sizeof(sBuf), "%.1f KB", sz / 1024.0);
                else if (sz < 1024*1024*1024) snprintf(sBuf, sizeof(sBuf), "%.2f MB", sz / (1024.0*1024.0));
                else snprintf(sBuf, sizeof(sBuf), "%.2f GB", sz / (1024.0*1024.0*1024.0));
                cell->setDetailText(sBuf);

                // Clic primario en archivo muestra propiedades informativas sin error
                cell->registerClickAction([this, fullPath, name, sz](brls::View*) {
                    showFileProperties(fullPath, name, sz);
                    return true;
                });
            }

            // Atajo con Boton X de mando Nintendo Switch para menu contextual
            cell->registerAction("Opciones", brls::BUTTON_X, [this, fullPath, name, isDir](brls::View*) {
                showFileActionsDialog(fullPath, name, isDir);
                return true;
            });

            boxFiles->addView(cell);
            count++;
        }
        closedir(dir);

        if (lblItemCount) lblItemCount->setText(std::to_string(count) + " elementos");
    }

    void showFileProperties(const std::string& fullPath, const std::string& name, u64 sz) {
        char pBuf[256];
        snprintf(pBuf, sizeof(pBuf), "Ruta: %s\nTamano: %llu bytes", fullPath.c_str(), (unsigned long long)sz);
        brls::Dialog* prop = new brls::Dialog(pBuf);
        prop->addButton("Aceptar", []() {});
        prop->open();
    }

    void showFileActionsDialog(const std::string& fullPath, const std::string& name, bool isDir) {
        brls::Dialog* d = new brls::Dialog(name);
        d->addButton("Copiar", [this, fullPath]() {
            g_clipboardPath = fullPath;
            g_clipboardIsCut = false;
            refreshList();
        });
        d->addButton("Cortar", [this, fullPath]() {
            g_clipboardPath = fullPath;
            g_clipboardIsCut = true;
            refreshList();
        });
        d->addButton("Renombrar", [this, fullPath, name]() {
            brls::sync([this, fullPath, name]() {
                std::string newName = showHorizonKeyboard("Nuevo nombre de archivo", name);
                if (!newName.empty() && newName != name) {
                    std::string newPath = currentPath + (currentPath.back() == '/' ? "" : "/") + newName;
                    rename(fullPath.c_str(), newPath.c_str());
                    refreshList();
                }
            });
        });
        d->addButton("Eliminar", [this, fullPath, name, isDir]() {
            brls::sync([this, fullPath, name, isDir]() {
                brls::Dialog* confirm = new brls::Dialog("Seguro que deseas eliminar '" + name + "'?");
                confirm->addButton("Eliminar", [this, fullPath, isDir]() {
                    if (isDir) {
                        mtp_ops::mtpDelRec(fullPath);
                    } else {
                        unlink(fullPath.c_str());
                    }
                    refreshList();
                });
                confirm->addButton("Cancelar", []() {});
                confirm->open();
            });
        });
        d->addButton("Propiedades", [this, fullPath, name]() {
            brls::sync([this, fullPath, name]() {
                struct stat st;
                u64 sz = 0;
                if (stat(fullPath.c_str(), &st) == 0) sz = st.st_size;
                showFileProperties(fullPath, name, sz);
            });
        });
        d->addButton("Cancelar", []() {});
        d->open();
    }

private:
    std::string currentPath;
};

// --- Tab 2: USB MTP Responder ---
class MtpTab : public brls::Box {
public:
    MtpTab() {
        this->inflateFromXMLFile("romfs:/xml/view_mtp.xml");

        btnToggleMtp = dynamic_cast<brls::Button*>(this->getView("btnToggleMtp"));
        lblMtpStatus = dynamic_cast<brls::Label*>(this->getView("lblMtpStatus"));
        lblMtpInfo = dynamic_cast<brls::Label*>(this->getView("lblMtpInfo"));

        if (btnToggleMtp) {
            btnToggleMtp->registerClickAction([this](brls::View*) {
                if (mtp_ops::running()) {
                    mtp_ops::stop();
                    mtp_usb::teardown();
                    updateUIState(false);
                } else {
                    mtp_usb::setup();
                    mtp_ops::start();
                    updateUIState(true);
                }
                return true;
            });
        }

        updateUIState(mtp_ops::running());

        // Tarea periodica de actualizacion de telemetria (4 Hz)
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
                lblMtpStatus->setText("Estado: Servidor Activo (0x057E:0x201D)");
                lblMtpStatus->setTextColor(nvgRGB(16, 185, 129));
            } else {
                lblMtpStatus->setText("Estado: Detenido / En Espera");
                lblMtpStatus->setTextColor(nvgRGB(156, 163, 175));
            }
        }
        if (btnToggleMtp) {
            btnToggleMtp->setText(running ? "Detener MTP" : "Activar MTP");
        }
    }

    void updateTelemetry() {
        bool running = mtp_ops::running();
        updateUIState(running);

        if (lblMtpInfo) {
            if (mtp_ops::g_telemetry.active) {
                char info[256];
                float pct = (mtp_ops::g_telemetry.totalBytes > 0 && mtp_ops::g_telemetry.totalBytes != 0xFFFFFFFFFFFFFFFFULL) ?
                    (float)((double)mtp_ops::g_telemetry.transferredBytes / (double)mtp_ops::g_telemetry.totalBytes * 100.0) : 0.0f;
                snprintf(info, sizeof(info), "%s: %s (%.1f%%) a %.1f MB/s",
                    mtp_ops::g_telemetry.isUpload ? "Recibiendo" : "Enviando",
                    mtp_ops::g_telemetry.filename, pct, (float)mtp_ops::g_telemetry.speedMBs);
                lblMtpInfo->setText(info);
                lblMtpInfo->setTextColor(nvgRGB(6, 182, 212));
            } else {
                lblMtpInfo->setText("8 Particiones: SD, User, System, Juegos, Install SD/NAND, Saves, Album");
                lblMtpInfo->setTextColor(nvgRGB(156, 163, 175));
            }
        }
    }

private:
    brls::RepeatingTimer updateTimer;
    brls::Button* btnToggleMtp = nullptr;
    brls::Label* lblMtpStatus = nullptr;
    brls::Label* lblMtpInfo = nullptr;
};

// --- Tab 3: Servidor FTP ---
class FtpTab : public brls::Box {
public:
    FtpTab() {
        this->inflateFromXMLFile("romfs:/xml/view_ftp.xml");

        btnToggleFtp = dynamic_cast<brls::Button*>(this->getView("btnToggleFtp"));
        lblFtpStatus = dynamic_cast<brls::Label*>(this->getView("lblFtpStatus"));
        lblFtpAddress = dynamic_cast<brls::Label*>(this->getView("lblFtpAddress"));

        if (btnToggleFtp) {
            btnToggleFtp->registerClickAction([this](brls::View*) {
                if (ftp_server::running()) {
                    ftp_server::stop();
                    updateFtpState(false);
                } else {
                    ftp_server::start();
                    updateFtpState(true);
                }
                return true;
            });
        }
        updateFtpState(ftp_server::running());
    }

    static brls::View* create() {
        return new FtpTab();
    }

    void updateFtpState(bool running) {
        if (lblFtpStatus) {
            lblFtpStatus->setText(running ? "Estado: Activo" : "Estado: Desactivado");
            lblFtpStatus->setTextColor(running ? nvgRGB(139, 92, 246) : nvgRGB(156, 163, 175));
        }
        if (lblFtpAddress) {
            std::string ip = ftp_server::getRealIp();
            lblFtpAddress->setText(running ? ("Direccion: ftp://" + ip + ":5000") : "Direccion: Desconectado");
        }
        if (btnToggleFtp) {
            btnToggleFtp->setText(running ? "Detener FTP" : "Iniciar FTP");
        }
    }

private:
    brls::Button* btnToggleFtp = nullptr;
    brls::Label* lblFtpStatus = nullptr;
    brls::Label* lblFtpAddress = nullptr;
};

// --- Tab 4: Ajustes ---
class SettingsTab : public brls::Box {
public:
    SettingsTab() {
        this->inflateFromXMLFile("romfs:/xml/view_settings.xml");

        cellTheme = dynamic_cast<brls::DetailCell*>(this->getView("cellTheme"));
        cellLanguage = dynamic_cast<brls::DetailCell*>(this->getView("cellLanguage"));
        cellFtpPort = dynamic_cast<brls::DetailCell*>(this->getView("cellFtpPort"));
        cellMtpPartitions = dynamic_cast<brls::DetailCell*>(this->getView("cellMtpPartitions"));

        if (cellTheme) {
            bool isDark = (brls::Application::getPlatform()->getThemeVariant() == brls::ThemeVariant::DARK);
            cellTheme->setDetailText(isDark ? "Oscuro (Predeterminado)" : "Claro");
            cellTheme->registerClickAction([this](brls::View*) {
                bool isDark = (brls::Application::getPlatform()->getThemeVariant() == brls::ThemeVariant::DARK);
                brls::Application::getPlatform()->setThemeVariant(isDark ? brls::ThemeVariant::LIGHT : brls::ThemeVariant::DARK);
                cellTheme->setDetailText(isDark ? "Claro" : "Oscuro (Predeterminado)");
                return true;
            });
        }

        if (cellLanguage) {
            cellLanguage->setDetailText("Espanol");
            cellLanguage->registerClickAction([this](brls::View*) {
                brls::Dialog* d = new brls::Dialog("Seleccionar idioma");
                d->addButton("Espanol (Predeterminado)", [this]() {
                    cellLanguage->setDetailText("Espanol");
                });
                d->addButton("English", [this]() {
                    cellLanguage->setDetailText("English");
                });
                d->open();
                return true;
            });
        }

        if (cellFtpPort) {
            cellFtpPort->setDetailText("5000");
        }

        if (cellMtpPartitions) {
            cellMtpPartitions->setDetailText("8 Unidades (Estilo DBI)");
        }
    }

    static brls::View* create() {
        return new SettingsTab();
    }

private:
    brls::DetailCell* cellTheme = nullptr;
    brls::DetailCell* cellLanguage = nullptr;
    brls::DetailCell* cellFtpPort = nullptr;
    brls::DetailCell* cellMtpPartitions = nullptr;
};

// --- Tab 5: Acerca de ---
class AboutTab : public brls::Box {
public:
    AboutTab() {
        this->inflateFromXMLFile("romfs:/xml/view_about.xml");
    }

    static brls::View* create() {
        return new AboutTab();
    }
};

// --- Actividad Principal ---
class MainActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_FILE("romfs:/xml/main_tabs.xml");
};

// --- Pantalla de Inicio / Splash Screen ---
class SplashActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_FILE("romfs:/xml/view_splash.xml");

    SplashActivity() {
        // Carga opcional de logotipo personalizado desde la tarjeta SD si existe
        if (access("sdmc:/switch/EzFiles/logo.png", F_OK) == 0) {
            brls::Image* img = dynamic_cast<brls::Image*>(this->getView("splashLogo"));
            if (img) img->setImageFromFile("sdmc:/switch/EzFiles/logo.png");
        } else if (access("sdmc:/switch/EzFiles/splash.png", F_OK) == 0) {
            brls::Image* img = dynamic_cast<brls::Image*>(this->getView("splashLogo"));
            if (img) img->setImageFromFile("sdmc:/switch/EzFiles/splash.png");
        }

        splashTimer.setCallback([this]() {
            splashTimer.stop();
            brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::FADE);
        });
        splashTimer.start(1200); // 1.2 segundos con transicion suave
    }

private:
    brls::RepeatingTimer splashTimer;
};

int main(int argc, char* argv[]) {
    // Configurar bitacora persistente en SD para diagnostico
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/EzFiles", 0777);
    FILE* logf = fopen("sdmc:/switch/EzFiles/ezfiles.log", "w");
    if (logf) {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        brls::Logger::setLogOutput(logf);
        brls::Logger::info("EzFiles Borealis iniciando...");
    }

    // Configurar idioma predeterminado al espanol
    brls::Platform::APP_LOCALE_DEFAULT = "es-419";

    if (!brls::Application::init()) {
        brls::Logger::error("No se pudo inicializar Borealis");
        if (logf) fclose(logf);
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("EzFiles");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    // Salir de la aplicacion limpiamente con boton '+' (START)
    brls::Application::setGlobalQuit(true);

    // Registrar vistas personalizadas del XML
    brls::Application::registerXMLView("ExplorerTab", ExplorerTab::create);
    brls::Application::registerXMLView("MtpTab", MtpTab::create);
    brls::Application::registerXMLView("FtpTab", FtpTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);
    brls::Application::registerXMLView("AboutTab", AboutTab::create);

    try {
        brls::Application::pushActivity(new SplashActivity());
        while (brls::Application::mainLoop());
    } catch (const std::exception& e) {
        brls::Logger::error("Excepcion interceptada: {}", e.what());
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
        brls::Logger::info("EzFiles cerrado correctamente.");
        fclose(logf);
    }

    return EXIT_SUCCESS;
}
