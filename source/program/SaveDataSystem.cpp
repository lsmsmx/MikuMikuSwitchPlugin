#include <ctime>
#include <map>
#include <deque>
#include <set>
#include <mutex>
#include <cstring>
#include <vector>
#include "lib.hpp"
#include <nn/os.hpp>
#include <nn/fs.hpp>

#include "macros.hpp"
#include "ScoreData.hpp"
#include "SaveDataSystem.hpp"
#include "nc/save_data.hpp"
#include "logger.hpp"

#define SCORE_SIZE          0x11F4
#define SAVE_PATH           "ExlSD:/MikuMikuSwitchPlugin/Save/DivaModData.dat"

// =========================================================
// ADDRESSES & CONSTANTS (NSO = Ghidra - 0x100)
// =========================================================
// Hook Addresses
#define ADDR_FIND_OR_CREATE    0x0C61E0
#define ADDR_FIND_SCORE        0x0C7890
#define ADDR_REGISTER_SCORE    FIX(0x0C87F0)
#define ADDR_SAVE_MANAGER      FIX(0x0C9820)
#define ADDR_INIT_BOOT_2       FIX(0x0C5950)
#define ADDR_SYNC_MANAGER      FIX(0x0C6440)

// Fix performer parsing from PV DB
#define ADDR_GET_PERF_DATA_1    FIX(0x0C8FD0) // FUN_000c8ed0
#define ADDR_GET_PERF_DATA_2    FIX(0x0C9170) // FUN_000c9070
#define ADDR_GET_SCORE_ATTR_3   FIX(0x0C92A0) // FUN_000c91a0

// Addresses for Modules and Custom Items
#define ADDR_FIND_MODULE_1     0x0C8850
#define ADDR_FIND_MODULE_2     0x0C8C30
#define ADDR_FIND_CSTM_ITEM    0x0C8870
#define ADDR_FIND_CSTM_GALLERY 0x0C8C50

#define ADDR_GET_PV_DB_ACTIVE   0x0C4D80 // PV DB active flag getter
#define ADDR_GET_DEFAULT_PERF   0x0C4B00 // Read default modules

// =========================================================
// STRUCTURES
// =========================================================
struct Score {
    int32_t pvId;
    uint8_t data[SCORE_SIZE - 4];
};

struct Module {
    uint8_t unknown0;
    uint8_t unknown1;
};

struct ModuleEx {
    uint32_t moduleId;
    Module module;
};

struct CstmItem {
    uint8_t unknown0;
};

struct CstmItemEx {
    uint32_t cstmItemId;
    CstmItem cstmItem;
};

struct SaveDataEx {
    static constexpr uint32_t MAX_VERSION = 1;
    uint32_t version;
    uint32_t headerSize;
    uint32_t scoreCount;
    uint32_t moduleCount;
    uint32_t cstmItemCount;
};

// =========================================================
// GLOBALS
// =========================================================
std::recursive_mutex g_SaveMtx;
std::unordered_map<int32_t, Score*> g_scoreMap;
std::deque<Score> g_modPool;
std::set<int32_t> g_systemSlots;

std::map<uint32_t, Module> g_moduleMap;
std::map<uint32_t, CstmItem> g_cstmItemMap;

typedef void (*RegisterScoreT)(int32_t id, void* scorePtr);

// Manually register a song into the game engine
void DoForceRegister(int32_t id, void* ptr) {
    if (id <= 0 || !ptr) return;
    auto RegFunc = (RegisterScoreT)(exl::util::GetMainModuleInfo().m_Total.m_Start + ADDR_REGISTER_SCORE);
    RegFunc(id, ptr);
}

