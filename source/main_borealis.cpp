#include <switch.h>
#include <borealis.hpp>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>

#include "mtp_usb.hpp"
#include "mtp_ops.hpp"
#include "ftp_server.hpp"
#include "config.hpp"

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
            }
            boxFiles->addView(cell);
            count++;
        }
        closedir(dir);

        if (lblItemCount) lblItemCount->setText(std::to_string(count) + " elementos");
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

        // Tarea periódica de actualización de telemetría
        updateTimer.setCallback([this]() {
            updateTelemetry();
        });
        updateTimer.start(250); // 4 Hz
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
                lblMtpInfo->setText("5 Particiones: MicroSD, Álbum, Instalador, NAND, Juegos");
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
            lblFtpAddress->setText(running ? ("Dirección: ftp://" + ip + ":5000") : "Dirección: Desconectado");
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

// --- Actividad Principal ---
class MainActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_FILE("romfs:/xml/main_tabs.xml");
};

int main(int argc, char* argv[]) {
    // Configurar bitácora persistente en SD para diagnóstico
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/EzFiles", 0777);
    FILE* logf = fopen("sdmc:/switch/EzFiles/ezfiles.log", "w");
    if (logf) {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        brls::Logger::setLogOutput(logf);
        brls::Logger::info("EzFiles Borealis iniciando...");
    }

    if (!brls::Application::init()) {
        brls::Logger::error("No se pudo inicializar Borealis");
        if (logf) fclose(logf);
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("EzFiles");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    // Salir de la aplicación limpiamente con botón '+' (START)
    brls::Application::setGlobalQuit(true);

    // Registrar vistas personalizadas del XML
    brls::Application::registerXMLView("ExplorerTab", ExplorerTab::create);
    brls::Application::registerXMLView("MtpTab", MtpTab::create);
    brls::Application::registerXMLView("FtpTab", FtpTab::create);

    try {
        brls::Application::pushActivity(new MainActivity());
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
        brls::Logger::info("EzFiles cerrado correctamente.");
        fclose(logf);
    }

    return EXIT_SUCCESS;
}
