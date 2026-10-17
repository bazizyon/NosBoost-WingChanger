#define NOMINMAX
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "Library.h"
#include "Wings.h"

namespace fs = std::filesystem;

namespace Library {
    namespace {
        constexpr int DesignCount = Wings::MaxDesign + 1;
        constexpr int UpgradeCount = Wings::MaxUpgrade + 1;

        bool IsLoaded = false;
        std::string Error;
        int Table[DesignCount][UpgradeCount]{};
        Nos::Archive Textures[8];
        Nos::Archive Effects;
        std::unordered_map<uint32_t, std::vector<uint32_t>> EffectTextures;
        std::unordered_map<uint32_t, std::vector<uint32_t>> TextureEffects;

        uint32_t Read32(uintptr_t At) { return *reinterpret_cast<const uint32_t*>(At); }

        int Evaluate(uintptr_t Pc, uint32_t Eax, uint32_t Ecx) {
            bool Zero = false, Below = false, Less = false;
            auto Compare = [&](uint32_t A, uint32_t B) {
                Zero = A == B;
                Below = A < B;
                Less = static_cast<int32_t>(A) < static_cast<int32_t>(B);
            };
            auto U8 = [](uintptr_t At) { return *reinterpret_cast<const uint8_t*>(At); };
            auto S8 = [](uintptr_t At) { return static_cast<int32_t>(*reinterpret_cast<const int8_t*>(At)); };
            auto S32 = [](uintptr_t At) { return *reinterpret_cast<const int32_t*>(At); };
            auto Taken = [&](uint8_t Condition) {
                switch (Condition) {
                    case 0x72: return Below;
                    case 0x73: return !Below;
                    case 0x74: return Zero;
                    case 0x75: return !Zero;
                    case 0x76: return Below || Zero;
                    case 0x77: return !Below && !Zero;
                    case 0x7C: return Less;
                    case 0x7D: return !Less;
                    case 0x7E: return Zero || Less;
                    case 0x7F: return !Zero && !Less;
                    default: return false;
                }
            };
            auto IsCondition = [](uint8_t B) { return B == 0x72 || B == 0x73 || (B >= 0x74 && B <= 0x77) || (B >= 0x7C && B <= 0x7F); };
            for (int Step = 0; Step < 96; Step++) {
                const uint8_t B0 = U8(Pc), B1 = U8(Pc + 1);
                if (B0 == 0xC7 && B1 == 0x43 && U8(Pc + 2) == 0x10) return S32(Pc + 3);
                if (B0 == 0x89 && B1 == 0x43 && U8(Pc + 2) == 0x10) return static_cast<int>(Eax);
                if (B0 == 0x6A && B1 == 0x00) return 0;
                if (B0 == 0x83 && B1 == 0xF9) { Compare(Ecx, S8(Pc + 2)); Pc += 3; continue; }
                if (B0 == 0x83 && B1 == 0xF8) { Compare(Eax, S8(Pc + 2)); Pc += 3; continue; }
                if (B0 == 0x3D) { Compare(Eax, S32(Pc + 1)); Pc += 5; continue; }
                if (B0 == 0x83 && B1 == 0xE9) { Compare(Ecx, S8(Pc + 2)); Ecx -= S8(Pc + 2); Pc += 3; continue; }
                if (B0 == 0x83 && B1 == 0xE8) { Compare(Eax, S8(Pc + 2)); Eax -= S8(Pc + 2); Pc += 3; continue; }
                if (B0 == 0x2D) { Compare(Eax, S32(Pc + 1)); Eax -= S32(Pc + 1); Pc += 5; continue; }
                if (B0 == 0x49) { Ecx--; Zero = Ecx == 0; Less = static_cast<int32_t>(Ecx) < 0; Pc += 1; continue; }
                if (B0 == 0x48) { Eax--; Zero = Eax == 0; Less = static_cast<int32_t>(Eax) < 0; Pc += 1; continue; }
                if (B0 == 0x40) { Eax++; Pc += 1; continue; }
                if (B0 == 0x03 && B1 == 0xC0) { Eax *= 2; Pc += 2; continue; }
                if (B0 == 0x8D && B1 == 0x04 && U8(Pc + 2) == 0x80) { Eax *= 5; Pc += 3; continue; }
                if (B0 == 0x05) { Eax += S32(Pc + 1); Pc += 5; continue; }
                if (B0 == 0x83 && B1 == 0xC0) { Eax += S8(Pc + 2); Pc += 3; continue; }
                if (B0 == 0xB8) { Eax = static_cast<uint32_t>(S32(Pc + 1)); Pc += 5; continue; }
                if (B0 == 0x8A && B1 == 0x89) { Ecx = (Ecx & ~0xFFu) | U8(static_cast<uintptr_t>(S32(Pc + 2)) + Ecx); Pc += 6; continue; }
                if (B0 == 0xFF && B1 == 0x24 && U8(Pc + 2) == 0x8D) {
                    Pc = Read32(static_cast<uintptr_t>(S32(Pc + 3)) + Ecx * 4);
                    continue;
                }
                if (IsCondition(B0)) { Pc = Taken(B0) ? Pc + 2 + S8(Pc + 1) : Pc + 2; continue; }
                if (B0 == 0x0F && IsCondition(static_cast<uint8_t>(B1 - 0x10))) {
                    Pc = Taken(static_cast<uint8_t>(B1 - 0x10)) ? Pc + 6 + S32(Pc + 2) : Pc + 6;
                    continue;
                }
                if (B0 == 0xEB) { Pc = Pc + 2 + S8(Pc + 1); continue; }
                if (B0 == 0xE9) { Pc = Pc + 5 + S32(Pc + 1); continue; }
                return 0;
            }
            return 0;
        }

