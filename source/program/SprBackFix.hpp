#pragma once

#include "lib.hpp"
#include "nc/diva_nc.hpp"
#include "Mode2DNoMV.hpp"

namespace BackgroundScaleFix {

    static constexpr uintptr_t ADDR_DRAW_SPRITE_FUNC  = 0x00612310;
    static constexpr uintptr_t ADDR_GET_ACTIVE_SCALE  = 0x0020C190;
    static constexpr uintptr_t ADDR_INLINE_READ_SCREEN = 0x00618FF0;

    static constexpr int32_t SCREEN_MODE_HDTV720       = 0x0D; // 13
    static constexpr int32_t SCREEN_MODE_HDTV1080      = 0x0E; // 14

    using GetActiveScaleFn = float (*)();

    static int32_t s_captured_real_screen_mode = -1;

    static inline bool IsInGameplay() {
        PVGameData* pv_game = GetPVGameData();
        if (pv_game == nullptr || !pv_game->loaded) return false;
        if (IsInSongResults()) return false;
        return true;
    }

    static inline bool IsTutorialSong() {
        auto pv_info = game::GetGlobalPVInfo();
        if (pv_info != nullptr) {
            int32_t pv_id = pv_info->pv_id;
            if (pv_id >= 700 && pv_id <= 709) return true;
        }

        auto gameplay_info = GetPvGameplayInfo();
        if (gameplay_info != nullptr && gameplay_info->type == 4) return true;

        return false;
    }

    HOOK_DEFINE_INLINE(ReadScreenModeInlineHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            uintptr_t set_node = ctx->X[23];
            uint32_t spr_idx   = static_cast<uint32_t>(ctx->X[8] & 0xFFFF);

            if (set_node != 0) {
                uintptr_t p1 = *reinterpret_cast<uintptr_t*>(set_node + 0x40);
                if (p1 != 0) {
                    uintptr_t spr_data = *reinterpret_cast<uintptr_t*>(p1 + 0x28);
                    if (spr_data != 0) {
                        s_captured_real_screen_mode = *reinterpret_cast<int32_t*>(spr_data + spr_idx * 8 + 4);
                    }
                }
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DrawSpriteBackFixHook) {
        static SprArgs* Callback(SprArgs* args) {
            if (args == nullptr) {
                return Orig(args);
            }

            s_captured_real_screen_mode = -1;

            SprArgs* ret = Orig(args);

            if (ret != nullptr && IsInGameplay() && !IsTutorialSong()) {
                int32_t target = *reinterpret_cast<int32_t*>((uintptr_t)ret + 0x14);

                // Target 2 corresponds to stage background layer (BG / IMAGE)
                if (target == 2) {
                    uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
                    auto GetActiveScale = reinterpret_cast<GetActiveScaleFn>(base + ADDR_GET_ACTIVE_SCALE);
                    float active_scale = GetActiveScale ? GetActiveScale() : 1.0f;
                    if (active_scale <= 0.01f || active_scale > 2.0f) active_scale = 1.0f;

                    bool should_apply_custom_scale = false;
                    float scale_x = 1.0f;
                    float scale_y = 1.0f;

                    // 1. Forced 2D replacement (BG mode)
                    if (Mode2DNoMV::g_isBgSubstituted) {
                        should_apply_custom_scale = true;
                        if (ret->sprite_size.x > 0.0f && ret->sprite_size.y > 0.0f) {
                            scale_x = (1920.0f / ret->sprite_size.x) * active_scale;
                            scale_y = (1080.0f / ret->sprite_size.y) * active_scale;
                        } else {
                            scale_x = 1.5f * active_scale;
                            scale_y = 1.5f * active_scale;
                        }
                    }
                    // 2. Native IMAGE sprite (e.g. native 2D songs).
                    // Only scale if aspect ratio matches 16:9 widescreen and dimensions are within 1080p,
                    // avoiding square skybox textures or sub-elements.
                    else {
                        int32_t real_mode = s_captured_real_screen_mode;
                        if (real_mode == -1) {
                            uint32_t spr_id = *reinterpret_cast<uint32_t*>((uintptr_t)ret + 0x04);
                            if ((spr_id & 0xF0000000) == 0x10000000) {
                                real_mode = SCREEN_MODE_HDTV720;
                            } else {
                                real_mode = *reinterpret_cast<int32_t*>((uintptr_t)ret + 0x24);
                            }
                        }

                        if (real_mode == SCREEN_MODE_HDTV1080 || real_mode == SCREEN_MODE_HDTV720) {
                            float aspect = (ret->sprite_size.y > 0.0f) ? (ret->sprite_size.x / ret->sprite_size.y) : 0.0f;

                            if (aspect >= 1.75f && aspect <= 1.80f && ret->sprite_size.x <= 1920.0f && ret->sprite_size.y <= 1080.0f) {
                                should_apply_custom_scale = true;

                                // Target resolution depends on screen mode
                                float target_w = (real_mode == SCREEN_MODE_HDTV1080) ? 1920.0f : 1280.0f;
                                float target_h = (real_mode == SCREEN_MODE_HDTV1080) ? 1080.0f : 720.0f;

                                // Scale to match canvas viewport
                                scale_x = (target_w / ret->sprite_size.x) * active_scale;
                                scale_y = (target_h / ret->sprite_size.y) * active_scale;
                            }
                        }
                    }

                    // Return unmodified if scaling is not applicable
                    if (!should_apply_custom_scale) {
                        return ret;
                    }

                    float off = 720.0f * (1.0f - active_scale);

                    ret->attr &= ~0x800;
                    ret->trans = { 0.0f, off, 0.0f };
                    ret->scale = { scale_x, scale_y, 1.0f };
                    ret->mat.row0 = { 1.0f, 0.0f, 0.0f, 0.0f };
                    ret->mat.row1 = { 0.0f, 1.0f, 0.0f, 0.0f };
                    ret->mat.row2 = { 0.0f, 0.0f, 1.0f, 0.0f };
                    ret->mat.row3 = { 0.0f, 0.0f, 0.0f, 1.0f };
                }
            }

            return ret;
        }
    };

    void init() {
        ReadScreenModeInlineHook::InstallAtOffset(ADDR_INLINE_READ_SCREEN);
        DrawSpriteBackFixHook::InstallAtOffset(ADDR_DRAW_SPRITE_FUNC);
    }
}
