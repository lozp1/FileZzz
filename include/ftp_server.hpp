#pragma once
#include <switch.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <fstream>
#include <iostream>

#include <cerrno>
#include <sys/select.h>
#include "config.hpp"
#include "sys_clock.hpp"

namespace ftp_server {

inline std::atomic<bool> g_running(false);
inline bool running() { return g_running.load(); }
inline std::thread g_serverThread;
inline int g_listenPort = 5000;
inline int g_serverSock = -1;
inline std::string g_currentIp = "0.0.0.0";
// Credenciales FTP = nickname del perfil de la consola (usuario y clave iguales).
// Se fijan al arrancar en main.cpp; fallback "switch" si no hay perfil.
inline std::string g_ftpUser = "switch";
inline std::string g_ftpPass = "switch";

// Callback opcional de bitácora (lo fija main.cpp hacia logcon::push).
// Puntero a función para no depender de <functional> con -fno-exceptions.
inline void (*g_logCb)(const char* msg) = nullptr;
inline void flog(const std::string& s) {
    if (g_logCb) g_logCb(s.c_str());
}
inline std::string peerIpStr(int sock) {
    struct sockaddr_in a;
    socklen_t l = sizeof(a);
    memset(&a, 0, sizeof(a));
    if (getpeername(sock, (struct sockaddr*)&a, &l) != 0) return "?";
    char ipbuf[INET_ADDRSTRLEN] = "?";
    if (!inet_ntop(AF_INET, &a.sin_addr, ipbuf, sizeof(ipbuf))) return "?";
    return std::string(ipbuf);
}

// accept con timeout: evita hilos/joins colgados si el cliente no abre datos.
inline int acceptTimeout(int listenSock, int timeoutMs) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(listenSock, &rfds);
    struct timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int r = select(listenSock + 1, &rfds, nullptr, nullptr, &tv);
    if (r <= 0 || !FD_ISSET(listenSock, &rfds)) return -1;
    struct sockaddr_in a;
    socklen_t l = sizeof(a);
    return accept(listenSock, (struct sockaddr*)&a, &l);
}

inline std::string getRealIp() {
    u32 ip = 0;
    if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip != 0) {
        struct in_addr addr;
        addr.s_addr = ip;
        char* str = inet_ntoa(addr);
        if (str) return std::string(str);
    }
    return "0.0.0.0";
}

inline void sendResponse(int clientSock, const std::string& msg) {
    std::string data = msg + "\r\n";
    send(clientSock, data.c_str(), data.length(), 0);
}

// Convierte ruta virtual FTP (ej: "/switch") a ruta nativa libnx ("sdmc:/switch")
inline std::string toNativePath(const std::string& ftpPath) {
    if (ftpPath.empty() || ftpPath == "/") return "sdmc:/";
    if (ftpPath.rfind("sdmc:", 0) == 0) return ftpPath;
    if (ftpPath[0] == '/') return "sdmc:" + ftpPath;
    return "sdmc:/" + ftpPath;
}

// Normaliza ruta virtual FTP, resolviendo "." y ".." sin escapar de "/"
inline std::string normalizeFtpPath(const std::string& cwd, const std::string& arg) {
    std::string base;
    if (arg.empty()) return cwd.empty() ? "/" : cwd;
    if (arg[0] == '/') base = arg;
    else {
        base = cwd;
        if (base.empty()) base = "/";
        if (base.back() != '/') base += "/";
        base += arg;
    }
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i <= base.size(); i++) {
        char c = (i < base.size()) ? base[i] : '/';
        if (c == '/') {
            if (cur.empty() || cur == ".") { cur.clear(); continue; }
            if (cur == "..") { if (!parts.empty()) parts.pop_back(); cur.clear(); continue; }
            parts.push_back(cur); cur.clear();
        } else cur += c;
    }
    std::string out = "/";
    for (size_t i = 0; i < parts.size(); i++) { if (i) out += "/"; out += parts[i]; }
    // Mantener "/" raíz, resto sin slash final (el llamador añade si es dir)
    return out;
}

inline std::string resolveNative(const std::string& cwd, const std::string& arg) {
    return toNativePath(normalizeFtpPath(cwd, arg));
}