// =========================================================
// FILE SYSTEM
// =========================================================
void LoadSD() {
    std::scoped_lock lock(g_SaveMtx);
    nn::fs::FileHandle h;

    if (R_SUCCEEDED(nn::fs::OpenFile(&h, SAVE_PATH, nn::fs::OpenMode_Read))) {
        int64_t sz = 0;
        nn::fs::GetFileSize(&sz, h);

        if (sz > 0) {
            std::vector<uint8_t> buf(sz);
            nn::fs::ReadFile(h, 0, buf.data(), sz);

            bool isNewFormat = false;
            if (sz >= (int64_t)sizeof(SaveDataEx)) {
                SaveDataEx* header = reinterpret_cast<SaveDataEx*>(buf.data());
                if (header->version == SaveDataEx::MAX_VERSION && header->headerSize == sizeof(SaveDataEx)) {
                    isNewFormat = true;
                    size_t offset = header->headerSize;

                    // Read Scores
                    for (uint32_t i = 0; i < header->scoreCount; i++) {
                        Score s;
                        std::memcpy(&s, buf.data() + offset, sizeof(Score));
                        offset += sizeof(Score);

                        g_modPool.push_back(s);
                        g_scoreMap[s.pvId] = &g_modPool.back();
                        DoForceRegister(s.pvId, g_scoreMap[s.pvId]);
                    }

                    // Read Modules
                    for (uint32_t i = 0; i < header->moduleCount; i++) {
                        ModuleEx m;
                        std::memcpy(&m, buf.data() + offset, sizeof(ModuleEx));
                        offset += sizeof(ModuleEx);
                        g_moduleMap[m.moduleId] = m.module;
                    }

                    // Read CstmItems
                    for (uint32_t i = 0; i < header->cstmItemCount; i++) {
                        CstmItemEx c;
                        std::memcpy(&c, buf.data() + offset, sizeof(CstmItemEx));
                        offset += sizeof(CstmItemEx);
                        g_cstmItemMap[c.cstmItemId] = c.cstmItem;
                    }
                }
            }

            // Legacy save data migration
            if (!isNewFormat) {
                int count = (int)(sz / SCORE_SIZE);
                for (int i = 0; i < count; i++) {
                    Score s;
                    std::memcpy(&s, buf.data() + (i * SCORE_SIZE), SCORE_SIZE);
                    if (s.pvId > 0) {
                        g_modPool.push_back(s);
                        g_scoreMap[s.pvId] = &g_modPool.back();
                        DoForceRegister(s.pvId, g_scoreMap[s.pvId]);
                    }
                }
            }
        }
        nn::fs::CloseFile(h);
    }
}

