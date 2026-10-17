#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <cstring>

#include "Wings.h"

namespace Wings {
    namespace {
        constexpr uint32_t FxListOffset = 0xC4;
        constexpr uint32_t ModelOffset = 0x1B8;
        constexpr uint32_t UpgradeOffset = 0x21A;
        constexpr uint32_t DesignOffset = 0x21C;
        constexpr uint32_t DragonUpgradeOffset = 0x21E;

        uintptr_t WingVmt = 0;
        uintptr_t WingCreate = 0;
        uintptr_t DragonVmt = 0;
        uintptr_t DragonCreate = 0;
        uintptr_t FxListAdd = 0;
        uintptr_t FxListDelete = 0;
        uintptr_t SceneGlobal = 0;
        uintptr_t GameTime = 0;
        uintptr_t SpawnEffect = 0;
        uintptr_t ReleaseEffect = 0;

        uintptr_t ImageBase = 0;
        size_t ImageSize = 0;

        uintptr_t Scan(const uint8_t* Pattern, const char* Mask, uintptr_t From, uintptr_t To) {
            const size_t Length = strlen(Mask);
            if (To < From + Length) return 0;
            for (uintptr_t At = From; At <= To - Length; At++) {
                size_t i = 0;
                for (; i < Length; i++) {
                    if (Mask[i] != '?' && *reinterpret_cast<const uint8_t*>(At + i) != Pattern[i]) break;
                }
                if (i == Length) return At;
            }
            return 0;
        }

        uint32_t Read32(uintptr_t At) { return *reinterpret_cast<const uint32_t*>(At); }
        uintptr_t CallTarget(uintptr_t CallAt) { return CallAt + 5 + static_cast<int32_t>(Read32(CallAt + 1)); }

        bool InheritsFrom(uintptr_t Object, uintptr_t Vmt) {
            if (!Object || !Vmt) return false;
            uintptr_t Current = Read32(Object);
            for (int Depth = 0; Current && Depth < 32; Depth++) {
                if (Current == Vmt) return true;
                const uintptr_t Parent = Read32(Current - 0x24);
                Current = Parent ? Read32(Parent) : 0;
            }
            return false;
        }

        bool IsClass(uintptr_t Object, const char* Name) {
            if (!Object) return false;
            const uintptr_t Vmt = Read32(Object);
            if (!Vmt) return false;
            const auto* ShortName = reinterpret_cast<const uint8_t*>(Read32(Vmt - 0x2C));
            const size_t Length = strlen(Name);
            return ShortName && ShortName[0] == Length && memcmp(ShortName + 1, Name, Length) == 0;
        }

        __declspec(noinline) uintptr_t CreateWing(int Upgrade, uintptr_t Owner, int Design) {
            uintptr_t Function = WingCreate;
            uintptr_t Vmt = WingVmt;
            uintptr_t Result = 0;
            _asm {
                push Design
                push Owner
                mov ecx, Upgrade
                mov dl, 1
                mov eax, Vmt
                call Function
                mov Result, eax
            }
            return Result;
        }

        __declspec(noinline) uintptr_t CreateDragon(int Upgrade, uintptr_t Owner) {
            uintptr_t Function = DragonCreate;
            uintptr_t Vmt = DragonVmt;
            uintptr_t Result = 0;
            _asm {
                push Owner
                push 1
                mov ecx, Upgrade
                mov dl, 1
                mov eax, Vmt
                call Function
                mov Result, eax
            }
            return Result;
        }

        __declspec(noinline) void AddFx(uintptr_t List, uintptr_t Fx) {
            uintptr_t Function = FxListAdd;
            _asm {
                mov edx, Fx
                mov eax, List
                call Function
            }
        }

        __declspec(noinline) void DeleteFx(uintptr_t List, int Index) {
            uintptr_t Function = FxListDelete;
            _asm {
                mov edx, Index
                mov eax, List
                call Function
            }
        }

        __declspec(noinline) uint32_t Now() {
            uintptr_t Function = GameTime;
            uint32_t Result = 0;
            _asm {
                call Function
                mov Result, eax
            }
            return Result;
        }