inline void handleClient(int clientSock, u32 localIpU32) {
    std::string currentFtpDir = "/";
    int pasvSock = -1;
    int pasvPort = 0;
    std::string renameFromPath = "";
    std::string recvBuffer = "";
    std::string providedUser = "";
    bool loggedIn = false;

    struct timeval rtv{1, 0};
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rtv, sizeof(rtv));

    flog("FTP cliente conectado (" + peerIpStr(clientSock) + ")");
    sendResponse(clientSock, "220 FileZzz FTP Server Ready");

    char buf[2048];
    while (g_running) {
        memset(buf, 0, sizeof(buf));
        int n = recv(clientSock, buf, sizeof(buf) - 1, 0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue; // timeout: re-chequear g_running
        if (n <= 0) break;

        recvBuffer.append(buf, n);

        // Procesar línea por línea
        size_t pos = 0;
        while ((pos = recvBuffer.find('\n')) != std::string::npos) {
            std::string line = recvBuffer.substr(0, pos);
            recvBuffer.erase(0, pos + 1);

            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty()) continue;

            size_t sp = line.find(' ');
            std::string cmd = (sp == std::string::npos) ? line : line.substr(0, sp);
            std::string arg = (sp == std::string::npos) ? "" : line.substr(sp + 1);
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

            bool preAuth = (cmd == "USER" || cmd == "PASS" || cmd == "QUIT" || cmd == "NOOP" ||
                            cmd == "FEAT" || cmd == "OPTS" || cmd == "SYST" || cmd == "AUTH");
            if (!loggedIn && !preAuth && cmd.rfind("AUTH", 0) != 0) {
                sendResponse(clientSock, "530 Please login with USER and PASS.");
                continue;
            }

            if (cmd == "AUTH" || cmd.rfind("AUTH", 0) == 0) {
                sendResponse(clientSock, "502 Explicit TLS not available. Use Plain FTP.");
            } else if (cmd == "USER") {
                providedUser = arg;
                sendResponse(clientSock, "331 User name okay, need password.");
            } else if (cmd == "PASS") {
                std::string expUser = !AppConfig::get().ftpUsername.empty() ? AppConfig::get().ftpUsername : g_ftpUser;
                std::string expPass = !AppConfig::get().ftpPassword.empty() ? AppConfig::get().ftpPassword : g_ftpPass;
                bool allowAnon = AppConfig::get().ftpAnonLogin && (providedUser == "anonymous" || providedUser.empty());
                bool matchCreds = (!providedUser.empty() && providedUser == expUser && (expPass.empty() || arg == expPass));

                if (allowAnon || matchCreds) {
                    loggedIn = true;
                    flog("FTP login OK (" + providedUser + ")");
                    sendResponse(clientSock, "230 User logged in, proceed.");
                } else {
                    flog("FTP login ERR (" + providedUser + ")");
                    sendResponse(clientSock, "530 Login incorrect.");
                }
            } else if (cmd == "SYST") {
                sendResponse(clientSock, "215 UNIX Type: L8");
            } else if (cmd == "FEAT") {
                sendResponse(clientSock, "211-Features:\r\n UTF8\r\n EPSV\r\n PASV\r\n211 End");
            } else if (cmd == "OPTS") {
                sendResponse(clientSock, "200 OPTS accepted.");
            } else if (cmd == "PWD") {
                // FileZilla requiere el path relativo "/" sin prefijo de dispositivo
                sendResponse(clientSock, "257 \"" + currentFtpDir + "\" is current directory.");
            } else if (cmd == "TYPE") {
                sendResponse(clientSock, "200 Type set to " + arg);
            } else if (cmd == "PASV") {
                if (pasvSock >= 0) { close(pasvSock); pasvSock = -1; }
                pasvSock = socket(AF_INET, SOCK_STREAM, 0);
                if (pasvSock >= 0) {
                    struct sockaddr_in pAddr;
                    memset(&pAddr, 0, sizeof(pAddr));
                    pAddr.sin_family = AF_INET;
                    pAddr.sin_addr.s_addr = INADDR_ANY;
                    pAddr.sin_port = 0;
                    bind(pasvSock, (struct sockaddr*)&pAddr, sizeof(pAddr));
                    listen(pasvSock, 1);

                    socklen_t pLen = sizeof(pAddr);
                    getsockname(pasvSock, (struct sockaddr*)&pAddr, &pLen);
                    pasvPort = ntohs(pAddr.sin_port);

                    u8 ip1 = localIpU32 & 0xFF;
                    u8 ip2 = (localIpU32 >> 8) & 0xFF;
                    u8 ip3 = (localIpU32 >> 16) & 0xFF;
                    u8 ip4 = (localIpU32 >> 24) & 0xFF;
                    int p1 = pasvPort / 256;
                    int p2 = pasvPort % 256;

                    char reply[128];
                    snprintf(reply, sizeof(reply), "227 Entering Passive Mode (%u,%u,%u,%u,%d,%d)", ip1, ip2, ip3, ip4, p1, p2);
                    sendResponse(clientSock, reply);
                } else {
                    sendResponse(clientSock, "425 Can't open passive connection");
                }
            } else if (cmd == "EPSV") {
                // Extended Passive Mode (preferido por FileZilla)
                if (pasvSock >= 0) { close(pasvSock); pasvSock = -1; }
                pasvSock = socket(AF_INET, SOCK_STREAM, 0);
                if (pasvSock >= 0) {
                    struct sockaddr_in pAddr;
                    memset(&pAddr, 0, sizeof(pAddr));
                    pAddr.sin_family = AF_INET;
                    pAddr.sin_addr.s_addr = INADDR_ANY;
                    pAddr.sin_port = 0;
                    bind(pasvSock, (struct sockaddr*)&pAddr, sizeof(pAddr));
                    listen(pasvSock, 1);

                    socklen_t pLen = sizeof(pAddr);
                    getsockname(pasvSock, (struct sockaddr*)&pAddr, &pLen);
                    pasvPort = ntohs(pAddr.sin_port);

                    char reply[64];
                    snprintf(reply, sizeof(reply), "229 Entering Extended Passive Mode (|||%d|)", pasvPort);
                    sendResponse(clientSock, reply);
                } else {
                    sendResponse(clientSock, "425 Can't open passive connection");
                }
            } else if (cmd == "LIST" || cmd == "NLST" || cmd == "MLSD") {
                int dataSock = -1;
                if (pasvSock >= 0) {
                    sendResponse(clientSock, "150 Here comes the directory listing.");
                    dataSock = acceptTimeout(pasvSock, 10000);
                    close(pasvSock);
                    pasvSock = -1;
                }

                if (dataSock >= 0) {
                    std::string listArg = arg;
                    // LIST puede traer flags ("-la") o ruta; nos quedamos con el último token que no sea flag
                    std::string listVirt = currentFtpDir;
                    if (!listArg.empty() && listArg[0] != '-') {
                        // si trae ruta, resolverla
                        std::string token = listArg;
                        // quitar flags iniciales tipo "-l"
                        if (token.rfind("-", 0) == 0) token = "";
                        if (!token.empty()) listVirt = normalizeFtpPath(currentFtpDir, token);
                    }
                    std::string nativeDir = toNativePath(listVirt);
                    // Si apuntan a un archivo, listar solo ese
                    struct stat lst;
                    bool isFileTarget = (stat(nativeDir.c_str(), &lst) == 0 && !S_ISDIR(lst.st_mode));
                    if (isFileTarget) {
                        const char* bn = strrchr(nativeDir.c_str(), '/');
                        const char* nm = bn ? bn + 1 : nativeDir.c_str();
                        char lbuf[320];
                        char dstr[32] = "Jan 01 00:00";
                        struct tm* tmv = localtime(&lst.st_mtime);
                        if (tmv) strftime(dstr, sizeof(dstr), "%b %d %H:%M", tmv);
                        if (cmd == "NLST") snprintf(lbuf, sizeof(lbuf), "%s\r\n", nm);
                        else snprintf(lbuf, sizeof(lbuf), "-rwxr-xr-x 1 owner group %10lld %s %s\r\n",
                                      (long long)lst.st_size, dstr, nm);
                        send(dataSock, lbuf, strlen(lbuf), 0);
                    } else {
                        DIR* d = opendir(nativeDir.c_str());
                        if (d) {
                            struct dirent* de;
                            while ((de = readdir(d)) != NULL) {
                                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                                std::string fullP = nativeDir + (nativeDir.back() == '/' ? "" : "/") + de->d_name;
                                struct stat st;
                                bool isD = (de->d_type == DT_DIR);
                                long long sz = 0;
                                time_t mt = 0;
                                if (stat(fullP.c_str(), &st) == 0) {
                                    isD = S_ISDIR(st.st_mode);
                                    sz = (long long)st.st_size;
                                    mt = st.st_mtime;
                                }
                                char lbuf[320];
                                char dstr[32] = "Jan 01 00:00";
                                struct tm* tmv = localtime(&mt);
                                if (tmv) strftime(dstr, sizeof(dstr), "%b %d %H:%M", tmv);
                                if (cmd == "NLST") {
                                    snprintf(lbuf, sizeof(lbuf), "%s\r\n", de->d_name);
                                } else {
                                    snprintf(lbuf, sizeof(lbuf), "%crwxr-xr-x 1 owner group %10lld %s %s\r\n",
                                             isD ? 'd' : '-', sz, dstr, de->d_name);
                                }
                                send(dataSock, lbuf, strlen(lbuf), 0);
                            }
                            closedir(d);
                        }
                    }
                    close(dataSock);
                    sendResponse(clientSock, "226 Directory send OK.");
                } else {
                    sendResponse(clientSock, "425 No data connection");
                }
            } else if (cmd == "CWD") {
                std::string norm = normalizeFtpPath(currentFtpDir, arg.empty() ? "/" : arg);
                std::string nat = toNativePath(norm);
                DIR* td = opendir(nat.c_str());
                if (td) {
                    closedir(td);
                    currentFtpDir = norm;
                    if (currentFtpDir.back() != '/') currentFtpDir += "/";
                    sendResponse(clientSock, "250 Directory successfully changed.");
                } else {
                    flog("FTP CWD ERR " + norm);
                    sendResponse(clientSock, "550 Failed to change directory.");
                }
            } else if (cmd == "CDUP") {
                std::string norm = normalizeFtpPath(currentFtpDir, "..");
                currentFtpDir = norm;
                if (currentFtpDir.back() != '/') currentFtpDir += "/";
                sendResponse(clientSock, "250 Directory changed to " + currentFtpDir);
            } else if (cmd == "RETR") {
                int dataSock = -1;
                if (pasvSock >= 0) {
                    sendResponse(clientSock, "150 Opening binary mode data connection.");
                    dataSock = acceptTimeout(pasvSock, 10000);
                    close(pasvSock);
                    pasvSock = -1;
                }
                if (dataSock >= 0) {
                    std::string fpath = resolveNative(currentFtpDir, arg);
                    std::ifstream ifs(fpath, std::ios::binary);
                    if (ifs.is_open()) {
                        std::vector<char> fileBuf(32768);
                        long long total = 0;
                        while (ifs) {
                            ifs.read(fileBuf.data(), (std::streamsize)fileBuf.size());
                            std::streamsize got = ifs.gcount();
                            if (got > 0) { send(dataSock, fileBuf.data(), (size_t)got, 0); total += got; }
                        }
                        ifs.close();
                        close(dataSock);
                        flog("FTP RETR OK " + arg + " (" + std::to_string(total) + " B)");
                        sendResponse(clientSock, "226 Transfer complete.");
                    } else {
                        close(dataSock);
                        flog("FTP RETR ERR " + arg);
                        sendResponse(clientSock, "550 Failed to open file.");
                    }
                } else {
                    sendResponse(clientSock, "425 No data connection");
                }
            } else if (cmd == "STOR") {
                int dataSock = -1;
                if (pasvSock >= 0) {
                    sendResponse(clientSock, "150 Opening binary mode data connection.");
                    dataSock = acceptTimeout(pasvSock, 10000);
                    close(pasvSock);
                    pasvSock = -1;
                }
                if (dataSock >= 0) {
                    std::string fpath = resolveNative(currentFtpDir, arg);
                    std::ofstream ofs(fpath, std::ios::binary);
                    if (ofs.is_open()) {
                        std::vector<char> fileBuf(32768);
                        int r = 0;
                        long long total = 0;
                        while ((r = recv(dataSock, fileBuf.data(), (u32)fileBuf.size(), 0)) > 0) {
                            ofs.write(fileBuf.data(), r);
                            total += r;
                        }
                        ofs.close();
                        close(dataSock);
                        flog("FTP STOR OK " + arg + " (" + std::to_string(total) + " B)");
                        sendResponse(clientSock, "226 Transfer complete.");
                    } else {
                        close(dataSock);
                        flog("FTP STOR ERR " + arg);
                        sendResponse(clientSock, "550 Failed to write file.");
                    }
                } else {
                    sendResponse(clientSock, "425 No data connection");
                }
            } else if (cmd == "DELE") {
                std::string fpath = resolveNative(currentFtpDir, arg);
                bool ok = (remove(fpath.c_str()) == 0);
                flog(std::string("FTP DELE ") + (ok ? "OK " : "ERR ") + arg);
                sendResponse(clientSock, ok ? "250 File deleted." : "550 Delete failed.");
            } else if (cmd == "MKD") {
                std::string fpath = resolveNative(currentFtpDir, arg);
                bool ok = (mkdir(fpath.c_str(), 0777) == 0);
                flog(std::string("FTP MKD ") + (ok ? "OK " : "ERR ") + arg);
                sendResponse(clientSock, ok ? "257 Directory created." : "550 Create directory failed.");
            } else if (cmd == "RMD") {
                std::string fpath = resolveNative(currentFtpDir, arg);
                bool ok = (rmdir(fpath.c_str()) == 0);
                flog(std::string("FTP RMD ") + (ok ? "OK " : "ERR ") + arg);
                sendResponse(clientSock, ok ? "250 Directory removed." : "550 Remove directory failed.");
            } else if (cmd == "RNFR") {
                renameFromPath = resolveNative(currentFtpDir, arg);
                sendResponse(clientSock, "350 Ready for RNTO.");
            } else if (cmd == "RNTO") {
                std::string renameToPath = resolveNative(currentFtpDir, arg);
                bool ok = (!renameFromPath.empty() && rename(renameFromPath.c_str(), renameToPath.c_str()) == 0);
                flog(std::string("FTP RNTO ") + (ok ? "OK " : "ERR ") + arg);
                sendResponse(clientSock, ok ? "250 Rename successful." : "550 Rename failed.");
                renameFromPath = "";
            } else if (cmd == "SIZE") {
                std::string fpath = resolveNative(currentFtpDir, arg);
                struct stat st;
                if (stat(fpath.c_str(), &st) == 0) {
                    char sBuf[64];
                    snprintf(sBuf, sizeof(sBuf), "213 %lld", (long long)st.st_size);
                    sendResponse(clientSock, sBuf);
                } else {
                    sendResponse(clientSock, "550 Could not get file size.");
                }
            } else if (cmd == "NOOP") {
                sendResponse(clientSock, "200 OK.");
            } else if (cmd == "QUIT") {
                sendResponse(clientSock, "221 Goodbye.");
                break;
            } else {
                sendResponse(clientSock, "502 Command not implemented.");
            }
        }
    }

    if (pasvSock >= 0) close(pasvSock);
    close(clientSock);
    flog("FTP cliente desconectado");
}

