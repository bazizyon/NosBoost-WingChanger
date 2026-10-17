#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <fstream>

#include "Nos.h"

namespace Nos {
    std::string DataDirectory() {
        char Path[MAX_PATH];
        GetModuleFileNameA(nullptr, Path, MAX_PATH);
        std::string Dir(Path);
        return Dir.substr(0, Dir.find_last_of("\\/") + 1) + "NostaleData\\";
    }

    bool Archive::Open(const std::string& FilePath) {
        Path = FilePath;
        Offsets.clear();
        std::ifstream File(FilePath, std::ios::binary);
        if (!File) return false;
        uint8_t Header[0x15];
        if (!File.read(reinterpret_cast<char*>(Header), sizeof(Header))) return false;
        if (std::memcmp(Header, "NT Data", 7) != 0) return false;
        uint32_t Count = 0;
        std::memcpy(&Count, Header + 0x10, 4);
        if (Count > 1000000) return false;
        std::vector<uint32_t> Pairs(static_cast<size_t>(Count) * 2);
        if (!File.read(reinterpret_cast<char*>(Pairs.data()), Pairs.size() * 4)) return false;
        for (uint32_t i = 0; i < Count; i++) Offsets[Pairs[i * 2]] = Pairs[i * 2 + 1];
        return true;
    }

    bool Archive::Read(uint32_t Id, std::vector<uint8_t>& Out) const {
        const auto Found = Offsets.find(Id);
        if (Found == Offsets.end()) return false;
        std::ifstream File(Path, std::ios::binary);
        if (!File) return false;
        File.seekg(Found->second);
        uint8_t Head[13];
        if (!File.read(reinterpret_cast<char*>(Head), sizeof(Head))) return false;
        uint32_t Size = 0, Stored = 0;
        std::memcpy(&Size, Head + 4, 4);
        std::memcpy(&Stored, Head + 8, 4);
        if (Head[12] != 0 || Size > (64u << 20)) return false;
        Out.resize(Size);
        return static_cast<bool>(File.read(reinterpret_cast<char*>(Out.data()), Size));
    }

    bool DecodeTexture(const std::vector<uint8_t>& Entry, Texture& Out) {
        if (Entry.size() < 8) return false;
        std::memcpy(&Out.Width, Entry.data(), 2);
        std::memcpy(&Out.Height, Entry.data() + 2, 2);
        Out.Format = Entry[4];
        const size_t Pixels = static_cast<size_t>(Out.Width) * Out.Height;
        const uint8_t* Source = Entry.data() + 8;
        Out.Bgra.resize(Pixels * 4);
        uint8_t* Target = Out.Bgra.data();
        if (Out.Format == 0 || Out.Format == 1) {
            if (Entry.size() < 8 + Pixels * 2) return false;
            for (size_t i = 0; i < Pixels; i++) {
                const uint16_t V = static_cast<uint16_t>(Source[i * 2] | Source[i * 2 + 1] << 8);
                if (Out.Format == 0) {
                    Target[i * 4 + 0] = static_cast<uint8_t>((V & 15) * 17);
                    Target[i * 4 + 1] = static_cast<uint8_t>((V >> 4 & 15) * 17);
                    Target[i * 4 + 2] = static_cast<uint8_t>((V >> 8 & 15) * 17);
                    Target[i * 4 + 3] = static_cast<uint8_t>((V >> 12 & 15) * 17);
                } else {
                    Target[i * 4 + 0] = static_cast<uint8_t>((V & 31) * 255 / 31);
                    Target[i * 4 + 1] = static_cast<uint8_t>((V >> 5 & 31) * 255 / 31);
                    Target[i * 4 + 2] = static_cast<uint8_t>((V >> 10 & 31) * 255 / 31);
                    Target[i * 4 + 3] = V >> 15 ? 255 : 0;
                }
            }
            return true;
        }
        if (Out.Format == 2) {
            if (Entry.size() < 8 + Pixels * 4) return false;
            std::memcpy(Target, Source, Pixels * 4);
            return true;
        }
        return false;
    }
}
