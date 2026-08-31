#include "StrArray.hpp"
#include "lib.hpp"
#include "macros.hpp"
#include "ModLoader.hpp"
#include "toml.hpp"
#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cstring>
#include "fs.hpp"

// =========================================================
// ADDRESSES & CONSTANTS (NSO = Ghidra - 0x100)
// =========================================================
#define ADDR_LOAD_STR_ARRAY        FIX(0x15D210)
#define ADDR_GET_STR               FIX(0x15D480)
#define ADDR_GET_LANG_DIR          FIX(0x21CA70)

#define ADDR_CALL_MODULE_NAME      FIX(0x3C40D8)
#define ADDR_CALL_CUSTOMIZE_NAME   FIX(0x3BBEA0)
#define ADDR_CALL_BTN_SE_NAME      FIX(0x3B5C1C)
#define ADDR_CALL_CHAIN_SLIDE_NAME FIX(0x3B77FC)
#define ADDR_CALL_SLIDER_TOUCH     FIX(0x3D9CD4)
#define ADDR_CALL_SLIDE_SE_NAME    FIX(0x3DB764)

// ARM64 NOP instruction
constexpr uint32_t ARM64_NOP = 0xD503201F;

typedef std::unordered_map<int, std::string> StrByIdMap;

static StrByIdMap strMap;
static StrByIdMap moduleStrMap;
static StrByIdMap customizeStrMap;
static StrByIdMap btnSeStrMap;
static StrByIdMap slideSeStrMap;
static StrByIdMap chainSlideSeStrMap;
static StrByIdMap sliderTouchSeStrMap;

// TOML PARSING
static void readStrArray(const toml::table* table, StrByIdMap& dstStrMap) {
    if (!table) return;
    for (auto&&[key, value] : *table) {
        if (value.is_table() || value.is_array()) continue;

        // Use temporary string to ensure null-termination for strtol
        std::string keyStr(key.data(), key.length());
        char* end = nullptr;
        const int id = std::strtol(keyStr.c_str(), &end, 10);

        if (end != keyStr.c_str() && dstStrMap.find(id) == dstStrMap.end()) {
            if (value.is_string()) {
                dstStrMap.insert({ id, std::string(value.as_string()->get()) });
            } else {
                dstStrMap.insert({ id, "YOU FORGOT QUOTATION MARKS" });
            }
        }
    }
}

static void readStrArray(const toml::table* table, const toml::table* langTable, StrByIdMap& dstStrMap) {
    if (langTable != nullptr) readStrArray(langTable, dstStrMap);
    readStrArray(table, dstStrMap);
}

static void readStrArray(const toml::table* table, const toml::table* langTable, const char* name, StrByIdMap& dstStrMap) {
    if (langTable != nullptr) readStrArray(langTable->get_as<toml::table>(name), dstStrMap);
    readStrArray(table->get_as<toml::table>(name), dstStrMap);
}

static void loadStrArray(const std::string& filePath) {
    nn::fs::FileHandle h;
    if (R_FAILED(nn::fs::OpenFile(&h, filePath.c_str(), nn::fs::OpenMode_Read))) return;

    int64_t size = 0;
    nn::fs::GetFileSize(&size, h);
    if (size <= 0) {
        nn::fs::CloseFile(h);
        return;
    }

    std::string fileContent(size, '\0');
    nn::fs::ReadFile(h, 0, fileContent.data(), size);
    nn::fs::CloseFile(h);

    toml::parse_result result = toml::parse(fileContent);
    if (!result) return;

    toml::table table = std::move(result.table());

    typedef const char* (*GetLangDirT)();
    GetLangDirT getLangDir = (GetLangDirT)(exl::util::GetMainModuleInfo().m_Total.m_Start + ADDR_GET_LANG_DIR);

    const char* langPath = getLangDir();

    // FIX: Use strrchr to find the LAST slash to get folder name (e.g. "en" from "rom/lang2/en")
    const char* lastSlash = strrchr(langPath, '/');
    const char* langName = lastSlash ? (lastSlash + 1) : langPath;

    toml::table* langTable = table.get_as<toml::table>(langName);

    readStrArray(&table, langTable, strMap);
    readStrArray(&table, langTable, "module", moduleStrMap);
    readStrArray(&table, langTable, "customize", customizeStrMap);
    readStrArray(&table, langTable, "cstm_item", customizeStrMap);
    readStrArray(&table, langTable, "btn_se", btnSeStrMap);
    readStrArray(&table, langTable, "slide_se", slideSeStrMap);
    readStrArray(&table, langTable, "chainslide_se", chainSlideSeStrMap);
    readStrArray(&table, langTable, "slidertouch_se", sliderTouchSeStrMap);
}

