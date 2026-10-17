#pragma once
#include <cstdint>

namespace Wings {
    constexpr int MaxUpgrade = 20;
    constexpr int MaxDesign = 57;
    constexpr int MaxCustomDesign = 255;

    struct Shown {
        uint16_t Upgrade;
        uint16_t Design;
        uint16_t DragonUpgrade;
        bool CanHaveWings;
    };

    bool Find();
    bool Found();
    uintptr_t WingConstructor();
    uintptr_t LocalPlayer();
    int32_t LocalPlayerId();
    bool Read(uintptr_t Player, Shown& Out);
    void Rebuild(uintptr_t Player, int Upgrade, int Design, int CustomEffect = 0);
}