inline void serverLoop() {
    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) return;
    g_serverSock = serverSock;

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = INADDR_ANY;
    servAddr.sin_port = htons(g_listenPort);

    if (bind(serverSock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        close(serverSock);
        g_serverSock = -1;
        return;
    }

    listen(serverSock, 4);
    int flags = fcntl(serverSock, F_GETFL, 0);
    if (flags >= 0) fcntl(serverSock, F_SETFL, flags | O_NONBLOCK);

    while (g_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(serverSock, &rfds);
        struct timeval stv{0, 500000};
        int r = select(serverSock + 1, &rfds, nullptr, nullptr, &stv);
        if (!g_running) break;
        if (r <= 0) continue;
        struct sockaddr_in clientAddr;
        socklen_t cLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (struct sockaddr*)&clientAddr, &cLen);
        if (clientSock >= 0) {
            // Un solo cliente de control a la vez (sin hilos detached):
            // elimina carreras entre sesiones y paradas limpias garantizadas.
            u32 rawIp = 0;
            nifmGetCurrentIpAddress(&rawIp);
            handleClient(clientSock, rawIp);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            svcSleepThread(50'000'000ULL);
        }
    }

    if (serverSock >= 0) close(serverSock);
    g_serverSock = -1;
}

inline bool start(int port = 5000) {
    if (g_running) return true;
    sys_clock::enableBoost();
    g_listenPort = port;
    g_currentIp = getRealIp();
    g_running = true;
    flog("FTP servidor iniciado :" + std::to_string(port) + " (" + g_currentIp + ")");
    g_serverThread = std::thread(serverLoop);
    return true;
}

inline void stop() {
    if (!g_running) return;
    g_running = false;
    flog("FTP servidor detenido");
    if (g_serverSock >= 0) {
        shutdown(g_serverSock, SHUT_RDWR);
        // serverLoop cierra el fd; cerramos aquí también para despertar el select
        close(g_serverSock);
        g_serverSock = -1;
    }
    if (g_serverThread.joinable()) {
        g_serverThread.join();
    }
    sys_clock::disableBoost();
}

} // namespace ftp_server
