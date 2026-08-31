#include "SpriteLoader.hpp"
#include "lib.hpp"
#include "macros.hpp"

// =========================================================
// NSO Offsets (Ghidra - 0x100)
// =========================================================

// --- Limit Masks (0xFFF -> 0x7FFF) ---
#define ADDR_SPRITE_MASK_1        FIX(0x001DD228) 
#define ADDR_SPRITE_MASK_2        FIX(0x00618E6C) 
#define ADDR_SPRITE_MASK_3        FIX(0x0061C4A8) // FUN_0061c4a0
#define ADDR_SPRITE_MASK_4        FIX(0x0061C53C) // FUN_0061c530
#define ADDR_SPRITE_MASK_5        FIX(0x0061C608) // FUN_0061c600

// --- System Flags (0x10000000 -> 0x80000000) ---
#define ADDR_SPRITE_FLAG_AND      FIX(0x00618EF8) 
#define ADDR_SPRITE_FLAG_CMP1     FIX(0x00618EFC) 
#define ADDR_SPRITE_FLAG_CMP2     FIX(0x00619090) 
#define ADDR_SPRITE_FLAG_5        FIX(0x0061DED4) 
#define ADDR_SPRITE_FLAG_6        FIX(0x00614F20) 

// --- Flag Getters (LSR by 31 bits) ---
#define ADDR_SPRITE_FINAL_CHECK   FIX(0x006151D0)
#define ADDR_SPRITE_GETTER_1      FIX(0x0061C504)
#define ADDR_SPRITE_GETTER_2      FIX(0x0061C57C)

// --- Loader Hook ---
#define ADDR_FIXUP_STORE          FIX(0x0061CA1C)

HOOK_DEFINE_INLINE(SpriteFixupHook) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        uint32_t val = ctx->W[9]; 
        uint32_t flags = (val & 0xF0000000) << 3; 
        uint32_t id = (val & 0x0FFFFFFF);         
        ctx->W[9] = flags | id; 
    }
};

namespace SpriteLoader {
    void init() {
        // 1. Limit masks patch (Expanding up to 32K sprites)
        exl::patch::CodePatcher(ADDR_SPRITE_MASK_1).Write<uint32_t>(0x12003901); // and w1, w8, #0x7fff
        exl::patch::CodePatcher(ADDR_SPRITE_MASK_2).Write<uint32_t>(0x53107AA8); // ubfx w8, w21, #16, #15
        exl::patch::CodePatcher(ADDR_SPRITE_MASK_3).Write<uint32_t>(0x53107829); // ubfx w9, w1, #16, #15
        exl::patch::CodePatcher(ADDR_SPRITE_MASK_4).Write<uint32_t>(0x53107829); // ubfx w9, w1, #16, #15
        exl::patch::CodePatcher(ADDR_SPRITE_MASK_5).Write<uint32_t>(0x53107829); // ubfx w9, w1, #16, #15

        // 2. Main flag patches
        exl::patch::CodePatcher(ADDR_SPRITE_FLAG_AND).Write<uint32_t>(0x12010109);  // and w9, w8, #0x80000000
        exl::patch::CodePatcher(ADDR_SPRITE_FLAG_CMP1).Write<uint32_t>(0x320103EA); // mov w10, #0x80000000
        exl::patch::CodePatcher(ADDR_SPRITE_FLAG_CMP2).Write<uint32_t>(0x320103EA); // mov w10, #0x80000000
        exl::patch::CodePatcher(ADDR_SPRITE_FLAG_5).Write<uint32_t>(0x32010116);    // orr w22, w8, #0x80000000
        exl::patch::CodePatcher(ADDR_SPRITE_FLAG_6).Write<uint32_t>(0x3201014A);    // orr w10, w10, #0x80000000

        // 3. Flag getters fix 
        exl::patch::CodePatcher(ADDR_SPRITE_FINAL_CHECK).Write<uint32_t>(0x531F7C29); // lsr w9, w1, #0x1f
        exl::patch::CodePatcher(ADDR_SPRITE_GETTER_1).Write<uint32_t>(0x531F7C29);    // lsr w9, w1, #0x1f
        exl::patch::CodePatcher(ADDR_SPRITE_GETTER_2).Write<uint32_t>(0x531F7C2A);    // lsr w10, w1, #0x1f

        // 4. Install Loader hook
        SpriteFixupHook::InstallAtOffset(ADDR_FIXUP_STORE);
    }
}
