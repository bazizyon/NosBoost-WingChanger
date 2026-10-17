#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "ModContract.h"
#include "CModePacket.h"
#include "CInfoPacket.h"
#include "EntityType.h"
#include "Wings.h"
#include "Library.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
    struct Settings {
        bool OverrideUpgrade = false;
        int Upgrade = 0;
        bool OverrideDesign = false;
        int Design = 0;
    };

    constexpr uint32_t EffectTextureCache = 1;
    constexpr int FirstCustomEffect = 1900;

    const ModHost* CachedHost = nullptr;
    bool WindowVisible = false;
    Settings Config;

    long LocalId = 0;
    bool SpMode = false;
    int LastMorph = 0;

    uintptr_t AppliedPlayer = 0;
    bool HasApplied = false;
    Wings::Shown Applied{};
    Wings::Shown Real{};
    bool RestorePending = false;
    bool Dirty = false;

    std::vector<Library::SavedWing> Saved;
    std::map<int, std::array<int, Wings::MaxUpgrade + 1>> CustomEffects;
    int NextEffect = FirstCustomEffect;
    char ExportName[64] = "My wing";
    char ImportPath[MAX_PATH] = "";
    std::string Status;

    bool Same(const Wings::Shown& A, const Wings::Shown& B) {
        return A.Upgrade == B.Upgrade && A.Design == B.Design && A.DragonUpgrade == B.DragonUpgrade;
    }

    bool Overriding() {
        return Config.OverrideUpgrade || Config.OverrideDesign;
    }

    std::string SettingsPath() {
        char Path[MAX_PATH];
        GetModuleFileNameA(nullptr, Path, MAX_PATH);
        std::string Dir(Path);
        return Dir.substr(0, Dir.find_last_of("\\/") + 1) + "mods\\WingChanger.ini";
    }

    void LoadSettings() {
        const std::string Path = SettingsPath();
        Config.OverrideUpgrade = GetPrivateProfileIntA("WingChanger", "OverrideUpgrade", 0, Path.c_str()) != 0;
        Config.Upgrade = std::clamp<int>(GetPrivateProfileIntA("WingChanger", "Upgrade", 0, Path.c_str()), 0, Wings::MaxUpgrade);
        Config.OverrideDesign = GetPrivateProfileIntA("WingChanger", "OverrideDesign", 0, Path.c_str()) != 0;
        Config.Design = std::clamp<int>(GetPrivateProfileIntA("WingChanger", "Design", 0, Path.c_str()), 0, Wings::MaxCustomDesign);
    }

    void SaveSettings() {
        const std::string Path = SettingsPath();
        WritePrivateProfileStringA("WingChanger", "OverrideUpgrade", Config.OverrideUpgrade ? "1" : "0", Path.c_str());
        WritePrivateProfileStringA("WingChanger", "Upgrade", std::to_string(Config.Upgrade).c_str(), Path.c_str());
        WritePrivateProfileStringA("WingChanger", "OverrideDesign", Config.OverrideDesign ? "1" : "0", Path.c_str());
        WritePrivateProfileStringA("WingChanger", "Design", std::to_string(Config.Design).c_str(), Path.c_str());
    }

    int AllocateEffect() {
        while (Library::EffectExists(NextEffect)) NextEffect++;
        return NextEffect++;
    }

    void Put32(std::vector<uint8_t>& Bytes, size_t At, uint32_t Value) {
        std::memcpy(Bytes.data() + At, &Value, 4);
    }

    int Clone(int BaseEffect, const std::map<uint32_t, uint32_t>& Images) {
        std::vector<uint8_t> Source;
        std::vector<Library::Part> Parts;
        if (!Library::ReadEffect(BaseEffect, Source) || !Library::ParseParts(Source, Parts)) return 0;
        const size_t TracksStart = Parts.empty() ? Source.size() : Parts.front().TracksStart;
        std::vector<uint8_t> Definition(Source.begin(), Source.begin() + static_cast<std::ptrdiff_t>(TracksStart));
        for (const Library::Part& Entry : Parts) {
            const auto Begin = Source.begin();
            const auto Base = Images.find(Entry.Base);
            if (!Entry.FrameCount && Entry.Base != 0xFFFFFFFF && Base != Images.end()) {
                Definition.insert(Definition.end(), Begin + static_cast<std::ptrdiff_t>(Entry.TracksStart),
                                  Begin + static_cast<std::ptrdiff_t>(Entry.Frames));
                const uint8_t Frame[6] = {0, 0, static_cast<uint8_t>(Base->second), static_cast<uint8_t>(Base->second >> 8),
                                          static_cast<uint8_t>(Base->second >> 16), static_cast<uint8_t>(Base->second >> 24)};
                Definition.insert(Definition.end(), Frame, Frame + 6);
                Definition.insert(Definition.end(), Begin + static_cast<std::ptrdiff_t>(Entry.Frames),
                                  Begin + static_cast<std::ptrdiff_t>(Entry.TracksEnd));
                Definition[Entry.Offset + 0x90] = 1;
                continue;
            }
            const size_t Start = Definition.size();
            Definition.insert(Definition.end(), Begin + static_cast<std::ptrdiff_t>(Entry.TracksStart),
                              Begin + static_cast<std::ptrdiff_t>(Entry.TracksEnd));
            const size_t Values = Start + (Entry.Frames - Entry.TracksStart) + Entry.FrameCount * 2;
            for (int i = 0; i < Entry.FrameCount; i++) {
                uint32_t Value = 0;
                std::memcpy(&Value, Definition.data() + Values + i * 4, 4);
                const auto Found = Images.find(Value);
                if (Found != Images.end()) Put32(Definition, Values + i * 4, Found->second);
            }
        }
        const int Effect = AllocateEffect();
        Put32(Definition, 0, static_cast<uint32_t>(Effect));
        return CachedHost->RegisterEffect(Definition.data(), static_cast<uint32_t>(Definition.size())) ? Effect : 0;
    }

    bool Install(const Library::SavedWing& Wing, std::string& Problem) {
        std::map<uint32_t, uint32_t> Images;
        for (const auto& [Id, File] : Wing.Textures) {
            std::ifstream Png(File, std::ios::binary);
            const std::vector<char> Bytes((std::istreambuf_iterator<char>(Png)), std::istreambuf_iterator<char>());
            if (Bytes.empty()) continue;
            const int32_t NewId = CachedHost->LoadGameImage(EffectTextureCache, Bytes.data(), static_cast<uint32_t>(Bytes.size()),
                                                           nullptr, nullptr);
            if (NewId) Images[Id] = static_cast<uint32_t>(NewId);
        }
        if (Images.empty()) {
            Problem = "none of its PNGs could be loaded";
            return false;
        }
        std::array<int, Wings::MaxUpgrade + 1> Levels{};
        std::map<int, int> Cloned;
        for (int Upgrade = 1; Upgrade <= Wings::MaxUpgrade; Upgrade++) {
            const int Base = Wing.Levels[Upgrade];
            if (!Base) continue;
            if (!Cloned.contains(Base)) Cloned[Base] = Clone(Base, Images);
            Levels[Upgrade] = Cloned[Base];
        }
        if (std::ranges::all_of(Levels, [](int E) { return E == 0; })) {
            Problem = "no level could be made (missing effects in its wing.ini?)";
            return false;
        }
        CustomEffects[Wing.Design] = Levels;
        return true;
    }

    void InstallAll() {
        CustomEffects.clear();
        Saved = Library::List();
        if (!CachedHost || !Library::Loaded()) return;
        int Failed = 0;
        std::string Problem;
        for (const auto& Wing : Saved) {
            if (!Wing.Design || Install(Wing, Problem)) continue;
            Failed++;
            Status = std::format("\"{}\" not loaded: {}.", Wing.Name, Problem);
        }
        if (!Failed) Status.clear();
        Dirty = true;
    }

    bool IsSpMorph(const long Morph) {
        return Morph > 0 && Morph <= 54 && (Morph < 42 || Morph > 44);
    }

    long CurrentLocalId() {
        return LocalId ? LocalId : Wings::LocalPlayerId();
    }

    bool OnCInfo(const Packet::CInfoPacket& Packet) {
        LocalId = Packet.characterID;
        LastMorph = static_cast<int>(Packet.morphVNum);
        SpMode = IsSpMorph(Packet.morphVNum);
        return true;
    }

    bool OnCMode(const Packet::CModePacket& Packet) {
        if (Packet.entityType != EntityType::Player || Packet.entityID != CurrentLocalId()) return true;
        LastMorph = Packet.morphVNum;
        SpMode = Packet.spMode;
        return true;
    }

    void Apply() {
        const uintptr_t Player = Wings::LocalPlayer();
        if (!Player) {
            HasApplied = false;
            AppliedPlayer = 0;
            return;
        }

        Wings::Shown Now{};
        Wings::Read(Player, Now);
        if (!HasApplied || Player != AppliedPlayer || !Same(Now, Applied)) {
            Real = Now;
            HasApplied = false;
        }

        if (Overriding()) {
            if (!SpMode || !Now.CanHaveWings) return;
            if (Real.Upgrade > Wings::MaxUpgrade && !Config.OverrideDesign) return;
            const int Upgrade = Config.OverrideUpgrade ? Config.Upgrade : std::min<int>(Real.Upgrade, Wings::MaxUpgrade);
            const int Design = Config.OverrideDesign ? Config.Design : Real.Design;
            int Custom = 0;
            if (Design > Wings::MaxDesign) {
                const auto Found = CustomEffects.find(Design);
                if (Found == CustomEffects.end()) return;
                Custom = Found->second[Upgrade];
                if (!Custom) return;
            }
            if (!Dirty && Now.Upgrade == Upgrade && Now.Design == Design && Now.DragonUpgrade == Upgrade) return;
            Wings::Rebuild(Player, Upgrade, Design, Custom);
            Dirty = false;
        } else if (RestorePending) {
            RestorePending = false;
            if (!HasApplied) return;
            Wings::Rebuild(Player, Real.Upgrade, Real.Design);
            HasApplied = false;
            return;
        } else {
            return;
        }

        Wings::Read(Player, Applied);
        AppliedPlayer = Player;
        HasApplied = true;
    }

    void Wear(int Design) {
        Config.OverrideDesign = true;
        Config.Design = Design;
        Dirty = true;
        SaveSettings();
    }

    void OpenFolder(const std::string& Folder) {
        std::error_code Ignored;
        std::filesystem::create_directories(Folder, Ignored);
        ShellExecuteA(nullptr, "open", Folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void DrawLibrary() {
        ImGui::SeparatorText("Wing library");
        if (!Library::Loaded()) {
            ImGui::TextDisabled("Not available: %s.", Library::LastError().c_str());
            return;
        }

        const int BaseDesign = Config.OverrideDesign ? Config.Design : Real.Design;
        ImGui::PushItemWidth(150.0f);
        ImGui::InputText("##exportName", ExportName, sizeof(ExportName));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::BeginDisabled(BaseDesign > Wings::MaxDesign);
        if (ImGui::Button(std::format("Export design {}", BaseDesign).c_str())) {
            std::string Folder;
            if (Library::Export(BaseDesign, ExportName, Folder)) {
                Status = "Exported to " + Folder + ". Edit the PNGs, keep the layout, then Refresh.";
                InstallAll();
            } else {
                Status = "Export failed: " + Library::LastError() + ".";
            }
        }
        ImGui::EndDisabled();

        ImGui::PushItemWidth(150.0f);
        ImGui::InputTextWithHint("##importPath", "folder with wing.ini", ImportPath, sizeof(ImportPath));
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Import")) {
            std::string Path = ImportPath;
            Path.erase(std::remove(Path.begin(), Path.end(), '"'), Path.end());
            std::string Folder;
            if (Library::Import(Path, Folder)) {
                ImportPath[0] = 0;
                InstallAll();
                if (Status.empty()) Status = "Imported into " + Folder + ".";
            } else {
                Status = "Import failed: " + Library::LastError() + ".";
            }
        }

        if (ImGui::Button("Open wings folder")) OpenFolder(Library::WingsDirectory());
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) InstallAll();

        if (Saved.empty()) ImGui::TextDisabled("No saved wings yet.");
        for (const auto& Wing : Saved) {
            ImGui::PushID(Wing.Folder.c_str());
            const bool Ready = CustomEffects.contains(Wing.Design);
            const bool Worn = Config.OverrideDesign && Config.Design == Wing.Design;
            ImGui::BeginDisabled(!Ready);
            if (Worn) {
                if (ImGui::SmallButton("Stop")) {
                    Config.OverrideDesign = false;
                    if (!Overriding()) RestorePending = true;
                    SaveSettings();
                }
            } else if (ImGui::SmallButton("Wear")) {
                Wear(Wing.Design);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Folder")) OpenFolder(Wing.Folder);
            ImGui::SameLine();
            ImGui::Text("%d  %s", Wing.Design, Wing.Name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("from design %d, %d images", Wing.BaseDesign, static_cast<int>(Wing.Textures.size()));
            ImGui::PopID();
        }

        if (!Status.empty()) ImGui::TextWrapped("%s", Status.c_str());
    }

    void DrawWindow() {
        ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Wing Changer", &WindowVisible)) {
            ImGui::End();
            return;
        }

        const bool WasOverriding = Overriding();
        bool Changed = false;
        ImGui::PushItemWidth(110.0f);
        Changed |= ImGui::Checkbox("##useUpgrade", &Config.OverrideUpgrade);
        ImGui::SameLine();
        if (ImGui::InputInt("Wing upgrade", &Config.Upgrade)) {
            Config.Upgrade = std::clamp(Config.Upgrade, 0, Wings::MaxUpgrade);
            Changed = true;
        }
        Changed |= ImGui::Checkbox("##useDesign", &Config.OverrideDesign);
        ImGui::SameLine();
        if (ImGui::InputInt("Wing design", &Config.Design)) {
            Config.Design = std::clamp(Config.Design, 0, Wings::MaxCustomDesign);
            Changed = true;
        }
        ImGui::PopItemWidth();
        if (Config.Design > Wings::MaxDesign && !CustomEffects.contains(Config.Design)) {
            ImGui::TextDisabled("No saved wing has design %d.", Config.Design);
        }

        if (Changed) {
            if (WasOverriding && !Overriding()) RestorePending = true;
            Dirty = true;
            SaveSettings();
        }

        ImGui::Separator();
        if (!SpMode) {
            ImGui::TextDisabled("Not in specialist form, wings untouched.");
        }
        ImGui::TextDisabled("Real: upgrade %d, design %d", Real.Upgrade, Real.Design);
        DrawLibrary();
        ImGui::End();
    }
}