        __declspec(noinline) uint32_t Spawn(int Effect, uintptr_t Owner, uint32_t* Arguments) {
            uintptr_t Function = SpawnEffect;
            uint32_t Result = 0;
            _asm {
                push Arguments
                push 0
                xor ecx, ecx
                mov edx, Owner
                mov eax, Effect
                call Function
                mov Result, eax
            }
            return Result;
        }

        __declspec(noinline) void Release(uint32_t Handle) {
            uintptr_t Function = ReleaseEffect;
            _asm {
                mov eax, Handle
                call Function
            }
        }

        void UseEffect(uintptr_t Wing, uintptr_t Owner, int Effect) {
            auto* Handle = reinterpret_cast<uint32_t*>(Wing + 0x0C);
            if (*Handle != 0xFFFFFFFF) Release(*Handle);
            *reinterpret_cast<int32_t*>(Wing + 0x10) = Effect;
            uint32_t Arguments[1] = {Now()};
            *Handle = Spawn(Effect, Owner, Arguments);
            *reinterpret_cast<uint8_t*>(Wing + 0x14) = 0;
        }

        void DeleteAllOf(uintptr_t List, uintptr_t Vmt) {
            for (int i = static_cast<int>(Read32(List + 0x08)) - 1; i >= 0; i--) {
                const uintptr_t Items = Read32(List + 0x04);
                if (InheritsFrom(Read32(Items + i * 4), Vmt)) DeleteFx(List, i);
            }
        }
    }

    bool Find() {
        if (Found()) return true;

        const HMODULE Module = GetModuleHandleA(nullptr);
        MODULEINFO Info{};
        if (!Module || !K32GetModuleInformation(GetCurrentProcess(), Module, &Info, sizeof(Info))) return false;
        ImageBase = reinterpret_cast<uintptr_t>(Module);
        ImageSize = Info.SizeOfImage;
        const uintptr_t End = ImageBase + ImageSize;

        const uint8_t WingP[] = {
            0x50, 0x53, 0x8D, 0x4D, 0x00, 0xBA, 0x03, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xFC, 0x8B, 0x30, 0xFF, 0x56, 0x0C,
            0x8B, 0x45, 0x00, 0x33, 0xD2, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xC8, 0xB2, 0x01, 0xA1, 0x00, 0x00, 0x00, 0x00,
            0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xD0, 0x8B, 0x83, 0xC4, 0x00, 0x00, 0x00, 0xE8
        };
        const uintptr_t Wing = Scan(WingP, "xxxx?xxxxxxxxxxxxxxx?xxx????xxxxx????x????xxxxxxxxx", ImageBase, End);
        if (!Wing) return false;

        const uint8_t DragonP[] = {
            0x53, 0x6A, 0x01, 0x8D, 0x4D, 0x00, 0xBA, 0x03, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xFC, 0x8B, 0x30, 0xFF, 0x56, 0x0C,
            0x8B, 0x45, 0x00, 0x33, 0xD2, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0xC8, 0xB2, 0x01, 0xA1, 0x00, 0x00, 0x00, 0x00, 0xE8
        };
        const uintptr_t Dragon = Scan(DragonP, "xxxxx?xxxxxxxxxxxxxxx?xxx????xxxxx????x", Wing, Wing + 0x400);
        if (!Dragon) return false;

        const uint8_t DeleteP[] = {0x8B, 0x87, 0xC4, 0x00, 0x00, 0x00, 0x8B, 0xD6, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x83, 0xFE, 0xFF};
        const uintptr_t Delete = Scan(DeleteP, "xxxxxxxxx????xxxx", Wing - 0x400, Wing);
        if (!Delete) return false;

        const uint8_t SceneP[] = {0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x12, 0x3B, 0x5A, 0x30};
        const uintptr_t Scene = Scan(SceneP, "xx????xxxxx", Wing - 0x2000, Wing);
        if (!Scene) return false;

        const uint8_t SpawnP[] = {
            0x6A, 0xFF, 0x33, 0xC9, 0x33, 0xD2, 0x8B, 0xC3, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00,
            0x89, 0x45, 0xF8, 0x8D, 0x45, 0xF8, 0x50, 0x6A, 0x00, 0x8B, 0x43, 0x10, 0x33, 0xC9, 0x8B, 0xD6, 0xE8, 0x00, 0x00,
            0x00, 0x00, 0x89, 0x43, 0x0C, 0xC6, 0x43, 0x14, 0x00
        };
        const uintptr_t Constructor = CallTarget(Wing + 0x25);
        const uintptr_t SpawnAt = Scan(SpawnP, "xxxxxxxxx????x????xxxxxxxxxxxxxxxxx????xxxxxxx", Constructor, Constructor + 0x3000);
        if (!SpawnAt) return false;
        const uint8_t ReleaseP[] = {0x8B, 0x46, 0x0C, 0x83, 0xF8, 0xFF, 0x73, 0x05, 0xE8};
        const uintptr_t ReleaseAt = Scan(ReleaseP, "xxxxxxxxx", SpawnAt, SpawnAt + 0x200);
        if (!ReleaseAt) return false;
        GameTime = CallTarget(SpawnAt + 0x0D);
        SpawnEffect = CallTarget(SpawnAt + 0x22);
        ReleaseEffect = CallTarget(ReleaseAt + 0x08);

        WingVmt = Read32(Read32(Wing + 0x21));
        WingCreate = CallTarget(Wing + 0x25);
        FxListAdd = CallTarget(Wing + 0x32);
        DragonVmt = Read32(Read32(Dragon + 0x22));
        DragonCreate = CallTarget(Dragon + 0x26);
        FxListDelete = CallTarget(Delete + 0x08);
        SceneGlobal = Read32(Scene + 0x02);
        return Found();
    }

