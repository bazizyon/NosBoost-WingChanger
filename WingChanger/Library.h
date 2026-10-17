#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Nos.h"

namespace Library {
    constexpr int FirstCustomDesign = 58;

    struct SavedWing {
        std::string Folder;
        std::string Name;
        int Design = 0;
        int BaseDesign = 0;
        int Levels[21]{};
        std::map<uint32_t, std::string> Textures;
    };

    struct Part {
        size_t Offset = 0;
        uint8_t Type = 0;
        uint32_t Base = 0;
        size_t TracksStart = 0;
        size_t TracksEnd = 0;
        size_t Frames = 0;
        uint8_t FrameCount = 0;
    };

    struct ImageInfo {
        uint32_t Id;
        int First;
        int Last;
        bool Wing;
    };

    bool Load();
    bool Loaded();
    const std::string& LastError();

    int EffectFor(int Design, int Upgrade);
    bool EffectExists(int Effect);
    bool ReadEffect(int Effect, std::vector<uint8_t>& Out);
    bool ParseParts(const std::vector<uint8_t>& Definition, std::vector<Part>& Out);
    std::vector<ImageInfo> DesignImages(int Design);
    bool ReadTexture(uint32_t Id, Nos::Texture& Out);

    std::string WingsDirectory();
    std::vector<SavedWing> List();
    bool Export(int Design, const std::string& Name, std::string& OutFolder);
    bool Import(const std::string& SourceFolder, std::string& OutFolder);
}
