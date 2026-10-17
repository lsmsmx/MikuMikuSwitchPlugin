#include "Mode2DNoMV.hpp"
#include "Config.hpp"
#include "PvLoader.hpp"
#include "lib.hpp"
#include <nn/os.hpp>

#include "logger.hpp"
#include "nc/diva_nc.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_map>

#define ADDR_PARSE_DSC_FUNC 0x00181580

namespace Mode2DNoMV {

    bool is3D = false;
    bool hasMovie = false;

    // Flag states
    bool g_force2D = false;
    bool g_noMovie = false;
    bool g_isBgSubstituted = false;

    // Scene parser and loader hook offsets
    static constexpr uintptr_t ADDR_INLINE_CAPTURE_PVID  = 0x001900dc;
    static constexpr uintptr_t ADDR_INLINE_DISP2D_CHECK  = 0x00190140;
    static constexpr uintptr_t ADDR_INLINE_MOTION_CHECK  = 0x001911b4;
    static constexpr uintptr_t ADDR_INLINE_PV_EXP_CHECK  = 0x001917e8;
    static constexpr uintptr_t ADDR_INLINE_CHREFF_CHECK  = 0x001935a0;
    static constexpr uintptr_t ADDR_INLINE_PRE_ALLOC     = 0x00193ffc;
    static constexpr uintptr_t ADDR_INLINE_CHECK_FIELD   = 0x00194800;
    static constexpr uintptr_t ADDR_INLINE_GET_SPRITE    = 0x001948b0;
    static constexpr uintptr_t ADDR_STAGE_COLI_LOAD_FUNC = 0x0062d970;

    // Input polling hook offset (used for detecting song exit)
    static constexpr uintptr_t ADDR_GET_INPUT_STATE_FUNC = 0x001fd450;

    // Sprite database and rendering offsets
    static constexpr uintptr_t ADDR_GET_SPRITE_ID_FUNC   = 0x0061db40;
    static constexpr uintptr_t OFFSET_SPR_DB_PTR         = 0x00cdf360;

    // Movie blocking offsets
    static constexpr uintptr_t ADDR_TASK_MOVIE_OPEN      = 0x00439FD0;
    static constexpr uintptr_t ADDR_TASK_MOVIE_READY     = 0x0043A310;

    // GPU and folder patches
    static constexpr uintptr_t ADDR_OBJ_FOLDER           = 0x00AC6868;
    static constexpr uintptr_t ADDR_GPU_PASS_1           = 0x004F14D0;
    static constexpr uintptr_t ADDR_GPU_PASS_2           = 0x004F15BC;
    static constexpr uintptr_t ADDR_GPU_PASS_3           = 0x004F15FC;

    using GetSpriteId_t = int32_t* (*)(void* spr_db, stringRange* range);

    static int32_t s_currentPvId      = -1;
    static int32_t s_fallbackSpriteId = -1;
    static bool    s_is2DActiveNow     = false;
    static bool    s_wasInGameplay     = false;

    // State structures for safe 3D stage field restoration
    struct SavedDiff {
        uintptr_t begin;
        uintptr_t end;
        uintptr_t cap;
    };
    static std::unordered_map<uintptr_t, SavedDiff> s_savedDiffs;
    static std::unordered_map<uint8_t*, std::string> s_savedFieldNames;

    static char      s_origObjFolder[16] = {0};
    static uint32_t  s_origGpuInsn1      = 0;
    static uint32_t  s_origGpuInsn2      = 0;
    static uint32_t  s_origGpuInsn3      = 0;
    static bool      s_isCached          = false;

    static uint8_t s_dummyFields[0x220 * 2] = {0};

    bool ShouldBlockMovie() {
        return g_noMovie;
    }

    bool ShouldApply2D() {
        if (g_force2D) return true;
        if (is3D) return false;

        int32_t pv_id = (s_currentPvId > 0) ? s_currentPvId : game::GetGlobalPvID();
        if (ShouldBlockMovie() && PvLoader::IsMoviePv(pv_id)) {
            return true;
        }

        return false;
    }

    static inline bool IsInGameplay() {
        PVGameData* pv_game = GetPVGameData();
        if (pv_game == nullptr || !pv_game->loaded) return false;
        if (IsInSongResults()) return false;
        return true;
    }

