#pragma once
// Bitácora circular thread-safe estilo DBI para las pantallas FTP/MTP.
// El servidor FTP corre en hilos aparte: todo acceso va con mutex.
#include <string>
#include <vector>
#include <mutex>
#include <ctime>
#include <cstdio>

namespace logcon {

inline std::mutex g_mtx;
inline std::vector<std::string> g_lines;
inline const size_t MAX_LINES = 80;

inline void push(const std::string& msg) {
    char ts[16] = "--:--:--";
    {
        // El lock cubre también localtime (buffer estático, no thread-safe).
        std::lock_guard<std::mutex> lk(g_mtx);
        time_t rt;
        time(&rt);
        struct tm* ti = localtime(&rt);
        if (ti) snprintf(ts, sizeof(ts), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
        g_lines.push_back(std::string("[") + ts + "] " + msg);
        if (g_lines.size() > MAX_LINES)
            g_lines.erase(g_lines.begin(), g_lines.begin() + (g_lines.size() - MAX_LINES));
    }
}

inline std::vector<std::string> last(size_t n) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_lines.empty() || n == 0) return {};
    size_t from = g_lines.size() > n ? g_lines.size() - n : 0;
    return std::vector<std::string>(g_lines.begin() + from, g_lines.end());
}

inline void clear() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_lines.clear();
}

inline size_t count() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_lines.size();
}

} // namespace logcon
