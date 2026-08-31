#pragma once
#include "lib.hpp"
#include "Config.hpp"

#define FIX(addr) ((addr) - 0x100)

#define ADDR_MOD_LIMIT_1      FIX(0x000C4E50)
#define ADDR_MOD_LIMIT_2      FIX(0x000C8350)

#define ADDR_INLINE_MOD_LIMIT_1 FIX(0x000C4D2C)
#define ADDR_INLINE_MOD_LIMIT_2 FIX(0x000C6C90)
#define ADDR_INLINE_MOD_LIMIT_3 FIX(0x000C716C)
#define ADDR_INLINE_MOD_LIMIT_4 FIX(0x000C73C8)

#define ADDR_EFF_LIMIT            FIX(0x0017B710)
#define ADDR_LYRIC_LIMIT          FIX(0x004C3B18)

#define ADDR_COS_LIMIT_1 FIX(0x000C6F0C)
#define ADDR_COS_LIMIT_2 FIX(0x0076D9C4)
#define ADDR_COS_LIMIT_3 FIX(0x00519CA0)
#define ADDR_COS_LIMIT_3_CSEL_FIX FIX(0x00519CB0)

// Challenge time patches
#define ADDR_CHALLENGE_TIME_1     FIX(0x0018C224)
#define ADDR_CHALLENGE_TIME_2     FIX(0x0018C244)
#define ADDR_CHALLENGE_TIME_3     FIX(0x0018BCB8)

// Optional Gameplay Patches from IPSwitch
#define ADDR_WATERMARK_1          0x00AC9E1A
#define ADDR_WATERMARK_2          0x00AB7545

#define ADDR_HAND_SCALING         0x00185EC0

#define ADDR_LYRICS_DISABLE_1     0x00AD358C
#define ADDR_LYRICS_DISABLE_2     0x00AE87DF

#define ADDR_FORCE_JP_1           0x0021C964
#define ADDR_FORCE_JP_2           0x0021C974

#define ADDR_UNLOCK_PATCH         FIX(0x0CA400)

#define OFFSET_SATURATION_1       FIX(0x5D1B64)
#define OFFSET_SATURATION_2       FIX(0x5D29B0)

inline void ApplyCustomPatches() {

    // Challenge Time Handling (3 Modes: "enabled", "disabled", "default")
    if (Config::challengeTimeMode == "enabled") {
        exl::patch::CodePatcher(ADDR_CHALLENGE_TIME_1).Write<uint32_t>(0x320003FA);
        exl::patch::CodePatcher(ADDR_CHALLENGE_TIME_2).Write<uint32_t>(0x7100011F);
        exl::patch::CodePatcher(ADDR_CHALLENGE_TIME_3).Write<uint32_t>(0x7100011F);
    } else if (Config::challengeTimeMode == "disabled") {
        exl::patch::CodePatcher(ADDR_CHALLENGE_TIME_1).Write<uint32_t>(0x52800000);
        exl::patch::CodePatcher(ADDR_CHALLENGE_TIME_2).Write<uint32_t>(0x52800000);
        exl::patch::CodePatcher(ADDR_CHALLENGE_TIME_3).Write<uint32_t>(0x52800000);
    }

    // ExPatch (Unlock all difficulty levels)
    if (Config::ExPatch) {
        exl::patch::CodePatcher(ADDR_UNLOCK_PATCH).Write<uint32_t>(0x52800020);
    }

    // 1. Remove Copyright & PV Watermark
    if (Config::removeWatermarks) {
        exl::patch::CodePatcher(ADDR_WATERMARK_1).Write<uint8_t>(0x00);
        exl::patch::CodePatcher(ADDR_WATERMARK_2).Write<uint8_t>(0x00);
    }

    // 2. Disable Hand Scaling
    if (Config::disableHandScaling) {
        exl::patch::CodePatcher(ADDR_HAND_SCALING).Write<uint32_t>(0x14000258);
    }

    // 3. Disable Lyrics Display
    if (Config::disableLyrics) {
        exl::patch::CodePatcher(ADDR_LYRICS_DISABLE_1).Write<uint8_t>(0x00);
        exl::patch::CodePatcher(ADDR_LYRICS_DISABLE_2).Write<uint8_t>(0x00);
    }

    // 4. Force Japanese Language/Region
    if (Config::forceJapanese) {
        exl::patch::CodePatcher(ADDR_FORCE_JP_1).Write<uint32_t>(0x52800000);
        exl::patch::CodePatcher(ADDR_FORCE_JP_2).Write<uint32_t>(0x52800008);
    }

    // 5. FT UI leftovers
    if (Config::forceFtUI) {
        exl::patch::CodePatcher(0xAE7EA8).Write<uint64_t>(0x00347370646F6D2D);
        exl::patch::CodePatcher(0xACC6DF).Write<uint8_t>(0x00);
    }

    // Core patches: Module limits
    exl::patch::CodePatcher(ADDR_MOD_LIMIT_1 + 8).Write<uint32_t>(0x2A0103E8);

    // Rewrite reader function to bypass module limit and protect against null dereference
    exl::patch::CodePatcher(ADDR_MOD_LIMIT_2 + 0x00).Write<uint32_t>(0xB4000060); // cbz x0, +0xC (jump if ptr is null)
    exl::patch::CodePatcher(ADDR_MOD_LIMIT_2 + 0x04).Write<uint32_t>(0xB9400000); // ldr w0, [x0] (read ID if valid)
    exl::patch::CodePatcher(ADDR_MOD_LIMIT_2 + 0x08).Write<uint32_t>(0xD65F03C0); // ret
    exl::patch::CodePatcher(ADDR_MOD_LIMIT_2 + 0x0C).Write<uint32_t>(0x2A1F03E0); // mov w0, wzr (return 0 if null)
    exl::patch::CodePatcher(ADDR_MOD_LIMIT_2 + 0x10).Write<uint32_t>(0xD65F03C0); // ret

    // Inlined module limit patches
    exl::patch::CodePatcher(ADDR_INLINE_MOD_LIMIT_1).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(ADDR_INLINE_MOD_LIMIT_2).Write<uint32_t>(0x2A0803E1);
    exl::patch::CodePatcher(ADDR_INLINE_MOD_LIMIT_3).Write<uint32_t>(0x2A1B03E9);
    exl::patch::CodePatcher(ADDR_INLINE_MOD_LIMIT_4).Write<uint32_t>(0x2A1A03E9);

    // Costume limit patches
    exl::patch::CodePatcher(ADDR_COS_LIMIT_2).Write<uint32_t>(0x2A0003F8);
    exl::patch::CodePatcher(ADDR_COS_LIMIT_3).Write<uint32_t>(0x2A1F03E9);
    exl::patch::CodePatcher(ADDR_COS_LIMIT_3_CSEL_FIX).Write<uint32_t>(0x2A1403F7);

    // Saturation song patch
    exl::patch::CodePatcher(OFFSET_SATURATION_1).Write<uint32_t>(0x710F951F);
    exl::patch::CodePatcher(OFFSET_SATURATION_2).Write<uint32_t>(0x710F903F);

    // Effect and open lyrics limits
    exl::patch::CodePatcher(ADDR_EFF_LIMIT).Write<uint32_t>(0x7103FC3F);
    exl::patch::CodePatcher(ADDR_LYRIC_LIMIT).Write<uint32_t>(0xF10FA27F);
}
