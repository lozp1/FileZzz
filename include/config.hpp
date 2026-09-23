#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

struct AppConfig {
    // MTP settings
    bool mtpShowSD = true;
    bool mtpShowAlbum = true;
    bool mtpEnableInstaller = true;
    bool mtpShowNANDUser = true;

    // Installer settings
    std::string installerTarget = "SDCard"; // "SDCard" o "NAND"
    bool autoDeleteInstallers = false;

    // FTP settings
    int ftpPort = 5000;
    bool ftpAnonLogin = true;

    // UI
    std::string language = "es";
    std::string theme = "Dark";

    static AppConfig& get() {
        static AppConfig s_instance;
        return s_instance;
    }

    void load(const std::string& path = "sdmc:/switch/EzFiles/config.ini") {
        std::ifstream file(path);
        if (!file.is_open()) {
            save(path); // Guardar configuración predeterminada si no existe
            return;
        }

        std::string line;
        std::string currentSection = "";
        while (std::getline(file, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            // Trim key & val
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            size_t vStart = val.find_first_not_of(" \t\r\n");
            if (vStart != std::string::npos) val = val.substr(vStart);
            val.erase(val.find_last_not_of(" \t\r\n") + 1);

            auto toBool = [](const std::string& v) {
                return (v == "1" || v == "true" || v == "True" || v == "yes" || v == "on");
            };

            if (currentSection == "MTP") {
                if (key == "ShowSD") mtpShowSD = toBool(val);
                else if (key == "ShowAlbum") mtpShowAlbum = toBool(val);
                else if (key == "EnableInstaller") mtpEnableInstaller = toBool(val);
                else if (key == "ShowNANDUser") mtpShowNANDUser = toBool(val);
            } else if (currentSection == "Installer") {
                if (key == "TargetStorage") installerTarget = val;
                else if (key == "AutoDeleteAfterInstall") autoDeleteInstallers = toBool(val);
            } else if (currentSection == "FTP") {
                if (key == "Port") ftpPort = std::stoi(val);
                else if (key == "AnonLogin") ftpAnonLogin = toBool(val);
            } else if (currentSection == "UI") {
                if (key == "Language") language = val;
                else if (key == "Theme") theme = val;
            }
        }
    }

    void save(const std::string& path = "sdmc:/switch/EzFiles/config.ini") {
        mkdir("sdmc:/switch", 0777);
        mkdir("sdmc:/switch/EzFiles", 0777);

        std::ofstream file(path);
        if (!file.is_open()) return;

        file << "; EzFiles Configuration File\n\n";

        file << "[MTP]\n";
        file << "ShowSD = " << (mtpShowSD ? "true" : "false") << "\n";
        file << "ShowAlbum = " << (mtpShowAlbum ? "true" : "false") << "\n";
        file << "EnableInstaller = " << (mtpEnableInstaller ? "true" : "false") << "\n";
        file << "ShowNANDUser = " << (mtpShowNANDUser ? "true" : "false") << "\n\n";

        file << "[Installer]\n";
        file << "TargetStorage = " << installerTarget << "\n";
        file << "AutoDeleteAfterInstall = " << (autoDeleteInstallers ? "true" : "false") << "\n\n";

        file << "[FTP]\n";
        file << "Port = " << ftpPort << "\n";
        file << "AnonLogin = " << (ftpAnonLogin ? "true" : "false") << "\n\n";

        file << "[UI]\n";
        file << "Language = " << language << "\n";
        file << "Theme = " << theme << "\n";
    }
};