extern "C" {
    __declspec(dllexport) const ModClassRequirement* ModGetRequirements(size_t* OutCount) {
        *OutCount = 0;
        return nullptr;
    }

    __declspec(dllexport) void ModStartup(
        ImGuiContext* Context, const ImGuiMemAllocFunc AllocFunc, const ImGuiMemFreeFunc FreeFunc,
        void* AllocUserData, const ModHost* Host
    ) {
        ImGui::SetCurrentContext(Context);
        ImGui::SetAllocatorFunctions(AllocFunc, FreeFunc, AllocUserData);
        CachedHost = Host;
        LoadSettings();
        if (Wings::Find()) Library::Load();
        InstallAll();
        Packet::SubscribePacket(Host, &OnCInfo);
        Packet::SubscribePacket(Host, &OnCMode);
    }

    __declspec(dllexport) void ModShutdown() {
        if (CachedHost) {
            Packet::UnsubscribePacket(CachedHost, &OnCInfo);
            Packet::UnsubscribePacket(CachedHost, &OnCMode);
        }
        if (HasApplied) {
            if (const uintptr_t Player = Wings::LocalPlayer(); Player == AppliedPlayer) {
                Wings::Rebuild(Player, Real.Upgrade, Real.Design);
            }
        }
        HasApplied = false;
        CachedHost = nullptr;
    }

    __declspec(dllexport) void ModEarlyTick(TLBSWidget*, const TickContext tickContext) {
        if (!Wings::Found()) return;
        if (!tickContext.isPlayerLoaded) {
            HasApplied = false;
            AppliedPlayer = 0;
            return;
        }
        Apply();
    }

    __declspec(dllexport) void ModTick(TLBSWidget*, TickContext) {
        if (CachedHost) {
            if (!Wings::Found()) CachedHost->ReportStatus(ModHealthLevel::Broken, "Wing code not found in the game (game update?).");
            else if (!Library::Loaded()) CachedHost->ReportStatus(ModHealthLevel::Warning, Library::LastError().c_str());
            else CachedHost->ReportStatus(ModHealthLevel::Ok, "");
        }
        if (WindowVisible) DrawWindow();
    }

    __declspec(dllexport) void ModToggleMainWindow() {
        WindowVisible = !WindowVisible;
    }
}
