#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nos {
    struct Archive {
        std::string Path;
        std::unordered_map<uint32_t, uint32_t> Offsets;
        bool Open(const std::string& FilePath);
        bool Has(uint32_t Id) const { return Offsets.contains(Id); }
        bool Read(uint32_t Id, std::vector<uint8_t>& Out) const;
    };

    struct Texture {
        uint16_t Width = 0;
        uint16_t Height = 0;
        uint8_t Format = 0;
        std::vector<uint8_t> Bgra;
    };

    std::string DataDirectory();
    bool DecodeTexture(const std::vector<uint8_t>& Entry, Texture& Out);
}