void SaveSD() {
    std::scoped_lock lock(g_SaveMtx);
    if (g_scoreMap.empty() && g_moduleMap.empty() && g_cstmItemMap.empty()) return;

    size_t totalSize = sizeof(SaveDataEx)
                     + (g_scoreMap.size() * sizeof(Score))
                     + (g_moduleMap.size() * sizeof(ModuleEx))
                     + (g_cstmItemMap.size() * sizeof(CstmItemEx));

    std::vector<uint8_t> buffer;
    buffer.reserve(totalSize);
    buffer.resize(sizeof(SaveDataEx));

    SaveDataEx* header = reinterpret_cast<SaveDataEx*>(buffer.data());
    header->version = SaveDataEx::MAX_VERSION;
    header->headerSize = sizeof(SaveDataEx);
    header->scoreCount = static_cast<uint32_t>(g_scoreMap.size());
    header->moduleCount = static_cast<uint32_t>(g_moduleMap.size());
    header->cstmItemCount = static_cast<uint32_t>(g_cstmItemMap.size());

    // Copy Scores
    for (const auto& [id, sPtr] : g_scoreMap) {
        size_t off = buffer.size();
        buffer.resize(off + sizeof(Score));
        std::memcpy(buffer.data() + off, sPtr, sizeof(Score));
    }

    // Copy Modules
    for (const auto& [id, mod] : g_moduleMap) {
        size_t off = buffer.size();
        buffer.resize(off + sizeof(ModuleEx));
        ModuleEx mEx = { id, mod };
        std::memcpy(buffer.data() + off, &mEx, sizeof(ModuleEx));
    }

    // Copy CstmItems
    for (const auto& [id, cstm] : g_cstmItemMap) {
        size_t off = buffer.size();
        buffer.resize(off + sizeof(CstmItemEx));
        CstmItemEx cEx = { id, cstm };
        std::memcpy(buffer.data() + off, &cEx, sizeof(CstmItemEx));
    }

    nn::fs::DeleteFile(SAVE_PATH);
    if (R_SUCCEEDED(nn::fs::CreateFile(SAVE_PATH, totalSize))) {
        nn::fs::FileHandle h;
        if (R_SUCCEEDED(nn::fs::OpenFile(&h, SAVE_PATH, nn::fs::OpenMode_Write))) {
            nn::fs::WriteFile(h, 0, buffer.data(), totalSize, nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
            nn::fs::CloseFile(h);
        }
    }
}

// =========================================================
// HOOKS
// =========================================================

HOOK_DEFINE_TRAMPOLINE(FindOrCreateScoreHook) {
    static void* Callback(void* mgr, int32_t id) {
        if (id <= 0) return Orig(mgr, id);
        std::scoped_lock lock(g_SaveMtx);

        void* baseScore = nullptr;
        auto it = g_scoreMap.find(id);
        if (it != g_scoreMap.end()) {
            baseScore = it->second;
        } else {
            bool useSystem = false;
            if (g_systemSlots.count(id) > 0 || g_systemSlots.size() < 300) {
                useSystem = true;
            }
            if (useSystem) {
                baseScore = Orig(mgr, id);
                if (baseScore) {
                    g_systemSlots.insert(id);
                }
            }

            if (!baseScore) {
                Score s;
                std::memcpy(&s, EMPTY_SCORE_DATA, SCORE_SIZE);
                s.pvId = id;

                g_modPool.push_back(s);
                g_scoreMap[id] = &g_modPool.back();
                DoForceRegister(id, g_scoreMap[id]);
                baseScore = g_scoreMap[id];
            }
        }

        int32_t style = nc::GetCurrentStyleForSave();
        if (style != 0) {
            return nc::GetNCShadowScore(id, style, baseScore);
        }

        return baseScore;
    }
};

HOOK_DEFINE_TRAMPOLINE(FindScoreHook) {
    static void* Callback(void* mgr, int32_t id) {
        if (id <= 0) return Orig(mgr, id);

        void* baseScore = nullptr;
        {
            std::scoped_lock lock(g_SaveMtx);
            auto it = g_scoreMap.find(id);
            if (it != g_scoreMap.end()) {
                baseScore = it->second;
            }
        }

        // Fallback to system slot
        if (!baseScore) {
            baseScore = Orig(mgr, id);
        }

        int32_t style = nc::GetCurrentStyleForSave();
        if (style != 0) {
            if (baseScore) {
                return nc::GetNCShadowScore(id, style, baseScore);
            }
            return nullptr;
        }

        return baseScore;
    }
};

HOOK_DEFINE_TRAMPOLINE(FindModuleHook) {
    static Module* Callback(void* mgr, uint32_t id) {
        {
            std::scoped_lock lock(g_SaveMtx);
            if (g_moduleMap.count(id)) return &g_moduleMap[id];
        }

        Module* res = Orig(mgr, id); // Call outside lock

        if (res == nullptr && id > 0) {
            std::scoped_lock lock(g_SaveMtx);
            auto& mod = g_moduleMap[id];
            mod.unknown0 = 3;
            mod.unknown1 = 0;
            res = &mod;
        }
        return res;
    }
};

HOOK_DEFINE_TRAMPOLINE(FindModule2Hook) {
    static Module* Callback(void* mgr, uint32_t id) {
        {
            std::scoped_lock lock(g_SaveMtx);
            if (g_moduleMap.count(id)) return &g_moduleMap[id];
        }

        Module* res = Orig(mgr, id);

        if (res == nullptr && id > 0) {
            std::scoped_lock lock(g_SaveMtx);
            auto& mod = g_moduleMap[id];
            mod.unknown0 = 3;
            mod.unknown1 = 0;
            res = &mod;
        }
        return res;
    }
};

HOOK_DEFINE_TRAMPOLINE(FindCstmItemHook) {
    static CstmItem* Callback(void* mgr, uint32_t id) {
        {
            std::scoped_lock lock(g_SaveMtx);
            if (g_cstmItemMap.count(id)) return &g_cstmItemMap[id];
        }

        CstmItem* res = Orig(mgr, id);

        if (res == nullptr && id > 0) {
            std::scoped_lock lock(g_SaveMtx);
            auto& cstm = g_cstmItemMap[id];
            cstm.unknown0 = 3;
            res = &cstm;
        }
        return res;
    }
};

HOOK_DEFINE_TRAMPOLINE(FindCstmItemGalleryHook) {
    static CstmItem* Callback(void* mgr, uint32_t id) {
        {
            std::scoped_lock lock(g_SaveMtx);
            if (g_cstmItemMap.count(id)) return &g_cstmItemMap[id];
        }

        CstmItem* res = Orig(mgr, id);

        if (res == nullptr && id > 0) {
            std::scoped_lock lock(g_SaveMtx);
            auto& cstm = g_cstmItemMap[id];
            cstm.unknown0 = 3;
            res = &cstm;
        }
        return res;
    }
};

HOOK_DEFINE_TRAMPOLINE(InitBoot2Hook) {
    static void Callback(void* arg) {
        Orig(arg);
        std::scoped_lock lock(g_SaveMtx);
        for (auto& [id, s] : g_scoreMap) DoForceRegister(id, s);
    }
};

HOOK_DEFINE_TRAMPOLINE(SyncManagerHook) {
    static void Callback(void* arg) {
        std::scoped_lock lock(g_SaveMtx);
        for (auto&[id, s] : g_scoreMap) {
             DoForceRegister(id, s);
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(SaveManagerHook) {
    static uint64_t Callback(int mode) {

        if (mode == 1 || mode == 2) {
            nc::SyncNCShadowScores();
        }

        uint64_t res = Orig(mode);

        if (mode == 0) {
            LoadSD();
            nc::LoadSaveDataNC();
        }
        else if (mode == 1 || mode == 2) {
            SaveSD();
            nc::SaveSaveDataNC();
        }
        return res;
    }
};

void SaveDataSystem::init() {

    FindOrCreateScoreHook::InstallAtOffset(ADDR_FIND_OR_CREATE);
    FindScoreHook::InstallAtOffset(ADDR_FIND_SCORE);
    SaveManagerHook::InstallAtOffset(ADDR_SAVE_MANAGER);
    FindModuleHook::InstallAtOffset(ADDR_FIND_MODULE_1);
    FindModule2Hook::InstallAtOffset(ADDR_FIND_MODULE_2);
    FindCstmItemHook::InstallAtOffset(ADDR_FIND_CSTM_ITEM);
    FindCstmItemGalleryHook::InstallAtOffset(ADDR_FIND_CSTM_GALLERY);

    // 1. InitBoot (0x0C5C48)
    exl::patch::CodePatcher(0x0C5C48).Write<uint32_t>(0x2A0003E1); // mov w1, w0
    exl::patch::CodePatcher(0x0C5C4C).Write<uint32_t>(0xAA1303E0); // mov x0, x19
    exl::patch::CodePatcher(0x0C5C50).Write<uint32_t>(0x94000164); // bl 0x0C61E0
    exl::patch::CodePatcher(0x0C5C54).Write<uint32_t>(0x1400005D); // b 0x0C5DC8

    // 2. SyncManager Block 1 (0x0C6404)
    exl::patch::CodePatcher(0x0C6404).Write<uint32_t>(0x2A0003E1); // mov w1, w0
    exl::patch::CodePatcher(0x0C6408).Write<uint32_t>(0xF9402FE0); // ldr x0, [sp, #0x58]
    exl::patch::CodePatcher(0x0C640C).Write<uint32_t>(0x97FFFF75); // bl 0x0C61E0
    exl::patch::CodePatcher(0x0C6410).Write<uint32_t>(0x140000B5); // b 0x0C66E4

    // 3. SyncManager Block 1.5 (0x0C67A4)
    exl::patch::CodePatcher(0x0C67A4).Write<uint32_t>(0x2A0803E1); // mov w1, w8
    exl::patch::CodePatcher(0x0C67A8).Write<uint32_t>(0xF9402FE0); // ldr x0, [sp, #0x58]
    exl::patch::CodePatcher(0x0C67AC).Write<uint32_t>(0x94000439); // bl 0x0C7890
    exl::patch::CodePatcher(0x0C67B0).Write<uint32_t>(0xB9400308); // ldr w8, [x24]
    exl::patch::CodePatcher(0x0C67B4).Write<uint32_t>(0xB40003A0); // cbz x0, 0x0C6828
    exl::patch::CodePatcher(0x0C67B8).Write<uint32_t>(0xAA0003EA); // mov x10, x0
    exl::patch::CodePatcher(0x0C67BC).Write<uint32_t>(0x14000018); // b 0x0C681C

    // 4. SyncManager Block 2 (0x0C6828)
    exl::patch::CodePatcher(0x0C6828).Write<uint32_t>(0x2A0803E1); // mov w1, w8
    exl::patch::CodePatcher(0x0C682C).Write<uint32_t>(0xF9402FE0); // ldr x0, [sp, #0x58]
    exl::patch::CodePatcher(0x0C6830).Write<uint32_t>(0x94000418); // bl 0x0C7890
    exl::patch::CodePatcher(0x0C6834).Write<uint32_t>(0xB4005500); // cbz x0, 0x0C72D0
    exl::patch::CodePatcher(0x0C6838).Write<uint32_t>(0xAA0003E9); // mov x9, x0
    exl::patch::CodePatcher(0x0C683C).Write<uint32_t>(0x14000045); // b 0x0C6950

    // 5. SyncManager Block 3 (0x0C69B0)
    exl::patch::CodePatcher(0x0C69B0).Write<uint32_t>(0x2A0803E1); // mov w1, w8
    exl::patch::CodePatcher(0x0C69B4).Write<uint32_t>(0xF9402FE0); // ldr x0, [sp, #0x58]
    exl::patch::CodePatcher(0x0C69B8).Write<uint32_t>(0x940003B6); // bl 0x0C7890
    exl::patch::CodePatcher(0x0C69BC).Write<uint32_t>(0xB40048A0); // cbz x0, 0x0C72D0
    exl::patch::CodePatcher(0x0C69C0).Write<uint32_t>(0xAA0003E9); // mov x9, x0
    exl::patch::CodePatcher(0x0C69C4).Write<uint32_t>(0x14000047); // b 0x0C6AE0

    // 6. SyncManager Block 4 (0x0C6BD4)
    exl::patch::CodePatcher(0x0C6BD4).Write<uint32_t>(0x2A0003E1); // mov w1, w0
    exl::patch::CodePatcher(0x0C6BD8).Write<uint32_t>(0xF9402FE0); // ldr x0, [sp, #0x58]
    exl::patch::CodePatcher(0x0C6BDC).Write<uint32_t>(0x97FFFD81); // bl 0x0C61E0
    exl::patch::CodePatcher(0x0C6BE0).Write<uint32_t>(0xAA0003FC); // mov x28, x0
    exl::patch::CodePatcher(0x0C6BE4).Write<uint32_t>(0x14000080); // b 0x0C6DE4

    // 7. SyncManager Block 5 (0x0C6E3C)
    exl::patch::CodePatcher(0x0C6E3C).Write<uint32_t>(0x2A0003E1); // mov w1, w0
    exl::patch::CodePatcher(0x0C6E40).Write<uint32_t>(0xF9402FE0); // ldr x0, [sp, #0x58]
    exl::patch::CodePatcher(0x0C6E44).Write<uint32_t>(0x97FFFCE7); // bl 0x0C61E0
    exl::patch::CodePatcher(0x0C6E48).Write<uint32_t>(0xAA0003FA); // mov x26, x0
    exl::patch::CodePatcher(0x0C6E4C).Write<uint32_t>(0x14000080); // b 0x0C704C

    // 8. SyncManager Block 6 (0x0C7094)
    exl::patch::CodePatcher(0x0C7094).Write<uint32_t>(0xB9400301); // ldr w1, [x24]
    exl::patch::CodePatcher(0x0C7098).Write<uint32_t>(0xF9402FE0); // ldr x0, [sp, #0x58]
    exl::patch::CodePatcher(0x0C709C).Write<uint32_t>(0x97FFFC51); // bl 0x0C61E0
    exl::patch::CodePatcher(0x0C70A0).Write<uint32_t>(0xAA0003FB); // mov x27, x0
    exl::patch::CodePatcher(0x0C70A4).Write<uint32_t>(0x14000081); // b 0x0C72A8

    // 9. FUN_000c9070 (GetPerformerData 2)
    exl::patch::CodePatcher(0x000C90CC).Write<uint32_t>(0x37F80501); // tbnz w1, #0x1f, 0x0C916C
    exl::patch::CodePatcher(0x000C90D0).Write<uint32_t>(0xA9BF7BE2); // stp x2, x30, [sp, #-16]!
    exl::patch::CodePatcher(0x000C90D4).Write<uint32_t>(0x97FFF9EF); // bl 0x0C7890
    exl::patch::CodePatcher(0x000C90D8).Write<uint32_t>(0xAA0003E8); // mov x8, x0
    exl::patch::CodePatcher(0x000C90DC).Write<uint32_t>(0xA8C17BE2); // ldp x2, x30, [sp], #16
    exl::patch::CodePatcher(0x000C90E0).Write<uint32_t>(0xB5000068); // cbnz x8, 0x0C90EC
    exl::patch::CodePatcher(0x000C90E4).Write<uint32_t>(0xAA1F03E0); // mov x0, xzr
    exl::patch::CodePatcher(0x000C90E8).Write<uint32_t>(0xD65F03C0); // ret
    exl::patch::CodePatcher(0x000C90EC).Write<uint32_t>(0x14000026); // b 0x0C9184

    // 10. FUN_000c8ed0 (GetPerformerData 1)
    exl::patch::CodePatcher(0x0C8F4C).Write<uint32_t>(0x37F80661); // tbnz w1, #0x1f, 0x0C9018
    exl::patch::CodePatcher(0x0C8F50).Write<uint32_t>(0xA9BF7BE2); // stp x2, x30, [sp, #-16]!
    exl::patch::CodePatcher(0x0C8F54).Write<uint32_t>(0x97FFFA4F); // bl 0x0C7890
    exl::patch::CodePatcher(0x0C8F58).Write<uint32_t>(0xAA0003F4); // mov x20, x0
    exl::patch::CodePatcher(0x0C8F5C).Write<uint32_t>(0xA8C17BE2); // ldp x2, x30, [sp], #16
    exl::patch::CodePatcher(0x0C8F60).Write<uint32_t>(0xB40005D4); // cbz x20, 0x0C9018
    exl::patch::CodePatcher(0x0C8F64).Write<uint32_t>(0x1400003A); // b 0x0C904C
    exl::patch::CodePatcher(0x0C9018).Write<uint32_t>(0xAA1F03E0); // mov x0, xzr
    exl::patch::CodePatcher(0x0C901C).Write<uint32_t>(0x14000012); // b 0x0C9064

    // 11. FUN_000c91a0 (GetConfigSet)
    exl::patch::CodePatcher(0x000C91FC).Write<uint32_t>(0x97FFF3F9); // bl 0x0C61E0
    exl::patch::CodePatcher(0x000C9200).Write<uint32_t>(0xAA0003F3); // mov x19, x0
    exl::patch::CodePatcher(0x000C9204).Write<uint32_t>(0x1400003D); // b 0x0C92F8

    // 13. Save freeze fix (FUN_000ca660) - Restore limit register x13
    exl::patch::CodePatcher(0x0CA728).Write<uint32_t>(0x2A0003E1); // mov w1, w0
    exl::patch::CodePatcher(0x0CA72C).Write<uint32_t>(0xF9456EE0); // ldr x0, [x23, #0xad8]
    exl::patch::CodePatcher(0x0CA730).Write<uint32_t>(0x97FFEEAC); // bl 0x0C61E0
    exl::patch::CodePatcher(0x0CA734).Write<uint32_t>(0xAA0003F4); // mov x20, x0
    exl::patch::CodePatcher(0x0CA738).Write<uint32_t>(0xF94013ED); // ldr x13, [sp, #0x20]
    exl::patch::CodePatcher(0x0CA73C).Write<uint32_t>(0x1400004C); // b 0x0CA86C

    // 14. Save freeze fix (FUN_000ca9b0) - Restore limit register x13
    exl::patch::CodePatcher(0x0CAA70).Write<uint32_t>(0x2A0003E1); // mov w1, w0
    exl::patch::CodePatcher(0x0CAA74).Write<uint32_t>(0xF9456EC0); // ldr x0, [x22, #0xad8]
    exl::patch::CodePatcher(0x0CAA78).Write<uint32_t>(0x97FFEDDA); // bl 0x0C61E0
    exl::patch::CodePatcher(0x0CAA7C).Write<uint32_t>(0xAA0003F5); // mov x21, x0
    exl::patch::CodePatcher(0x0CAA80).Write<uint32_t>(0xF9400BED); // ldr x13, [sp, #0x10]
    exl::patch::CodePatcher(0x0CAA84).Write<uint32_t>(0x1400004C); // b 0x0CABB4
}
