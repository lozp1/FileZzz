#pragma once
#include <switch.h>
#include <vector>
#include <string>
#include <algorithm>

namespace cleaner {

struct CleanResult {
    bool success = false;
    int placeholdersCleaned = 0;
    int orphansDeleted = 0;
    u64 bytesFreed = 0;
    std::string error;
};

inline CleanResult cleanStorage(NcmStorageId storageId) {
    CleanResult res;
    Result rc = ncmInitialize();
    if (R_FAILED(rc)) {
        res.error = "Fallo al inicializar NCM";
        return res;
    }

    NcmContentStorage storage = {};
    rc = ncmOpenContentStorage(&storage, storageId);
    if (R_FAILED(rc)) {
        ncmExit();
        res.error = "Fallo al abrir ContentStorage";
        return res;
    }

    NcmContentMetaDatabase metaDb = {};
    rc = ncmOpenContentMetaDatabase(&metaDb, storageId);
    if (R_FAILED(rc)) {
        ncmContentStorageClose(&storage);
        ncmExit();
        res.error = "Fallo al abrir ContentMetaDatabase";
        return res;
    }

    // 1. Limpiar todos los placeholders temporales
    ncmContentStorageCleanupAllPlaceHolder(&storage);

    // 2. Listar todos los contenidos en disco
    s32 totalContents = 0;
    ncmContentStorageGetContentCount(&storage, &totalContents);

    if (totalContents > 0) {
        std::vector<NcmContentId> contentIds(totalContents);
        s32 readCount = 0;
        rc = ncmContentStorageListContentId(&storage, contentIds.data(), totalContents, &readCount, 0);
        if (R_SUCCEEDED(rc) && readCount > 0) {
            contentIds.resize(readCount);

            // Consultar huerfanos en lotes de 128
            const size_t BATCH = 128;
            for (size_t i = 0; i < contentIds.size(); i += BATCH) {
                size_t count = std::min(BATCH, contentIds.size() - i);
                std::vector<u8> orphanFlags(count, 0);
                rc = ncmContentMetaDatabaseLookupOrphanContent(&metaDb, (bool*)orphanFlags.data(), &contentIds[i], (s32)count);
                if (R_SUCCEEDED(rc)) {
                    for (size_t j = 0; j < count; j++) {
                        if (orphanFlags[j]) {
                            s64 sz = 0;
                            ncmContentStorageGetSizeFromContentId(&storage, &sz, &contentIds[i + j]);
                            if (R_SUCCEEDED(ncmContentStorageDelete(&storage, &contentIds[i + j]))) {
                                res.orphansDeleted++;
                                if (sz > 0) res.bytesFreed += (u64)sz;
                            }
                        }
                    }
                }
            }
        }
    }

    ncmContentMetaDatabaseCommit(&metaDb);
    ncmContentMetaDatabaseClose(&metaDb);
    ncmContentStorageClose(&storage);
    ncmExit();
    res.success = true;
    return res;
}

inline CleanResult cleanAllOrphans() {
    CleanResult sdRes = cleanStorage(NcmStorageId_SdCard);
    CleanResult nandRes = cleanStorage(NcmStorageId_BuiltInUser);
    CleanResult total;
    total.success = sdRes.success || nandRes.success;
    total.orphansDeleted = sdRes.orphansDeleted + nandRes.orphansDeleted;
    total.bytesFreed = sdRes.bytesFreed + nandRes.bytesFreed;
    if (!sdRes.success && !nandRes.success) {
        total.error = sdRes.error + " | " + nandRes.error;
    }
    return total;
}

} // namespace cleaner
