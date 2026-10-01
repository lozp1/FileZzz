#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "pfs0.hpp"

namespace installer {

using ProgressCallback = std::function<void(u64 currentBytes, u64 totalBytes, float mbps, const std::string& currentFileName)>;

struct InstallResult {
    bool success = false;
    std::string error;
    u64 titleId = 0;
};

class NcmInstaller {
public:
    static InstallResult installFromNsp(const std::string& filePath, NcmStorageId targetStorage,
                                        const ProgressCallback& progress,
                                        std::atomic<bool>* cancelFlag = nullptr) {
        InstallResult res;
        FILE* f = fopen(filePath.c_str(), "rb");
        if (!f) {
            res.error = "No se pudo abrir el archivo NSP";
            return res;
        }

        std::vector<Pfs0File> files;
        std::string pfs0Err;
        if (!Pfs0Parser::parse(f, files, pfs0Err)) {
            fclose(f);
            res.error = "Error al parsear estructura PFS0: " + pfs0Err;
            return res;
        }

        Result rc = ncmInitialize();
        if (R_FAILED(rc)) {
            fclose(f);
            res.error = "Fallo al inicializar servicio NCM (0x" + toHex(rc) + ")";
            return res;
        }

        NcmContentStorage storage = {};
        rc = ncmOpenContentStorage(&storage, targetStorage);
        if (R_FAILED(rc)) {
            ncmExit();
            fclose(f);
            res.error = "Fallo al abrir ContentStorage destino (0x" + toHex(rc) + ")";
            return res;
        }

        NcmContentMetaDatabase metaDb = {};
        rc = ncmOpenContentMetaDatabase(&metaDb, targetStorage);
        if (R_FAILED(rc)) {
            ncmContentStorageClose(&storage);
            ncmExit();
            fclose(f);
            res.error = "Fallo al abrir ContentMetaDatabase (0x" + toHex(rc) + ")";
            return res;
        }

        u64 totalBytes = 0;
        for (const auto& pf : files) {
            if (pf.isNca()) totalBytes += pf.size;
        }

        u64 installedBytes = 0;
        const size_t CHUNK_SIZE = 1024 * 1024; // 1 MB chunk
        std::vector<u8> chunk(CHUNK_SIZE);

        auto startTime = std::chrono::steady_clock::now();
        auto lastSpeedTime = startTime;
        u64 bytesSinceLastSpeed = 0;
        float currentMbps = 0.0f;

        for (const auto& pf : files) {
            if (cancelFlag && cancelFlag->load()) {
                res.error = "Instalacion cancelada por el usuario";
                break;
            }

            if (!pf.isNca()) continue;

            NcmContentId contentId = {};
            if (!pf.getNcaId(contentId)) continue;

            bool exists = false;
            ncmContentStorageHas(&storage, &exists, &contentId);
            if (exists) {
                installedBytes += pf.size;
                if (progress) {
                    progress(installedBytes, totalBytes, currentMbps, pf.name);
                }
                continue;
            }

            NcmPlaceHolderId placeholderId = {};
            rc = ncmContentStorageGeneratePlaceHolderId(&storage, &placeholderId);
            if (R_FAILED(rc)) {
                res.error = "Error al generar placeholder (" + pf.name + "): 0x" + toHex(rc);
                break;
            }

            rc = ncmContentStorageCreatePlaceHolder(&storage, &contentId, &placeholderId, pf.size);
            if (R_FAILED(rc)) {
                res.error = "Error al crear placeholder (0x" + toHex(rc) + ")";
                break;
            }

            if (fseek(f, pf.offset, SEEK_SET) != 0) {
                ncmContentStorageDeletePlaceHolder(&storage, &placeholderId);
                res.error = "Error de posicionamiento en archivo: " + pf.name;
                break;
            }

            u64 ncaWritten = 0;
            bool ncaFailed = false;
            while (ncaWritten < pf.size) {
                if (cancelFlag && cancelFlag->load()) {
                    ncaFailed = true;
                    res.error = "Cancelado por el usuario";
                    break;
                }

                size_t toRead = (size_t)std::min((u64)CHUNK_SIZE, pf.size - ncaWritten);
                size_t bytesRead = fread(chunk.data(), 1, toRead, f);
                if (bytesRead != toRead) {
                    ncaFailed = true;
                    res.error = "Fallo de lectura en archivo " + pf.name;
                    break;
                }

                rc = ncmContentStorageWritePlaceHolder(&storage, &placeholderId, ncaWritten, chunk.data(), bytesRead);
                if (R_FAILED(rc)) {
                    ncaFailed = true;
                    res.error = "Error al escribir placeholder (0x" + toHex(rc) + ")";
                    break;
                }

                ncaWritten += bytesRead;
                installedBytes += bytesRead;
                bytesSinceLastSpeed += bytesRead;

                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSpeedTime).count();
                if (elapsed >= 500) {
                    currentMbps = (float)(bytesSinceLastSpeed / (1024.0 * 1024.0)) / ((float)elapsed / 1000.0f);
                    lastSpeedTime = now;
                    bytesSinceLastSpeed = 0;
                }

                if (progress) {
                    progress(installedBytes, totalBytes, currentMbps, pf.name);
                }
            }

            if (ncaFailed) {
                ncmContentStorageDeletePlaceHolder(&storage, &placeholderId);
                break;
            }

            rc = ncmContentStorageRegister(&storage, &contentId, &placeholderId);
            if (R_FAILED(rc)) {
                ncmContentStorageDeletePlaceHolder(&storage, &placeholderId);
                res.error = "Error al registrar NCA (" + pf.name + "): 0x" + toHex(rc);
                break;
            }

            ncmContentStorageDeletePlaceHolder(&storage, &placeholderId);
        }

        if (res.error.empty()) {
            ncmContentMetaDatabaseCommit(&metaDb);
            res.success = true;
        }

        ncmContentMetaDatabaseClose(&metaDb);
        ncmContentStorageClose(&storage);
        ncmExit();
        fclose(f);
        return res;
    }

private:
    static std::string toHex(Result r) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%X", (unsigned int)r);
        return buf;
    }
};

} // namespace installer
