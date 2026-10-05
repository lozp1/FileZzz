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
#include <memory>
#include "pfs0.hpp"

namespace installer {

using ProgressCallback = std::function<void(u64 currentBytes, u64 totalBytes, float mbps, const std::string& currentFileName)>;

struct InstallResult {
    bool success = false;
    std::string error;
    u64 titleId = 0;
};

#pragma pack(push, 1)
struct PackagedContentMetaHeader {
    u64 title_id;
    u32 version;
    u8 type;
    u8 reserved_platform;
    u16 extended_header_size;
    u16 content_count;
    u16 content_meta_count;
    u8 attributes;
    u8 reserved[3];
    u32 required_download_system_version;
    u32 reserved2;
};

struct PackagedContentInfo {
    u8 hash[0x20];
    NcmContentInfo content_info;
};
#pragma pack(pop)

struct ContentStorageRecord {
    NcmContentMetaKey metaRecord;
    u64 storageId;
};

class NcmInstaller {
public:
    static Result esImportTicket(const void* tik_buf, u64 tik_size, const void* cert_buf, u64 cert_size) {
        Service srv;
        Result rc = smGetService(&srv, "es");
        if (R_FAILED(rc)) return rc;
        rc = serviceDispatch(&srv, 1,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In, SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
            .buffers = { { tik_buf, tik_size }, { cert_buf, cert_size } });
        serviceClose(&srv);
        return rc;
    }

    static Result pushApplicationRecord(u64 title_id, NcmStorageId storage_id, const NcmContentMetaKey& key) {
        Result rc = nsInitialize();
        if (R_FAILED(rc)) return rc;

        Service ns_srv;
        if (hosversionBefore(3, 0, 0)) {
            Service* s = nsGetServiceSession_ApplicationManagerInterface();
            if (!s) {
                nsExit();
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            }
            ns_srv = *s;
        } else {
            rc = nsGetApplicationManagerInterface(&ns_srv);
            if (R_FAILED(rc)) {
                nsExit();
                return rc;
            }
        }

        u64 baseTitleId = title_id;
        if (key.type == NcmContentMetaType_Patch) {
            baseTitleId = title_id ^ 0x800;
        } else if (key.type == NcmContentMetaType_AddOnContent) {
            baseTitleId = (title_id ^ 0x1000) & ~0xFFFULL;
        }

        ContentStorageRecord record = {};
        record.metaRecord = key;
        record.storageId = (u64)storage_id;

        struct {
            u8 last_modified_event;
            u64 application_id;
        } in = { 3 /* NsApplicationRecordType_Installed */, baseTitleId };

        rc = serviceDispatchIn(&ns_srv, 16, in,
            .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
            .buffers = { { &record, sizeof(record) } });

        if (R_SUCCEEDED(rc)) {
            nsTouchApplication(baseTitleId);
        }

        if (!hosversionBefore(3, 0, 0)) {
            serviceClose(&ns_srv);
        }
        nsExit();
        return rc;
    }

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

        // 1. Importar ticket y certificado si existen en el NSP
        importTicketIfPresent(f, files);

        // 2. Inicializar NCM y abrir almacenamiento
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
        Pfs0File cnmtPf;
        bool hasCnmt = false;

        for (const auto& pf : files) {
            if (pf.isNca()) totalBytes += pf.size;
            if (pf.isCnmt()) {
                cnmtPf = pf;
                hasCnmt = true;
            }
        }

        // Si no encontramos un .cnmt explícito en el nombre, buscar cualquier NCA pequeño que pueda serlo
        if (!hasCnmt) {
            for (const auto& pf : files) {
                if (pf.isNca() && pf.size < 1024 * 1024) { // Menor a 1 MB suele ser el meta
                    cnmtPf = pf;
                    hasCnmt = true;
                    break;
                }
            }
        }

        if (!hasCnmt) {
            ncmContentMetaDatabaseClose(&metaDb);
            ncmContentStorageClose(&storage);
            ncmExit();
            fclose(f);
            res.error = "El archivo NSP no contiene metadatos validos (.cnmt.nca)";
            return res;
        }

        u64 installedBytes = 0;
        const size_t CHUNK_SIZE = 1024 * 1024; // 1 MB chunk
        std::vector<u8> chunk(CHUNK_SIZE);

        auto startTime = std::chrono::steady_clock::now();
        auto lastSpeedTime = startTime;
        u64 bytesSinceLastSpeed = 0;
        float currentMbps = 0.0f;

        // Lista de NCAs recién escritos para rollback automático si algo falla
        std::vector<NcmContentId> newlyWrittenNcas;

        // 3. Escribir y registrar todos los NCA
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

            // Si por alguna razón el placeholder ya existía, eliminarlo antes
            bool hasPh = false;
            if (R_SUCCEEDED(ncmContentStorageHasPlaceHolder(&storage, &hasPh, &placeholderId)) && hasPh) {
                ncmContentStorageDeletePlaceHolder(&storage, &placeholderId);
            }

