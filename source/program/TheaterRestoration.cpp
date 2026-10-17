#include "TheaterRestoration.hpp"
#include "lib.hpp"
#include "nc/diva_nc.hpp"
#include <cstring>

namespace TheaterRestoration {

    struct MovieEntry {
        const char* movie_path;
        const char* sound_path;
        const char* menu_layer;
        int32_t     picture_id;
    };

    static const MovieEntry s_movies[5] = {
        { "rom/movie/MEGA39sOP.mp4",                "rom/sound/bgm/MEGA39sOP.ogg",          "theater_menu_05", 5 },
        { "rom/movie/diva_adv_FHD.mp4",             "rom/sound/bgm/diva_ac_adv.ogg",        "theater_menu_01", 1 },
        { "rom/movie/diva_adv_B_FHD.mp4",           "rom/sound/bgm/adv_2mix_master.ogg",    "theater_menu_02", 2 },
        { "rom/movie/diva_adv_ft_FHD.mp4",          "rom/sound/bgm/divaft_advertise.ogg",   "theater_menu_03", 3 },
        { "rom/movie/diva_adv_photostudio_FHD.mp4", "rom/sound/bgm/diva_adv_C_MIX_T3.ogg", "theater_menu_04", 4 }
    };

    constexpr size_t BUTTON_SIZE = 0x1D0;
    alignas(16) static uint8_t s_extra_buttons[4 * BUTTON_SIZE] = {0};

    static inline void* GetButtonPtr(uintptr_t theater, int index) {
        if (index == 0) return reinterpret_cast<void*>(theater + 0x148);
        return &s_extra_buttons[(index - 1) * BUTTON_SIZE];
    }

    using ButtonCtorFn_t  = void (*)(void* btn);
    using ButtonDtorFn_t  = void (*)(void* btn);
    using ButtonInitFn_t  = void (*)(void* btn, const char* name, int32_t p3, int32_t p4, int32_t scene_id);
    using ButtonPlayFn_t  = void (*)(void* btn);
    using ButtonStopFn_t  = void (*)(void* btn);
    using ButtonResetFn_t = void (*)(void* btn);

    // =========================================================================
    // 1. INITIALIZE ALL 5 BUTTONS (0x00801dd0)
    // =========================================================================
    HOOK_DEFINE_TRAMPOLINE(TheaterInitHook) {
        static uint64_t Callback(uintptr_t theater) {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            uint64_t ret = Orig(theater);

            auto ButtonCtor = reinterpret_cast<ButtonCtorFn_t>(base + 0x0071d900);
            auto ButtonInit = reinterpret_cast<ButtonInitFn_t>(base + 0x0071dc80);
            auto ButtonPlay = reinterpret_cast<ButtonPlayFn_t>(base + 0x0071de70);

            for (int i = 1; i < 5; i++) {
                void* btn = GetButtonPtr(theater, i);
                ButtonCtor(btn);
                ButtonInit(btn, s_movies[i].menu_layer, 0xb, 0, 0x524);
                ButtonPlay(btn);
            }

            return ret;
        }
    };