    static void CacheOriginalBytes() {
        if (s_isCached) return;
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        std::memcpy(s_origObjFolder, reinterpret_cast<void*>(base + ADDR_OBJ_FOLDER), 12);
        s_origGpuInsn1 = *reinterpret_cast<uint32_t*>(base + ADDR_GPU_PASS_1);
        s_origGpuInsn2 = *reinterpret_cast<uint32_t*>(base + ADDR_GPU_PASS_2);
        s_origGpuInsn3 = *reinterpret_cast<uint32_t*>(base + ADDR_GPU_PASS_3);

        s_isCached = true;
    }

    static void SetDynamicPatches(bool enable) {
        if (!s_isCached) CacheOriginalBytes();

        if (enable) {
            if (s_is2DActiveNow) return;
            const char dummy_folder[] = "rom/no_obj/";
            for (size_t i = 0; i < sizeof(dummy_folder); i++) {
                exl::patch::CodePatcher(ADDR_OBJ_FOLDER + i).Write<uint8_t>(dummy_folder[i]);
            }
            exl::patch::CodePatcher(ADDR_GPU_PASS_1).Write<uint32_t>(0xA900235F);
            exl::patch::CodePatcher(ADDR_GPU_PASS_2).Write<uint32_t>(0x3912851F);
            exl::patch::CodePatcher(ADDR_GPU_PASS_3).Write<uint32_t>(0x2A1F03E8);
            s_is2DActiveNow = true;
        } else {
            // Restore original rom/obj/ folder string and GPU instructions
            for (size_t i = 0; i < 12; i++) {
                exl::patch::CodePatcher(ADDR_OBJ_FOLDER + i).Write<uint8_t>(s_origObjFolder[i]);
            }
            exl::patch::CodePatcher(ADDR_GPU_PASS_1).Write<uint32_t>(s_origGpuInsn1);
            exl::patch::CodePatcher(ADDR_GPU_PASS_2).Write<uint32_t>(s_origGpuInsn2);
            exl::patch::CodePatcher(ADDR_GPU_PASS_3).Write<uint32_t>(s_origGpuInsn3);
            s_is2DActiveNow = false;
        }
    }

    void RestoreOriginalFields() {
        SetDynamicPatches(false);

        // 1. Restore original 3D stage field names in pv_db
        for (const auto& [ptr, orig_name] : s_savedFieldNames) {
            int len = static_cast<int>(orig_name.length());
            ptr[0] = static_cast<uint8_t>(len << 1);
            std::memcpy(ptr + 1, orig_name.c_str(), len + 1);
        }
        s_savedFieldNames.clear();

        // 2. Restore original diff_ptr vectors
        for (const auto& [diff_ptr, saved] : s_savedDiffs) {
            *reinterpret_cast<uintptr_t*>(diff_ptr + 0x190) = saved.begin;
            *reinterpret_cast<uintptr_t*>(diff_ptr + 0x198) = saved.end;
            *reinterpret_cast<uintptr_t*>(diff_ptr + 0x1a0) = saved.cap;
        }
        s_savedDiffs.clear();
    }

    // =========================================================================
    // SONG EXIT MONITORING (PAUSE, RETRY, CLEAR, DEATH)
    // =========================================================================
    inline void CheckSongExit() {
        bool in_game = IsInGameplay();

        // Transitioned from gameplay to menu or results
        if (s_wasInGameplay && !in_game) {
            RestoreOriginalFields();
            s_currentPvId = -1;
            is3D = false;
            hasMovie = false;
        }

        s_wasInGameplay = in_game;
    }

    HOOK_DEFINE_TRAMPOLINE(FrameUpdateHook) {
        static diva_nc::InputState* Callback(int32_t index) {
            CheckSongExit();
            return Orig(index);
        }
    };