            rc = ncmContentStorageCreatePlaceHolder(&storage, &contentId, &placeholderId, pf.size);
            if (R_FAILED(rc)) {
                res.error = "Error al crear placeholder (" + pf.name + "): 0x" + toHex(rc);
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
            newlyWrittenNcas.push_back(contentId);
        }

        // 4. Registro del CNMT y del registro de la aplicación en el Menú HOME
        if (res.error.empty()) {
            NcmContentId cnmtId = {};
            if (cnmtPf.getNcaId(cnmtId)) {
                rc = registerCnmtAndAppRecord(storage, metaDb, targetStorage, cnmtId, cnmtPf.size, res.titleId);
                if (R_FAILED(rc)) {
                    res.error = "Error al registrar metadatos / Menu HOME: 0x" + toHex(rc);
                } else {
                    res.success = true;
                }
            } else {
                res.error = "No se pudo obtener el ID del CNMT NCA";
            }
        }

        // 5. ROLLBACK AUTOMÁTICO SI OCURRIÓ CUALQUIER FALLO
        // Si no se pudo completar la instalación o registrar el icono, eliminamos
        // de inmediato los archivos NCA recién escritos para no dejar basura ni huérfanos.
        if (!res.success) {
            for (const auto& nid : newlyWrittenNcas) {
                ncmContentStorageDelete(&storage, &nid);
            }
        }

        ncmContentMetaDatabaseClose(&metaDb);
        ncmContentStorageClose(&storage);
        ncmExit();
        fclose(f);
        return res;
    }

private:
    static void importTicketIfPresent(FILE* f, const std::vector<Pfs0File>& files) {
        const Pfs0File* tikFile = nullptr;
        const Pfs0File* certFile = nullptr;

        for (const auto& pf : files) {
            if (pf.isTicket()) tikFile = &pf;
            else if (pf.isCert()) certFile = &pf;
        }

        if (tikFile && certFile && tikFile->size > 0 && certFile->size > 0) {
            std::vector<u8> tikData(tikFile->size);
            std::vector<u8> certData(certFile->size);

            if (fseek(f, tikFile->offset, SEEK_SET) == 0 &&
                fread(tikData.data(), 1, tikFile->size, f) == tikFile->size &&
                fseek(f, certFile->offset, SEEK_SET) == 0 &&
                fread(certData.data(), 1, certFile->size, f) == certFile->size) {
                esImportTicket(tikData.data(), tikData.size(), certData.data(), certData.size());
            }
        }
    }