    // =========================================================================
    // 2. CONFIGURE 5 MENU ITEMS (0x008015b0)
    // =========================================================================
    HOOK_DEFINE_INLINE(TheaterMenuCountPatch) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            ctx->X[8] = 0x0000000500000005ULL;
        }
    };

    // =========================================================================
    // 2.1 PRESERVE SELECTED INDEX ON MENU RECREATION (0x008015dc)
    // =========================================================================
    // Write actual index immediately after 008015d8: str wzr, [sp, #0x28]
    HOOK_DEFINE_INLINE(TheaterMenuInitialIndexPatch) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t theater = ctx->X[19];
            uint8_t cur_idx = *reinterpret_cast<uint8_t*>(theater + 0x32c);
            if (cur_idx >= 5) cur_idx = 0;

            // Calculate original SP via X29 (Frame Pointer)
            uintptr_t sp = ctx->X[29] - 0x110;
            *reinterpret_cast<uint32_t*>(sp + 0x28) = cur_idx;
        }
    };

    // =========================================================================
    // 3. BUTTON POINTER OVERRIDES IN LOOPS (0x008017b0 and 0x008018b4)
    // =========================================================================
    HOOK_DEFINE_INLINE(TheaterLoop1ButtonPtrPatch) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t theater = ctx->X[19];
            int32_t idx = ctx->W[24];
            if (idx >= 0 && idx < 5) {
                ctx->X[21] = reinterpret_cast<uintptr_t>(GetButtonPtr(theater, idx));
            }
        }
    };

    HOOK_DEFINE_INLINE(TheaterLoop2ButtonPtrPatch) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t theater = ctx->X[20] - 0x68;
            int32_t idx = ctx->W[23];
            if (idx >= 0 && idx < 5) {
                ctx->X[19] = reinterpret_cast<uintptr_t>(GetButtonPtr(theater, idx));
            }
        }
    };

    // =========================================================================
    // 4. SAVE SELECTED INDEX ON 'A' BUTTON PRESS (0x00801298)
    // =========================================================================
    HOOK_DEFINE_INLINE(TheaterSaveSelectedIndexPatch) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t theater = ctx->X[19];
            uint8_t cur_idx = *reinterpret_cast<uint8_t*>(theater + 0x32c);
            if (cur_idx >= 5) cur_idx = 0;
            *reinterpret_cast<int32_t*>(theater + 0x33c) = cur_idx;
        }
    };

    // =========================================================================
    // 5. PREVIEW IMAGE PATCHES IN 0x00801340 (+4 BYTE OFFSET)
    // =========================================================================
    // Patch 1 (0x00801394): Initial preview image loading when menu opens
    HOOK_DEFINE_INLINE(TheaterPictureIdPatch1) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t theater = ctx->X[19];
            int32_t cur_idx = *reinterpret_cast<uint8_t*>(theater + 0x32c);
            if (cur_idx < 0 || cur_idx >= 5) cur_idx = 0;
            ctx->W[1] = s_movies[cur_idx].picture_id;
        }
    };

    // Patch 2 (0x00801414): Outgoing preview image animation on focus change
    HOOK_DEFINE_INLINE(TheaterPictureIdPatch2) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t theater = ctx->X[19];
            int32_t cur_idx = *reinterpret_cast<uint8_t*>(theater + 0x32c);
            if (cur_idx < 0 || cur_idx >= 5) cur_idx = 0;
            ctx->W[1] = s_movies[cur_idx].picture_id;
        }
    };

    // Patch 3 (0x008014a4): Incoming preview image animation
    HOOK_DEFINE_INLINE(TheaterPictureIdPatch3) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t theater = ctx->X[19];
            int32_t cur_idx = *reinterpret_cast<uint8_t*>(theater + 0x32c);
            if (cur_idx < 0 || cur_idx >= 5) cur_idx = 0;
            ctx->W[1] = s_movies[cur_idx].picture_id;
        }
    };

    // =========================================================================
    // 6. PLAY SELECTED MOVIE AND AUDIO (0x00801ad0)
    // =========================================================================
    HOOK_DEFINE_TRAMPOLINE(TheaterPlayMovieHook) {
        static uint64_t Callback(uintptr_t theater) {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

            int32_t idx = *reinterpret_cast<int32_t*>(theater + 0x33c);
            if (idx < 0 || idx >= 5) {
                idx = *reinterpret_cast<uint8_t*>(theater + 0x32c);
                if (idx < 0 || idx >= 5) idx = 0;
            }

            auto movie_table = reinterpret_cast<const char**>(base + 0x00cd45e0);
            auto sound_table = reinterpret_cast<const char**>(base + 0x00cd4600);

            movie_table[0] = s_movies[idx].movie_path;
            sound_table[0] = s_movies[idx].sound_path;

            *reinterpret_cast<int32_t*>(theater + 0x33c) = 0;

            return Orig(theater);
        }
    };

    // =========================================================================
    // 7. BUTTON RESET AND DESTRUCTORS
    // =========================================================================
    HOOK_DEFINE_TRAMPOLINE(TheaterStopAetHook) {
        static uint64_t Callback(uintptr_t theater) {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            auto ButtonStop  = reinterpret_cast<ButtonStopFn_t>(base + 0x0071dc50);
            auto ButtonReset = reinterpret_cast<ButtonResetFn_t>(base + 0x0071dbd0);

            for (int i = 1; i < 5; i++) {
                void* btn = GetButtonPtr(theater, i);
                ButtonStop(btn);
                ButtonReset(btn);
            }

            return Orig(theater);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(TheaterDtorHook1) {
        static void Callback(void* theater) {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            auto ButtonDtor = reinterpret_cast<ButtonDtorFn_t>(base + 0x0071db20);
            for (int i = 1; i < 5; i++) {
                void* btn = GetButtonPtr(reinterpret_cast<uintptr_t>(theater), i);
                ButtonDtor(btn);
            }
            Orig(theater);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(TheaterDtorHook2) {
        static void Callback(void* theater) {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            auto ButtonDtor = reinterpret_cast<ButtonDtorFn_t>(base + 0x0071db20);
            for (int i = 1; i < 5; i++) {
                void* btn = GetButtonPtr(reinterpret_cast<uintptr_t>(theater), i);
                ButtonDtor(btn);
            }
            Orig(theater);
        }
    };

    // =========================================================================
    // INITIALIZATION
    // =========================================================================
    void init() {
        // 1. Initialize 5 buttons
        TheaterInitHook::InstallAtOffset(0x00801dd0);

        // 2. Expand menu configuration to 5 items
        TheaterMenuCountPatch::InstallAtOffset(0x008015b0);

        // 2.1 Restore selected index instead of resetting to 0
        TheaterMenuInitialIndexPatch::InstallAtOffset(0x008015dc);

        // 3. Override button pointers in loops
        TheaterLoop1ButtonPtrPatch::InstallAtOffset(0x008017b0);
        TheaterLoop2ButtonPtrPatch::InstallAtOffset(0x008018b4);

        // 4. Patch selected movie index save on 'A' press
        TheaterSaveSelectedIndexPatch::InstallAtOffset(0x00801298);

        // 5. Preview images: offsets shifted by +4 bytes after 'mov w1, #5'
        TheaterPictureIdPatch1::InstallAtOffset(0x00801394);
        TheaterPictureIdPatch2::InstallAtOffset(0x00801414);
        TheaterPictureIdPatch3::InstallAtOffset(0x008014a4);

        // 6. Play selected movie and sound
        TheaterPlayMovieHook::InstallAtOffset(0x00801ad0);

        // 7. Reset button states on exit
        TheaterStopAetHook::InstallAtOffset(0x008020b0);

        // 8. Button destructors
        TheaterDtorHook1::InstallAtOffset(0x00802280);
        TheaterDtorHook2::InstallAtOffset(0x008022d0);
    }

} // namespace TheaterRestoration