    // =========================================================================
    // MOVIE PLAYBACK BLOCKING
    // =========================================================================
    HOOK_DEFINE_TRAMPOLINE(TaskMovie_OpenFile_Hook) {
        static uint64_t Callback(uintptr_t this_ptr, const char* file_path) {
            if (ShouldBlockMovie()) {
                return 0;
            }
            return Orig(this_ptr, file_path);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(TaskMovie_IsReady_Hook) {
        static uint64_t Callback(uintptr_t this_ptr) {
            if (ShouldBlockMovie()) {
                return 1;
            }
            return Orig(this_ptr);
        }
    };

    // =========================================================================
    // SAFE DSC SCRIPT CLEANUP FOR 2D MODE
    // =========================================================================
    void CleanDscPostProcess(PVGamePvData* pv_data) {
        if (!pv_data) return;

        int32_t pos = 1;
        while (pos < 44900) {
            int32_t op = pv_data->script_buffer[pos];
            if (op == 0) break; // END

            const auto* info = dsc::GetOpcodeInfo(op);
            if (!info || info->length < 0) {
                pos++;
                continue;
            }
            int32_t len = info->length;

            // =========================================================================
            // 1. CLEAR FADES, SCREEN DARKENING AND STAGE EFFECTS
            // =========================================================================

            // SCENE_FADE (Opcode 52: mode, r, g, b, alpha, duration) - disable alpha & duration
            if (op == 52 && pos + 6 < 45000) {
                for (int k = 1; k <= 6; k++) pv_data->script_buffer[pos + k] = 0;
            }

            // TONE_TRANS (Opcode 53: r_off, g_off, b_off, r_scale, g_scale, b_scale) - reset offsets and normalize brightness
            else if (op == 53 && pos + 6 < 45000) {
                pv_data->script_buffer[pos + 1] = 0;
                pv_data->script_buffer[pos + 2] = 0;
                pv_data->script_buffer[pos + 3] = 0;
                pv_data->script_buffer[pos + 4] = 1000;
                pv_data->script_buffer[pos + 5] = 1000;
                pv_data->script_buffer[pos + 6] = 1000;
            }

            // SATURATE (Opcode 54) - keep at 1000 to prevent desaturation
            else if (op == 54 && pos + 1 < 45000) {
                pv_data->script_buffer[pos + 1] = 1000;
            }

            // FADE_MODE (Opcode 55) - disable inversion/negative effects
            else if (op == 55 && pos + 1 < 45000) {
                pv_data->script_buffer[pos + 1] = 0;
            }

            // HIDE_FIELD (Opcode 15) - force 0 to ensure field visibility
            else if (op == 15 && pos + 1 < 45000) {
                pv_data->script_buffer[pos + 1] = 0;
            }

            // Field fades: FADEIN_FIELD (10), FADEOUT_FIELD (17), PV_END_FADEOUT (83)
            else if ((op == 10 || op == 17 || op == 83) && pos + 2 < 45000) {
                pv_data->script_buffer[pos + 1] = 0;
                pv_data->script_buffer[pos + 2] = 0;
            }

            // COLOR_COLLE (Opcode 94) - disable distorting LUT color correction filter
            else if (op == 94 && pos + 3 < 45000) {
                pv_data->script_buffer[pos + 1] = -1;
                pv_data->script_buffer[pos + 2] = 0;
                pv_data->script_buffer[pos + 3] = 0;
            }

            // EDIT_EFFECT (Opcode 43) - disable screen darkening effects
            else if (op == 43 && pos + 2 < 45000) {
                pv_data->script_buffer[pos + 1] = -1;
                pv_data->script_buffer[pos + 2] = 0;
            }

            // =========================================================================
            // 2. STRIP 3D OPCODES
            // =========================================================================
            else {
                switch (op) {
                    // Character visibility and parts
                    case 4:  // MIKU_DISP (chara, disp) -> set disp = 0
                    case 5:  // MIKU_SHADOW (chara, shadow) -> set shadow = 0
                        if (pos + 2 < 45000) pv_data->script_buffer[pos + 2] = 0;
                        break;

                    case 57: // PARTS_DISP (chara, part, disp) -> set disp = 0
                        if (pos + 3 < 45000) pv_data->script_buffer[pos + 3] = 0;
                        break;

                    case 96: // CHARA_ALPHA (chara, alpha...) -> set alpha = 0
                        if (pos + 2 < 45000) pv_data->script_buffer[pos + 2] = 0;
                        break;

                    case 37: // SET_CHARA
                    case 59: // CHARA_SIZE
                    case 60: // CHARA_HEIGHT_ADJUST
                    case 62: // CHARA_POS_ADJUST
                    case 72: // CHARA_COLOR
                        // Zero out offset and color parameters without corrupting chara_id
                        for (int k = 2; k <= len && (pos + k < 45000); k++) {
                            pv_data->script_buffer[pos + k] = 0;
                        }
                        break;

                    // Motion and animations
                    case 2:  // MIKU_MOVE
                    case 3:  // MIKU_ROT
                    case 7:  // SET_MOTION
                    case 27: // EDIT_MOTION
                    case 38: // EDIT_MOVE
                    case 74: // EDIT_MOVE_XYZ
                        for (int k = 2; k <= len && (pos + k < 45000); k++) {
                            pv_data->script_buffer[pos + k] = 0;
                        }
                        break;

                    // Facial and hand expressions
                    case 18: // EYE_ANIM
                    case 19: // MOUTH_ANIM
                    case 20: // HAND_ANIM
                    case 21: // LOOK_ANIM
                    case 22: // EXPRESSION
                    case 23: // LOOK_CAMERA
                    case 30: // EDIT_FACE
                    case 36: // EDIT_MOUTH
                    case 40: // EDIT_EYELID
                    case 41: // EDIT_EYE
                    case 45: // EDIT_HAND_ANIM
                    case 48: // EDIT_BLUSH
                    case 56: // AUTO_BLINK
                    case 87: // HAND_SCALE
                    case 89: // FACE_TYPE
                        for (int k = 2; k <= len && (pos + k < 45000); k++) {
                            pv_data->script_buffer[pos + k] = 0;
                        }
                        break;

                    // Camera parameters
                    case 12:  // SET_CAMERA
                    case 13:  // DATA_CAMERA
                    case 31:  // MOVE_CAMERA
                    case 66:  // DATA_CAMERA_START
                    case 81:  // EDIT_CAMERA
                    case 103: // EDIT_CAMERA_BOX
                        for (int k = 1; k <= len && (pos + k < 45000); k++) {
                            pv_data->script_buffer[pos + k] = 0;
                        }
                        break;

                    // Items and visual effects
                    case 9:   // EFFECT
                        if (pos + 1 < 45000) pv_data->script_buffer[pos + 1] = -1;
                        break;
                    case 11:  // EFFECT_OFF
                        if (pos + 1 < 45000) pv_data->script_buffer[pos + 1] = 0;
                        break;
                    case 42:  // EDIT_ITEM
                        if (pos + 1 < 45000) pv_data->script_buffer[pos + 1] = -1;
                        break;
                    case 47:  // HAND_ITEM
                        if (pos + 2 < 45000) pv_data->script_buffer[pos + 2] = -1;
                        break;
                    case 61:  // ITEM_ANIM
                        if (pos + 2 < 45000) pv_data->script_buffer[pos + 2] = -1;
                        break;
                    case 76:  // EDIT_INSTRUMENT_ITEM
                        if (pos + 1 < 45000) pv_data->script_buffer[pos + 1] = -1;
                        break;
                    case 85:  // ITEM_ANIM_ATTACH
                        if (pos + 1 < 45000) pv_data->script_buffer[pos + 1] = -1;
                        break;
                    case 101: // ITEM_ALPHA
                        if (pos + 2 < 45000) pv_data->script_buffer[pos + 2] = 0;
                        break;

                    // Lighting, physics and post-processing
                    case 50:  // CLOTH_WET
                    case 51:  // LIGHT_ROT
                    case 69:  // WIND
                    case 70:  // OSAGE_STEP
                    case 71:  // OSAGE_MV_CCL
                    case 88:  // LIGHT_POS
                    case 92:  // FOG
                    case 93:  // BLOOM
                    case 95:  // DOF
                    case 99:  // TOON
                    case 100: // SHIMMER
                    case 104: // STAGE_LIGHT
                    case 105: // AGEAGE_CTRL
                    case 106: // PSE
                        for (int k = 1; k <= len && (pos + k < 45000); k++) {
                            pv_data->script_buffer[pos + k] = 0;
                        }
                        break;
                }
            }

            pos += 1 + len;
        }
    }

    void ProcessDsc(PVGamePvData* pv_data) {
        if (!pv_data) return;
        is3D = false;
        hasMovie = false;

        int32_t pos = 1;
        while (pos < 44900) {
            int32_t op = pv_data->script_buffer[pos];
            if (op == 0) break;
            const auto* info = dsc::GetOpcodeInfo(op);
            if (!info || info->length < 0) { pos++; continue; }

            if (op == 19 || op == 4) is3D = true;

            if (op == 67 || op == 68 || op == 102) {
                hasMovie = true;
                if (ShouldBlockMovie() && (pos + 1 < 45000)) {
                    pv_data->script_buffer[pos + 1] = 0;
                }
            }
            pos += 1 + info->length;
        }

        if (ShouldApply2D()) {
            CleanDscPostProcess(pv_data);
        }
    }

    // =========================================================================
    // 2D LOADER HOOKS
    // =========================================================================
    HOOK_DEFINE_INLINE(EarlyCapturePvIdHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            int32_t new_pvid = static_cast<int32_t>(ctx->X[8]);
            if (new_pvid > 0) s_currentPvId = new_pvid;
            s_fallbackSpriteId = -1;

            if (ShouldApply2D()) {
                SetDynamicPatches(true);
            } else {
                RestoreOriginalFields();
            }
        }
    };

    HOOK_DEFINE_INLINE(SkipDisp2dInlineHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (ShouldApply2D()) {
                ctx->X[8] = 0;
            }
        }
    };

