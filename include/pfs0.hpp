#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cctype>

#pragma pack(push, 1)
struct Pfs0Header {
    u32 magic;              // 'PFS0' (0x30534650)
    u32 numFiles;           // Number of files
    u32 stringTableSize;    // Size of string table
    u32 reserved;           // 0
};

struct Pfs0FileEntry {
    u64 dataOffset;         // Offset relative to end of string table
    u64 fileSize;           // File size in bytes
    u32 stringOffset;       // Offset in string table
    u32 reserved;           // 0
};
#pragma pack(pop)

struct Pfs0File {
    std::string name;
    u64 offset;             // Absolute offset from start of NSP
    u64 size;

    bool isCnmt() const {
        return name.size() >= 9 && name.compare(name.size() - 9, 9, ".cnmt.nca") == 0;
    }

    bool isNca() const {
        return name.size() >= 4 && name.compare(name.size() - 4, 4, ".nca") == 0;
    }

    bool isTicket() const {
        return name.size() >= 4 && name.compare(name.size() - 4, 4, ".tik") == 0;
    }

    bool isCert() const {
        return name.size() >= 5 && name.compare(name.size() - 5, 5, ".cert") == 0;
    }

    bool getNcaId(NcmContentId& outId) const {
        std::string base = name;
        size_t dot = base.find('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        if (base.size() < 32) return false;

        for (size_t i = 0; i < 16; i++) {
            char hexByte[3] = { base[i * 2], base[i * 2 + 1], '\0' };
            outId.c[i] = (u8)strtoul(hexByte, nullptr, 16);
        }
        return true;
    }
};

class Pfs0Parser {
public:
    static bool parse(FILE* f, std::vector<Pfs0File>& outFiles, std::string& outError) {
        if (!f) {
            outError = "Archivo invalido";
            return false;
        }

        fseek(f, 0, SEEK_SET);
        Pfs0Header hdr = {};
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
            outError = "No se pudo leer la cabecera PFS0";
            return false;
        }

        if (hdr.magic != 0x30534650) { // 'PFS0'
            outError = "Cabecera invalida (no es PFS0)";
            return false;
        }

        if (hdr.numFiles == 0 || hdr.numFiles > 1024) {
            outError = "Cantidad de archivos PFS0 fuera de rango";
            return false;
        }

        std::vector<Pfs0FileEntry> entries(hdr.numFiles);
        if (fread(entries.data(), sizeof(Pfs0FileEntry), hdr.numFiles, f) != hdr.numFiles) {
            outError = "Error al leer entradas PFS0";
            return false;
        }

        std::vector<char> strTable(hdr.stringTableSize);
        if (fread(strTable.data(), 1, hdr.stringTableSize, f) != hdr.stringTableSize) {
            outError = "Error al leer tabla de cadenas PFS0";
            return false;
        }

        u64 dataBaseOffset = sizeof(Pfs0Header) + (hdr.numFiles * sizeof(Pfs0FileEntry)) + hdr.stringTableSize;
        outFiles.clear();
        outFiles.reserve(hdr.numFiles);

        for (u32 i = 0; i < hdr.numFiles; i++) {
            Pfs0File pf;
            if (entries[i].stringOffset < strTable.size()) {
                pf.name = std::string(strTable.data() + entries[i].stringOffset);
            }
            pf.offset = dataBaseOffset + entries[i].dataOffset;
            pf.size = entries[i].fileSize;
            outFiles.push_back(std::move(pf));
        }

        return true;
    }
};
