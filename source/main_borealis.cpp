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
        this->inflateFromXMLRes("xml/view_explorer.xml");
        currentPath = "sdmc:/";
        refreshList();
    }

    static brls::View* create() {
        return new ExplorerTab();
    }

    void refreshList() {
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
    BRLS_BIND(brls::Label, lblCurrentPath, "lblCurrentPath");
    BRLS_BIND(brls::Label, lblItemCount, "lblItemCount");
    BRLS_BIND(brls::Box, boxFiles, "boxFiles");
};

// --- Tab 2: USB MTP Responder ---
class MtpTab : public brls::Box {
public:
    MtpTab() {
        this->inflateFromXMLRes("xml/view_mtp.xml");

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
    BRLS_BIND(brls::Label, lblMtpStatus, "lblMtpStatus");
    BRLS_BIND(brls::Label, lblMtpInfo, "lblMtpInfo");
    BRLS_BIND(brls::Button, btnToggleMtp, "btnToggleMtp");
};

// --- Tab 3: Servidor FTP ---
class FtpTab : public brls::Box {
public:
    FtpTab() {
        this->inflateFromXMLRes("xml/view_ftp.xml");

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
    BRLS_BIND(brls::Label, lblFtpStatus, "lblFtpStatus");
    BRLS_BIND(brls::Label, lblFtpAddress, "lblFtpAddress");
    BRLS_BIND(brls::Button, btnToggleFtp, "btnToggleFtp");
};

// --- Actividad Principal ---
class MainActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("xml/main_tabs.xml");
};

int main(int argc, char* argv[]) {
    // Inicializar servicios Horizon OS
    romfsInit();
    socketInitializeDefault();

    if (!brls::Application::init()) {
        brls::Logger::error("No se pudo inicializar Borealis");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("EzFiles");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    // Registrar vistas personalizadas del XML
    brls::Application::registerXMLView("ExplorerTab", ExplorerTab::create);
    brls::Application::registerXMLView("MtpTab", MtpTab::create);
    brls::Application::registerXMLView("FtpTab", FtpTab::create);

    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop());

    // Cierre limpio
    if (mtp_ops::running()) {
        mtp_ops::stop();
        mtp_usb::teardown();
    }
    if (ftp_server::running()) {
        ftp_server::stop();
    }

    socketExit();
    romfsExit();
    return EXIT_SUCCESS;
}