        bool ReadTable() {
            const uintptr_t Create = Wings::WingConstructor();
            if (!Create) return false;
            const uint8_t OuterP[] = {0x8B, 0xD0, 0x83, 0xFA, 0x39, 0x0F, 0x87};
            uintptr_t Outer = 0;
            for (uintptr_t At = Create; At < Create + 0x100; At++) {
                if (std::memcmp(reinterpret_cast<const void*>(At), OuterP, sizeof(OuterP)) == 0) {
                    Outer = At;
                    break;
                }
            }
            if (!Outer || *reinterpret_cast<const uint8_t*>(Outer + 4) != Wings::MaxDesign
                || std::memcmp(reinterpret_cast<const void*>(Outer + 0x0B), "\xFF\x24\x95", 3) != 0) return false;
            const uintptr_t OuterTable = Read32(Outer + 0x0E);
            int Found = 0;
            for (int Design = 0; Design < DesignCount; Design++) {
                for (int Upgrade = 1; Upgrade < UpgradeCount; Upgrade++) {
                    Table[Design][Upgrade] = Evaluate(Read32(OuterTable + Design * 4), Design, Upgrade);
                    if (Table[Design][Upgrade]) Found++;
                }
            }
            return Found > 0;
        }

        bool ReadEffects() {
            std::ifstream File(Nos::DataDirectory() + "NSeffData.NOS", std::ios::binary);
            if (!File) return false;
            const std::vector<uint8_t> Data((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
            if (Data.size() < 0x15 || std::memcmp(Data.data(), "NT Data", 7) != 0) return false;
            uint32_t Count = 0;
            std::memcpy(&Count, Data.data() + 0x10, 4);
            if (0x15 + static_cast<size_t>(Count) * 8 > Data.size()) return false;
            for (uint32_t i = 0; i < Count; i++) {
                uint32_t Id = 0, Offset = 0;
                std::memcpy(&Id, Data.data() + 0x15 + i * 8, 4);
                std::memcpy(&Offset, Data.data() + 0x19 + i * 8, 4);
                if (Offset + 13 > Data.size()) continue;
                uint32_t Size = 0;
                std::memcpy(&Size, Data.data() + Offset + 4, 4);
                if (Data[Offset + 12] != 0 || Offset + 13 + Size > Data.size()) continue;
                const uint8_t* Body = Data.data() + Offset + 13;
                std::vector<uint32_t>& Refs = EffectTextures[Id];
                for (uint32_t At = 0; At + 4 <= Size; At++) {
                    uint32_t Value = 0;
                    std::memcpy(&Value, Body + At, 4);
                    if ((Value >> 24) != 0x4F || !Textures[Value & 7].Has(Value)) continue;
                    if (std::ranges::find(Refs, Value) != Refs.end()) continue;
                    Refs.push_back(Value);
                    TextureEffects[Value].push_back(Id);
                }
            }
            return true;
        }

        std::wstring Wide(const std::string& Text) {
            const int Length = MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0);
            std::wstring Result(Length > 0 ? Length - 1 : 0, L'\0');
            if (Length > 0) MultiByteToWideChar(CP_UTF8, 0, Text.c_str(), -1, Result.data(), Length);
            return Result;
        }

        struct Com {
            HRESULT Init;
            Com() : Init(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
            ~Com() { if (SUCCEEDED(Init)) CoUninitialize(); }
        };

        bool SavePng(const std::string& Path, const Nos::Texture& Image) {
            Com Guard;
            IWICImagingFactory* Factory = nullptr;
            IWICStream* Stream = nullptr;
            IWICBitmapEncoder* Encoder = nullptr;
            IWICBitmapFrameEncode* Frame = nullptr;
            WICPixelFormatGUID Format = GUID_WICPixelFormat32bppBGRA;
            const bool Ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory)))
                && SUCCEEDED(Factory->CreateStream(&Stream))
                && SUCCEEDED(Stream->InitializeFromFilename(Wide(Path).c_str(), GENERIC_WRITE))
                && SUCCEEDED(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder))
                && SUCCEEDED(Encoder->Initialize(Stream, WICBitmapEncoderNoCache))
                && SUCCEEDED(Encoder->CreateNewFrame(&Frame, nullptr))
                && SUCCEEDED(Frame->Initialize(nullptr))
                && SUCCEEDED(Frame->SetSize(Image.Width, Image.Height))
                && SUCCEEDED(Frame->SetPixelFormat(&Format)) && Format == GUID_WICPixelFormat32bppBGRA
                && SUCCEEDED(Frame->WritePixels(Image.Height, Image.Width * 4, static_cast<UINT>(Image.Bgra.size()),
                                                const_cast<BYTE*>(Image.Bgra.data())))
                && SUCCEEDED(Frame->Commit())
                && SUCCEEDED(Encoder->Commit());
            if (Frame) Frame->Release();
            if (Encoder) Encoder->Release();
            if (Stream) Stream->Release();
            if (Factory) Factory->Release();
            return Ok;
        }

        std::string Sanitize(std::string Name) {
            for (char& C : Name) {
                if (std::strchr("\\/:*?\"<>|", C) || static_cast<unsigned char>(C) < 32) C = '_';
            }
            while (!Name.empty() && (Name.back() == ' ' || Name.back() == '.')) Name.pop_back();
            return Name.empty() ? "Wing" : Name;
        }

        std::string UniqueFolder(const std::string& Name) {
            const fs::path Base = fs::path(WingsDirectory()) / Sanitize(Name);
            fs::path Candidate = Base;
            for (int i = 2; fs::exists(Candidate); i++) Candidate = Base.string() + " (" + std::to_string(i) + ")";
            return Candidate.string();
        }

        bool ReadSaved(const fs::path& Folder, SavedWing& Out) {
            const std::string Ini = (Folder / "wing.ini").string();
            if (!fs::exists(Ini)) return false;
            char Buffer[256]{};
            GetPrivateProfileStringA("Wing", "Name", Folder.filename().string().c_str(), Buffer, sizeof(Buffer), Ini.c_str());
            Out.Folder = Folder.string();
            Out.Name = Buffer;
            Out.Design = GetPrivateProfileIntA("Wing", "Design", 0, Ini.c_str());
            Out.BaseDesign = std::clamp<int>(GetPrivateProfileIntA("Wing", "BaseDesign", 0, Ini.c_str()), 0, Wings::MaxDesign);
            for (int Upgrade = 1; Upgrade < UpgradeCount; Upgrade++) {
                Out.Levels[Upgrade] = GetPrivateProfileIntA("Levels", std::to_string(Upgrade).c_str(), 0, Ini.c_str());
            }
            std::vector<char> Keys(32768);
            const DWORD Length = GetPrivateProfileSectionA("Textures", Keys.data(), static_cast<DWORD>(Keys.size()), Ini.c_str());
            for (const char* Line = Keys.data(); Line < Keys.data() + Length && *Line; Line += std::strlen(Line) + 1) {
                const char* Equals = std::strchr(Line, '=');
                if (!Equals) continue;
                const uint32_t Id = std::strtoul(std::string(Line, Equals).c_str(), nullptr, 16);
                if ((Id >> 24) == 0x4F) Out.Textures[Id] = (Folder / (Equals + 1)).string();
            }
            if (IsLoaded && std::ranges::all_of(Out.Levels, [](int E) { return E == 0; })) {
                for (int Upgrade = 1; Upgrade < UpgradeCount; Upgrade++) Out.Levels[Upgrade] = EffectFor(Out.BaseDesign, Upgrade);
            }
            return true;
        }
    }

    bool Load() {
        if (IsLoaded) return true;
        if (!Wings::Found()) {
            Error = "wing code not found in the game";
            return false;
        }
        for (int i = 0; i < 8; i++) {
            if (!Textures[i].Open(Nos::DataDirectory() + std::format("NStpeData{:02X}.NOS", i))) {
                Error = std::format("couldn't open NStpeData{:02X}.NOS", i);
                return false;
            }
        }
        if (!Effects.Open(Nos::DataDirectory() + "NSeffData.NOS")) {
            Error = "couldn't open NSeffData.NOS";
            return false;
        }
        if (!ReadTable()) {
            Error = "wing design table not found in the game";
            return false;
        }
        if (!ReadEffects()) {
            Error = "couldn't read NSeffData.NOS";
            return false;
        }
        IsLoaded = true;
        return true;
    }

    bool Loaded() { return IsLoaded; }
    const std::string& LastError() { return Error; }

    int EffectFor(int Design, int Upgrade) {
        if (Design < 0 || Design >= DesignCount || Upgrade < 0 || Upgrade >= UpgradeCount) return 0;
        return Table[Design][Upgrade];
    }

    bool EffectExists(int Effect) {
        return Effects.Has(static_cast<uint32_t>(Effect));
    }

    bool ReadEffect(int Effect, std::vector<uint8_t>& Out) {
        return Effects.Read(static_cast<uint32_t>(Effect), Out);
    }

    bool ParseParts(const std::vector<uint8_t>& Definition, std::vector<Part>& Out) {
        Out.clear();
        if (Definition.size() < 0x18) return false;
        uint16_t Count = 0;
        std::memcpy(&Count, Definition.data() + 0x0C, 2);
        size_t At = 0x18 + static_cast<size_t>(Count) * 0xC0;
        if (At > Definition.size()) return false;
        auto Skip = [&](size_t Part, size_t Offset, size_t ValueSize) { At += Definition[Part + Offset] * (2 + ValueSize); };
        for (uint16_t i = 0; i < Count; i++) {
            Part Entry;
            Entry.Offset = 0x18 + static_cast<size_t>(i) * 0xC0;
            Entry.Type = Definition[Entry.Offset + 0x08];
            std::memcpy(&Entry.Base, Definition.data() + Entry.Offset + 0x10, 4);
            Entry.TracksStart = At;
            Skip(Entry.Offset, 0x60, 16);
            Skip(Entry.Offset, 0x6C, 12);
            Skip(Entry.Offset, 0x78, 12);
            Skip(Entry.Offset, 0x84, 4);
            Entry.FrameCount = Definition[Entry.Offset + 0x90];
            Entry.Frames = At;
            Skip(Entry.Offset, 0x90, 4);
            Skip(Entry.Offset, 0x9C, 16);
            Skip(Entry.Offset, 0xA8, 12);
            Skip(Entry.Offset, 0xB4, 12);
            Entry.TracksEnd = At;
            if (At > Definition.size()) return false;
            Out.push_back(Entry);
        }
        return At == Definition.size();
    }

    std::vector<uint32_t> PartImages(const std::vector<uint8_t>& Definition, const Part& Entry) {
        std::vector<uint32_t> Result;
        for (int i = 0; i < Entry.FrameCount; i++) {
            uint32_t Value = 0;
            std::memcpy(&Value, Definition.data() + Entry.Frames + Entry.FrameCount * 2 + i * 4, 4);
            if (Value != 0xFFFFFFFF && std::ranges::find(Result, Value) == Result.end()) Result.push_back(Value);
        }
        if (!Entry.FrameCount && Entry.Base != 0xFFFFFFFF) Result.push_back(Entry.Base);
        return Result;
    }

    std::vector<ImageInfo> DesignImages(int Design) {
        std::unordered_set<uint32_t> WingEffects;
        for (const auto& Row : Table) {
            for (const int E : Row) if (E) WingEffects.insert(E);
        }
        auto Generic = [&](uint32_t Texture) {
            const auto Users = TextureEffects.find(Texture);
            if (Users == TextureEffects.end()) return false;
            return std::ranges::count_if(Users->second, [&](uint32_t E) { return !WingEffects.contains(E); }) > 2;
        };
        std::vector<ImageInfo> Result;
        for (int Upgrade = 1; Upgrade < UpgradeCount; Upgrade++) {
            std::vector<uint8_t> Definition;
            std::vector<Part> Parts;
            if (!ReadEffect(EffectFor(Design, Upgrade), Definition) || !ParseParts(Definition, Parts)) continue;
            for (const Part& Entry : Parts) {
                const bool Model = Entry.Type == 2 && (Entry.Base >> 16) == 0x4FB0;
                for (const uint32_t Texture : PartImages(Definition, Entry)) {
                    if (!Textures[Texture & 7].Has(Texture) || Generic(Texture)) continue;
                    auto Found = std::ranges::find(Result, Texture, &ImageInfo::Id);
                    if (Found == Result.end()) {
                        Result.push_back({Texture, Upgrade, Upgrade, Model});
                        continue;
                    }
                    Found->First = std::min(Found->First, Upgrade);
                    Found->Last = std::max(Found->Last, Upgrade);
                    Found->Wing |= Model;
                }
            }
        }
        std::ranges::sort(Result, [](const ImageInfo& A, const ImageInfo& B) {
            return std::tie(A.First, A.Last, B.Wing, A.Id) < std::tie(B.First, B.Last, A.Wing, B.Id);
        });
        return Result;
    }

    bool ReadTexture(uint32_t Id, Nos::Texture& Out) {
        std::vector<uint8_t> Entry;
        return Textures[Id & 7].Read(Id, Entry) && Nos::DecodeTexture(Entry, Out);
    }

    std::string WingsDirectory() {
        char Path[MAX_PATH];
        GetModuleFileNameA(nullptr, Path, MAX_PATH);
        std::string Dir(Path);
        return Dir.substr(0, Dir.find_last_of("\\/") + 1) + "mods\\WingChanger\\Wings";
    }

    std::vector<SavedWing> List() {
        std::vector<SavedWing> Result;
        std::error_code Ignored;
        if (!fs::is_directory(WingsDirectory(), Ignored)) return Result;
        for (const auto& Item : fs::directory_iterator(WingsDirectory(), Ignored)) {
            SavedWing Wing;
            if (Item.is_directory() && ReadSaved(Item.path(), Wing)) Result.push_back(std::move(Wing));
        }
        std::ranges::sort(Result, {}, &SavedWing::Name);
        std::set<int> Taken;
        for (auto& Wing : Result) {
            if (Wing.Design >= FirstCustomDesign && Wing.Design <= Wings::MaxCustomDesign && Taken.insert(Wing.Design).second) continue;
            Wing.Design = 0;
        }
        for (auto& Wing : Result) {
            if (Wing.Design) continue;
            int Next = FirstCustomDesign;
            while (Taken.contains(Next)) Next++;
            if (Next > Wings::MaxCustomDesign) continue;
            Wing.Design = Next;
            Taken.insert(Next);
            WritePrivateProfileStringA("Wing", "Design", std::to_string(Next).c_str(), (fs::path(Wing.Folder) / "wing.ini").string().c_str());
        }
        return Result;
    }

    bool Export(int Design, const std::string& Name, std::string& OutFolder) {
        if (!Load()) return false;
        const std::vector<ImageInfo> Ids = DesignImages(Design);
        if (Ids.empty()) {
            Error = std::format("design {} has no wing images of its own", Design);
            return false;
        }
        std::error_code Code;
        OutFolder = UniqueFolder(Name);
        fs::create_directories(OutFolder, Code);
        if (Code) {
            Error = "couldn't create " + OutFolder;
            return false;
        }
        const std::string Ini = (fs::path(OutFolder) / "wing.ini").string();
        WritePrivateProfileStringA("Wing", "Name", Name.c_str(), Ini.c_str());
        WritePrivateProfileStringA("Wing", "BaseDesign", std::to_string(Design).c_str(), Ini.c_str());
        for (int Upgrade = 1; Upgrade < UpgradeCount; Upgrade++) {
            WritePrivateProfileStringA("Levels", std::to_string(Upgrade).c_str(), std::to_string(EffectFor(Design, Upgrade)).c_str(), Ini.c_str());
        }
        for (const ImageInfo& Info : Ids) {
            const uint32_t Id = Info.Id;
            Nos::Texture Image;
            const std::string Levels = Info.First == Info.Last ? std::format("+{:02}", Info.First)
                                                              : std::format("+{:02}-{:02}", Info.First, Info.Last);
            const std::string File = std::format("{} {} {:08X}.png", Levels, Info.Wing ? "wing" : "particle", Id);
            if (!ReadTexture(Id, Image) || !SavePng((fs::path(OutFolder) / File).string(), Image)) continue;
            WritePrivateProfileStringA("Textures", std::format("{:08X}", Id).c_str(), File.c_str(), Ini.c_str());
        }
        return true;
    }

    bool Import(const std::string& SourceFolder, std::string& OutFolder) {
        SavedWing Source;
        fs::path From(SourceFolder);
        if (!From.empty() && From.filename() == "wing.ini") From = From.parent_path();
        if (!ReadSaved(From, Source)) {
            Error = "no wing.ini in " + From.string();
            return false;
        }
        std::error_code Code;
        OutFolder = UniqueFolder(Source.Name);
        fs::create_directories(OutFolder, Code);
        fs::copy(From, OutFolder, fs::copy_options::recursive | fs::copy_options::overwrite_existing, Code);
        if (Code) {
            Error = "couldn't copy into " + OutFolder;
            return false;
        }
        WritePrivateProfileStringA("Wing", "Design", nullptr, (fs::path(OutFolder) / "wing.ini").string().c_str());
        return true;
    }

}
