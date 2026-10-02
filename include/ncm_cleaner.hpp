#pragma once
#include <switch.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

namespace cleaner {

struct OrphanItem {
    NcmStorageId storageId;
    NcmContentId contentId;
    u64 size = 0;
    std::string hexId;
};

struct ScanResult {
    bool success = false;
    std::vector<OrphanItem> items;
    u64 totalBytes = 0;
    u64 sdBytes = 0;
    u64 nandBytes = 0;
    size_t sdCount = 0;
    size_t nandCount = 0;
    std::string error;
};

struct CleanResult {
    bool success = false;
    int orphansDeleted = 0;
    u64 bytesFreed = 0;
    std::string error;
};

inline std::string toHexId(const NcmContentId& id) {
    char buf[33];
    for (int i = 0; i < 16; i++) {
        snprintf(buf + i * 2, 3, "%02x", id.c[i]);
    }
    return std::string(buf);
}

inline ScanResult scanStorage(NcmStorageId storageId) {
    ScanResult res;
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

    // Limpiar placeholders residuales
    ncmContentStorageCleanupAllPlaceHolder(&storage);

    s32 totalContents = 0;
    ncmContentStorageGetContentCount(&storage, &totalContents);

    if (totalContents > 0) {
        std::vector<NcmContentId> contentIds(totalContents);
        s32 readCount = 0;
        rc = ncmContentStorageListContentId(&storage, contentIds.data(), totalContents, &readCount, 0);
        if (R_SUCCEEDED(rc) && readCount > 0) {
            contentIds.resize(readCount);

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
                            u64 uSize = sz > 0 ? (u64)sz : 0;

                            OrphanItem item;
                            item.storageId = storageId;
                            item.contentId = contentIds[i + j];
                            item.size = uSize;
                            item.hexId = toHexId(contentIds[i + j]);

                            res.items.push_back(item);
                            res.totalBytes += uSize;
                            if (storageId == NcmStorageId_SdCard) {
                                res.sdBytes += uSize;
                                res.sdCount++;
                            } else {
                                res.nandBytes += uSize;
                                res.nandCount++;
                            }
                        }
                    }
                }
            }
        }
    }

    ncmContentMetaDatabaseClose(&metaDb);
    ncmContentStorageClose(&storage);
    ncmExit();
    res.success = true;
    return res;
}

inline ScanResult scanAllOrphans() {
    ScanResult sd = scanStorage(NcmStorageId_SdCard);
    ScanResult nand = scanStorage(NcmStorageId_BuiltInUser);

    ScanResult total;
    total.success = sd.success || nand.success;
    total.items.insert(total.items.end(), sd.items.begin(), sd.items.end());
    total.items.insert(total.items.end(), nand.items.begin(), nand.items.end());
    total.sdBytes = sd.sdBytes;
    total.sdCount = sd.sdCount;
    total.nandBytes = nand.nandBytes;
    total.nandCount = nand.nandCount;
    total.totalBytes = sd.sdBytes + nand.nandBytes;

    if (!sd.success && !nand.success) {
        total.error = sd.error + " | " + nand.error;
    }
    return total;
}

inline CleanResult deleteOrphans(const std::vector<OrphanItem>& items) {
    CleanResult res;
    if (items.empty()) {
        res.success = true;
        return res;
    }

    Result rc = ncmInitialize();
    if (R_FAILED(rc)) {
        res.error = "Fallo al inicializar NCM";
        return res;
    }

    NcmContentStorage sdStorage = {};
    NcmContentStorage nandStorage = {};
    bool sdOpen = R_SUCCEEDED(ncmOpenContentStorage(&sdStorage, NcmStorageId_SdCard));
    bool nandOpen = R_SUCCEEDED(ncmOpenContentStorage(&nandStorage, NcmStorageId_BuiltInUser));

    for (const auto& it : items) {
        NcmContentStorage* cs = (it.storageId == NcmStorageId_SdCard) ? (sdOpen ? &sdStorage : nullptr) : (nandOpen ? &nandStorage : nullptr);
        if (!cs) continue;

        if (R_SUCCEEDED(ncmContentStorageDelete(cs, &it.contentId))) {
            res.orphansDeleted++;
            res.bytesFreed += it.size;
        }
    }

    if (sdOpen) ncmContentStorageClose(&sdStorage);
    if (nandOpen) ncmContentStorageClose(&nandStorage);
    ncmExit();

    res.success = true;
    return res;
}

} // namespace cleaner