    bool Found() {
        return WingVmt && WingCreate && DragonVmt && DragonCreate && FxListAdd && FxListDelete && SceneGlobal
            && GameTime && SpawnEffect && ReleaseEffect;
    }

    uintptr_t WingConstructor() {
        return WingCreate;
    }

    uintptr_t SceneManager() {
        if (!SceneGlobal) return 0;
        const uintptr_t Holder = Read32(SceneGlobal);
        return Holder ? Read32(Holder) : 0;
    }

    uintptr_t LocalPlayer() {
        const uintptr_t Scene = SceneManager();
        if (!Scene) return 0;
        const uintptr_t Player = Read32(Scene + 0x30);
        return IsClass(Player, "TMapPlayerObj") ? Player : 0;
    }

    int32_t LocalPlayerId() {
        const uintptr_t Scene = SceneManager();
        return Scene ? static_cast<int32_t>(Read32(Scene + 0x2C)) : -1;
    }

    bool Read(uintptr_t Player, Shown& Out) {
        if (!Player) return false;
        Out.Upgrade = *reinterpret_cast<const uint16_t*>(Player + UpgradeOffset);
        Out.Design = *reinterpret_cast<const uint16_t*>(Player + DesignOffset);
        Out.DragonUpgrade = *reinterpret_cast<const uint16_t*>(Player + DragonUpgradeOffset);
        Out.CanHaveWings = *reinterpret_cast<const uint16_t*>(Player + ModelOffset) != 0xFFFF;
        return true;
    }

    void Rebuild(uintptr_t Player, int Upgrade, int Design, int CustomEffect) {
        if (!Found() || !Player) return;
        const uintptr_t List = Read32(Player + FxListOffset);
        if (!List) return;

        *reinterpret_cast<uint16_t*>(Player + UpgradeOffset) = 0;
        *reinterpret_cast<uint16_t*>(Player + DesignOffset) = 0;
        *reinterpret_cast<uint16_t*>(Player + DragonUpgradeOffset) = 0;
        DeleteAllOf(List, WingVmt);
        DeleteAllOf(List, DragonVmt);

        if (*reinterpret_cast<const uint16_t*>(Player + ModelOffset) == 0xFFFF) return;
        if (const uintptr_t Wing = CreateWing(Upgrade, Player, Design)) {
            if (CustomEffect && Upgrade > 0) UseEffect(Wing, Player, CustomEffect);
            AddFx(List, Wing);
        }
        if (const uintptr_t Dragon = CreateDragon(Upgrade, Player)) AddFx(List, Dragon);
    }
}