// BASE HOOKS (TRAMPOLINE)
HOOK_DEFINE_TRAMPOLINE(LoadStrArrayHook) {
    static void Callback() {
        Orig();

        // Load strings from every mod directory
        for (const auto& modPath : ModLoader::modDirectoryPaths) {
            loadStrArray(modPath + "/rom/lang2/mod_str_array.toml");
            loadStrArray(modPath + "/rom/lang2/str_array.toml");
        }

        // Load localized DML specific and DLC string arrays
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/mod_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/mdata_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/privilege_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc1_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc2A_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc2B_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc3A_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc3B_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc4_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc7_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc8_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc9_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc10_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc11_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc12_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc13_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc14_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc15_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc16_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc17_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc18_str_array.toml");
        loadStrArray("ExlSD:/MikuMikuSwitchPlugin/lang2/dlc19_str_array.toml");
    }
};

HOOK_DEFINE_TRAMPOLINE(GetStrHook) {
    static const char* Callback(int32_t id) {
        auto it = strMap.find(id);
        if (it != strMap.end()) return it->second.c_str();

        const char* origStr = Orig(id);
        if (origStr == nullptr) return Orig(0);
        return origStr;
    }
};

// IMPLEMENTATION WRAPPERS
static const char* getModuleNameImp(int str_id, int module_id) {
    auto it = moduleStrMap.find(module_id);
    if (it != moduleStrMap.end()) return it->second.c_str();
    return GetStrHook::Callback(str_id);
}

static const char* getCustomizeNameImp(int str_id, int customize_id) {
    auto it = customizeStrMap.find(customize_id);
    if (it != customizeStrMap.end()) return it->second.c_str();
    return GetStrHook::Callback(str_id);
}

static const char* getBtnSeNameImp(int str_id, int btnSe_id) {
    auto it = btnSeStrMap.find(btnSe_id);
    if (it != btnSeStrMap.end()) return it->second.c_str();
    return GetStrHook::Callback(str_id);
}

static const char* getChainSlideSeNameImp(int str_id, int chainSlideSe_id) {
    auto it = chainSlideSeStrMap.find(chainSlideSe_id);
    if (it != chainSlideSeStrMap.end()) return it->second.c_str();
    return GetStrHook::Callback(str_id);
}

static const char* getSliderTouchSeNameImp(int str_id, int sliderTouchSe_id) {
    auto it = sliderTouchSeStrMap.find(sliderTouchSe_id);
    if (it != sliderTouchSeStrMap.end()) return it->second.c_str();
    return GetStrHook::Callback(str_id);
}

static const char* getSlideSeNameImp(int str_id, int slideSe_id) {
    auto it = slideSeStrMap.find(slideSe_id);
    if (it != slideSeStrMap.end()) return it->second.c_str();
    return GetStrHook::Callback(str_id);
}

// INLINE HOOKS
HOOK_DEFINE_INLINE(CallModuleNameHook) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->X[0] = reinterpret_cast<uint64_t>(getModuleNameImp(ctx->X[0], ctx->X[8]));
    }
};

HOOK_DEFINE_INLINE(CallCustomizeNameHook) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->X[0] = reinterpret_cast<uint64_t>(getCustomizeNameImp(ctx->X[0], ctx->X[8]));
    }
};

HOOK_DEFINE_INLINE(CallBtnSeNameHook) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->X[0] = reinterpret_cast<uint64_t>(getBtnSeNameImp(ctx->X[0], ctx->X[8]));
    }
};

HOOK_DEFINE_INLINE(CallChainSlideNameHook) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->X[0] = reinterpret_cast<uint64_t>(getChainSlideSeNameImp(ctx->X[0], ctx->X[8]));
    }
};

HOOK_DEFINE_INLINE(CallSliderTouchSeNameHook) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->X[0] = reinterpret_cast<uint64_t>(getSliderTouchSeNameImp(ctx->X[0], ctx->X[8]));
    }
};

HOOK_DEFINE_INLINE(CallSlideSeNameHook) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->X[0] = reinterpret_cast<uint64_t>(getSlideSeNameImp(ctx->X[0], ctx->X[8]));
    }
};

// INITIALIZATION
template <typename HookType>
inline void ReplaceWithNopAndHook(uintptr_t addr) {
    exl::patch::CodePatcher(addr).Write<uint32_t>(ARM64_NOP);
    HookType::InstallAtOffset(addr);
}

namespace StrArray {
    void init() {
        LoadStrArrayHook::InstallAtOffset(ADDR_LOAD_STR_ARRAY);
        GetStrHook::InstallAtOffset(ADDR_GET_STR);

        ReplaceWithNopAndHook<CallModuleNameHook>(ADDR_CALL_MODULE_NAME);
        ReplaceWithNopAndHook<CallCustomizeNameHook>(ADDR_CALL_CUSTOMIZE_NAME);
        ReplaceWithNopAndHook<CallBtnSeNameHook>(ADDR_CALL_BTN_SE_NAME);
        ReplaceWithNopAndHook<CallChainSlideNameHook>(ADDR_CALL_CHAIN_SLIDE_NAME);
        ReplaceWithNopAndHook<CallSliderTouchSeNameHook>(ADDR_CALL_SLIDER_TOUCH);
        ReplaceWithNopAndHook<CallSlideSeNameHook>(ADDR_CALL_SLIDE_SE_NAME);
    }
}