    HOOK_DEFINE_INLINE(SkipMotionAndChrmotHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (ShouldApply2D()) {
                ctx->X[8] = ctx->X[25];
            }
        }
    };

    HOOK_DEFINE_INLINE(SkipPvExpressionHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (ShouldApply2D()) {
                ctx->X[9] = 0;
            }
        }
    };

    HOOK_DEFINE_INLINE(SkipChreffInlineHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (ShouldApply2D()) {
                ctx->X[10] = ctx->X[21];
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(StageColiLoadHook) {
        static void Callback(void* pv_ptr) {
            if (ShouldApply2D()) {
                return;
            }
            Orig(pv_ptr);
        }
    };

    // =========================================================================
    // STAGES AND 2D BACKGROUND RENDERING
    // =========================================================================
    HOOK_DEFINE_INLINE(PreAllocEmptyFieldsInlineHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (!ShouldApply2D()) return;

            uintptr_t diff_ptr = ctx->X[26];
            if (diff_ptr == 0) return;

            uintptr_t begin = *reinterpret_cast<uintptr_t*>(diff_ptr + 0x190);
            uintptr_t end   = *reinterpret_cast<uintptr_t*>(diff_ptr + 0x198);
            uintptr_t cap   = *reinterpret_cast<uintptr_t*>(diff_ptr + 0x1a0);

            if (s_savedDiffs.find(diff_ptr) == s_savedDiffs.end()) {
                s_savedDiffs[diff_ptr] = { begin, end, cap };
            }

            int32_t pv_id = (s_currentPvId > 0) ? s_currentPvId : game::GetGlobalPvID();
            std::memset(s_dummyFields, 0, sizeof(s_dummyFields));

            char set_buf[32] = {0};
            int len = (pv_id < 1000)
                ? std::snprintf(set_buf, sizeof(set_buf), "SPR_SEL_PV%03d", pv_id)
                : std::snprintf(set_buf, sizeof(set_buf), "SPR_SEL_PV%d", pv_id);

            for (int k = 0; k < 2; k++) {
                uint8_t* f = s_dummyFields + (k * 0x220);
                *reinterpret_cast<int32_t*>(f + 0x4) = -1;
                *reinterpret_cast<int32_t*>(f + 0x8) = -1;
                f[0x1b0] = 1;

                f[0x198] = static_cast<uint8_t>(len << 1);
                std::memcpy(f + 0x199, set_buf, len + 1);
            }

            *reinterpret_cast<uintptr_t*>(diff_ptr + 0x190) = reinterpret_cast<uintptr_t>(s_dummyFields);
            *reinterpret_cast<uintptr_t*>(diff_ptr + 0x198) = reinterpret_cast<uintptr_t>(s_dummyFields + (0x220 * 2));
            *reinterpret_cast<uintptr_t*>(diff_ptr + 0x1a0) = reinterpret_cast<uintptr_t>(s_dummyFields + (0x220 * 2));
        }
    };

    HOOK_DEFINE_INLINE(ForceSprSetBackInlineHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (!ShouldApply2D()) return;

            uint8_t* str_raw = reinterpret_cast<uint8_t*>(ctx->X[25] + 0x198);
            int32_t pv_id = (s_currentPvId > 0) ? s_currentPvId : game::GetGlobalPvID();
            if (pv_id <= 0) return;

            char expected_set[32] = {0};
            int len = (pv_id < 1000)
                ? std::snprintf(expected_set, sizeof(expected_set), "SPR_SEL_PV%03d", pv_id)
                : std::snprintf(expected_set, sizeof(expected_set), "SPR_SEL_PV%d", pv_id);

            const char* current_name = ((str_raw[0] & 1) == 0)
                ? reinterpret_cast<const char*>(str_raw + 1)
                : *reinterpret_cast<const char**>(str_raw + 16);

            if (current_name && current_name[0] != '\0' && std::strcmp(current_name, expected_set) != 0) {
                if (s_savedFieldNames.find(str_raw) == s_savedFieldNames.end()) {
                    s_savedFieldNames[str_raw] = std::string(current_name);
                }
                str_raw[0] = static_cast<uint8_t>(len << 1);
                std::memcpy(str_raw + 1, expected_set, len + 1);
            }
        }
    };

    HOOK_DEFINE_INLINE(GetSpriteIdInlineHook) {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (!ShouldApply2D()) return;

            int32_t* res_ptr = reinterpret_cast<int32_t*>(ctx->X[0]);
            if (res_ptr && *res_ptr != -1) {
                // Native 2D sprite (IMAGE) found in database; no replacement needed
                s_fallbackSpriteId = *res_ptr;
                g_isBgSubstituted = false;
                return;
            }

            int32_t pv_id = (s_currentPvId > 0) ? s_currentPvId : game::GetGlobalPvID();
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            void* spr_db = *reinterpret_cast<void**>(base + OFFSET_SPR_DB_PTR);
            auto GetSpriteIdFunc = reinterpret_cast<int32_t* (*)(void*, stringRange*)>(base + ADDR_GET_SPRITE_ID_FUNC);

            if (!spr_db || !GetSpriteIdFunc) return;

            char candidate[128] = {0};
            std::snprintf(candidate, sizeof(candidate), "SPR_SEL_PV%03d_SONG_BG%03d", pv_id, pv_id);

            stringRange range;
            range.data = candidate;
            range.end  = candidate + std::strlen(candidate);

            int32_t* result = GetSpriteIdFunc(spr_db, &range);
            if (result && *result != -1) {
                // Replaced with fallback SONG_BG sprite
                s_fallbackSpriteId = *result;
                g_isBgSubstituted = true;
                ctx->X[0] = reinterpret_cast<uint64_t>(&s_fallbackSpriteId);
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(ParseDscHook) {
        static bool Callback(PVGamePvData* pv_data, void* file_data) {
            bool res = Orig(pv_data, file_data);
            if (pv_data != nullptr) {
               ProcessDsc(pv_data);
            }
            return res;
        }
    };

    void init() {
        g_force2D = Config::no3D;
        g_noMovie = Config::noMV;

        CacheOriginalBytes();

        // Revert 2D patches upon returning to menu
        FrameUpdateHook::InstallAtOffset(ADDR_GET_INPUT_STATE_FUNC);

        // Movie blocking
        TaskMovie_OpenFile_Hook::InstallAtOffset(ADDR_TASK_MOVIE_OPEN);
        TaskMovie_IsReady_Hook::InstallAtOffset(ADDR_TASK_MOVIE_READY);

        // DSC parser
        ParseDscHook::InstallAtOffset(ADDR_PARSE_DSC_FUNC);

        // 3D suppression hooks
        EarlyCapturePvIdHook::InstallAtOffset(ADDR_INLINE_CAPTURE_PVID);
        SkipDisp2dInlineHook::InstallAtOffset(ADDR_INLINE_DISP2D_CHECK);
        SkipMotionAndChrmotHook::InstallAtOffset(ADDR_INLINE_MOTION_CHECK);
        SkipPvExpressionHook::InstallAtOffset(ADDR_INLINE_PV_EXP_CHECK);
        SkipChreffInlineHook::InstallAtOffset(ADDR_INLINE_CHREFF_CHECK);
        StageColiLoadHook::InstallAtOffset(ADDR_STAGE_COLI_LOAD_FUNC);

        // Stage fields and 2D background
        PreAllocEmptyFieldsInlineHook::InstallAtOffset(ADDR_INLINE_PRE_ALLOC);
        ForceSprSetBackInlineHook::InstallAtOffset(ADDR_INLINE_CHECK_FIELD);
        GetSpriteIdInlineHook::InstallAtOffset(ADDR_INLINE_GET_SPRITE);
    }
}