    static Result registerCnmtAndAppRecord(NcmContentStorage& storage, NcmContentMetaDatabase& metaDb,
                                           NcmStorageId targetStorage, const NcmContentId& cnmtId,
                                           u64 cnmtNcaSize, u64& outTitleId) {
        char cnmtPath[FS_MAX_PATH] = {0};
        Result rc = ncmContentStorageGetPath(&storage, cnmtPath, sizeof(cnmtPath), &cnmtId);
        if (R_FAILED(rc)) return rc;

        FsFileSystem cnmtFs = {};
        rc = fsOpenFileSystemWithId(&cnmtFs, 0, FsFileSystemType_ContentMeta, cnmtPath, FsContentAttributes_None);
        if (R_FAILED(rc)) return rc;

        FsDir dir = {};
        rc = fsFsOpenDirectory(&cnmtFs, "/", FsDirOpenMode_ReadFiles, &dir);
        if (R_FAILED(rc)) {
            fsFsClose(&cnmtFs);
            return rc;
        }

        FsDirectoryEntry entry = {};
        s64 totalEntries = 0;
        std::string cnmtFileName;

        while (R_SUCCEEDED(fsDirRead(&dir, &totalEntries, 1, &entry)) && totalEntries > 0) {
            std::string name = entry.name;
            if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".cnmt") == 0) {
                cnmtFileName = "/" + name;
                break;
            }
        }
        fsDirClose(&dir);

        if (cnmtFileName.empty()) {
            fsFsClose(&cnmtFs);
            return MAKERESULT(Module_Libnx, LibnxError_NotFound);
        }

        FsFile cnmtFile = {};
        rc = fsFsOpenFile(&cnmtFs, cnmtFileName.c_str(), FsOpenMode_Read, &cnmtFile);
        if (R_FAILED(rc)) {
            fsFsClose(&cnmtFs);
            return rc;
        }

        s64 cnmtFileSize = 0;
        rc = fsFileGetSize(&cnmtFile, &cnmtFileSize);
        if (R_FAILED(rc) || cnmtFileSize < (s64)sizeof(PackagedContentMetaHeader)) {
            fsFileClose(&cnmtFile);
            fsFsClose(&cnmtFs);
            return R_FAILED(rc) ? rc : MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        std::vector<u8> cnmtBytes((size_t)cnmtFileSize);
        u64 bytesRead = 0;
        rc = fsFileRead(&cnmtFile, 0, cnmtBytes.data(), cnmtBytes.size(), FsReadOption_None, &bytesRead);
        fsFileClose(&cnmtFile);
        fsFsClose(&cnmtFs);

        if (R_FAILED(rc) || bytesRead != (u64)cnmtFileSize) {
            return R_FAILED(rc) ? rc : MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        // Parsear encabezado del PackagedContentMeta
        const auto* pkgHdr = reinterpret_cast<const PackagedContentMetaHeader*>(cnmtBytes.data());
        outTitleId = pkgHdr->title_id;

        const size_t extHdrOffset = sizeof(PackagedContentMetaHeader);
        const size_t extHdrSize = pkgHdr->extended_header_size;
        const size_t contentsOffset = extHdrOffset + extHdrSize;

        if (contentsOffset > cnmtBytes.size()) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        const auto* pkgContents = reinterpret_cast<const PackagedContentInfo*>(cnmtBytes.data() + contentsOffset);
        std::vector<NcmContentInfo> validContents;
        validContents.reserve(pkgHdr->content_count);

        for (u16 i = 0; i < pkgHdr->content_count; i++) {
            if (contentsOffset + (i + 1) * sizeof(PackagedContentInfo) > cnmtBytes.size()) break;
            const auto& item = pkgContents[i];
            // Filtrar fragmentos delta si existieran
            if ((u8)item.content_info.content_type <= 5) {
                validContents.push_back(item.content_info);
            }
        }

        // Construir el búfer para NCM ContentMetaDatabase
        NcmContentMetaHeader metaHeader = {};
        metaHeader.extended_header_size = (u16)extHdrSize;
        metaHeader.content_count = (u16)(validContents.size() + 1); // +1 por el propio CNMT NCA
        metaHeader.content_meta_count = pkgHdr->content_meta_count;
        metaHeader.attributes = pkgHdr->attributes;
        metaHeader.storage_id = 0;

        NcmContentInfo cnmtContentInfo = {};
        cnmtContentInfo.content_id = cnmtId;
        ncmU64ToContentInfoSize(cnmtNcaSize & 0xFFFFFFFFFFFFULL, &cnmtContentInfo);
        cnmtContentInfo.content_type = NcmContentType_Meta;

        std::vector<u8> installBuffer;
        installBuffer.reserve(sizeof(metaHeader) + extHdrSize + (validContents.size() + 1) * sizeof(NcmContentInfo) + 128);

        // 1. Cabecera NcmContentMetaHeader
        appendBuffer(installBuffer, &metaHeader, sizeof(metaHeader));

        // 2. Cabecera extendida
        if (extHdrSize > 0) {
            appendBuffer(installBuffer, cnmtBytes.data() + extHdrOffset, extHdrSize);
        }

        // 3. Info del CNMT NCA
        appendBuffer(installBuffer, &cnmtContentInfo, sizeof(cnmtContentInfo));

        // 4. Info de cada contenido (NCA de programa, datos, etc.)
        for (const auto& ci : validContents) {
            appendBuffer(installBuffer, &ci, sizeof(ci));
        }

        // 5. Si es Patch, incluir datos extendidos de patch si existieran
        if (pkgHdr->type == NcmContentMetaType_Patch && extHdrSize >= sizeof(NcmPatchMetaExtendedHeader)) {
            const auto* patchExt = reinterpret_cast<const NcmPatchMetaExtendedHeader*>(cnmtBytes.data() + extHdrOffset);
            if (patchExt->extended_data_size > 0) {
                size_t patchDataOffset = contentsOffset + pkgHdr->content_count * sizeof(PackagedContentInfo);
                if (patchDataOffset + patchExt->extended_data_size <= cnmtBytes.size()) {
                    appendBuffer(installBuffer, cnmtBytes.data() + patchDataOffset, patchExt->extended_data_size);
                }
            }
        }

        // Registrar en ContentMetaDatabase
        NcmContentMetaKey metaKey = {};
        metaKey.id = pkgHdr->title_id;
        metaKey.version = pkgHdr->version;
        metaKey.type = (NcmContentMetaType)pkgHdr->type;
        metaKey.install_type = NcmContentInstallType_Full;

        rc = ncmContentMetaDatabaseSet(&metaDb, &metaKey, installBuffer.data(), installBuffer.size());
        if (R_FAILED(rc)) return rc;

        // IMPORTANTE: Primero commitear la base de datos de NCM para que el sistema
        // Horizon reconozca que el contenido existe antes de solicitar el registro en HOME.
        rc = ncmContentMetaDatabaseCommit(&metaDb);
        if (R_FAILED(rc)) return rc;

        // Registrar aplicación en el Menú HOME y verificar resultado
        rc = pushApplicationRecord(pkgHdr->title_id, targetStorage, metaKey);
        if (R_FAILED(rc)) return rc;

        return 0;
    }

    static void appendBuffer(std::vector<u8>& dest, const void* src, size_t size) {
        if (!src || size == 0) return;
        const u8* ptr = reinterpret_cast<const u8*>(src);
        dest.insert(dest.end(), ptr, ptr + size);
    }

    static std::string toHex(Result r) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%X", (unsigned int)r);
        return buf;
    }
};

} // namespace installer
