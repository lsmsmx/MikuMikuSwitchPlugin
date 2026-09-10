#include "logger.hpp"
#include "lib.hpp"
#include "Config.hpp"
#include "FTRestoration.hpp"

#define FIX(addr) ((addr) - 0x100)

#define ADDR_SSS_BIT_GETTER       FIX(0x0020B950)
#define ADDR_SSS_BIT_SETTER       FIX(0x0020B970)

#define ADDR_NPR_CSTMMENU_KILL    FIX(0x003D4674)
#define ADDR_FXAA_CSTMMENU_KILL   FIX(0x003D4678)

#define ADDR_ADP_GETTER           FIX(0x0020B4E0)
#define ADDR_ADP_SETTER           FIX(0x0020B500)

#define ADDR_ADP_HARDCODE         0x002028A8

#define ADDR_SHADOW_W3            FIX(0x005E5F34)
#define ADDR_SHADOW_H3            FIX(0x005E5F38)
#define ADDR_SHADOW_W4            FIX(0x005E5F58)
#define ADDR_SHADOW_H4            FIX(0x005E5F5C)
#define ADDR_SHADOW_W5            FIX(0x005E5F7C)
#define ADDR_SHADOW_H5            FIX(0x005E5F80)
#define ADDR_SHADOW_W6            FIX(0x005E5FA0)
#define ADDR_SHADOW_H6            FIX(0x005E5FA4)

#define ADDR_FORCE_REFLECT_ZERO  0x0062918C // Address for cset w8, ne
#define ADDR_REFLECT_FUNC_CALL   0x00629194 // Address for bl FUN_004f4350

#define ADDR_SELF_SHADOW_DISABLE  0x00450CF4
#define ADDR_FUN_SHADOW_ENABLE    0x004F4280
#define ADDR_DAT_SHADOW_FLAG_1    0x00B2E95308
#define ADDR_DAT_SHADOW_FLAG_2    0x00B2E94498

#define ADDR_GET_BLUR_QUALITY_FLAG    0x0020CAB0
#define ADDR_BLUR_BUFFER_2_ARG_FIX    0x0020B098

#define ADDR_GET_MAG_FILTER 0x004A0D20

constexpr uint32_t ARM64_NOP   = 0xD503201F;
constexpr uint32_t ARM64_RET   = 0xD65F03C0;
constexpr uint32_t ARM64_W0_1  = 0x52800020;
constexpr uint32_t ARM64_W0_0  = 0x52800000;
constexpr uint32_t ARM64_W1_0  = 0x52800001;

uintptr_t SETTER_ADDRESSES[] = { 0x0020b800, /*0x0020b870*/ 0x0020b8c0, 0x0020b910, 0x0020b960, 0x0020b990, 0x0020c160, 0x0020c430, 0x0020c460, /*0x0020c5b0, 0x0020c600*/ 0x0020c630, 0x0020c660, 0x0020c870, 0x0020c8c0, 0x0020c910, 0x0020c960, 0x0020ca50, 0x0020ca80, 0x0020cad0, /*0x0020cb00*/ 0x0020cb30, 0x0020cb80, 0x0020cc20, 0x0020c7b0, 0x002146d0 };
// 0020c190, 0020c3d0, 0020c690, 0020c740, 0020c7b0, 0020c9f0, 00214560, 002146d0, 0020db20, 0020dbd0, 00211c60

// Dynamic ARM64 instruction helper: MOV Wx, #imm
inline uint32_t Arm64Movz(uint32_t reg, uint16_t val) {
    return 0x52800000 | ((uint32_t)val << 5) | (reg & 0x1F);
}

inline void PatchSettersWithRet() {
    for (uintptr_t addr : SETTER_ADDRESSES) {
        exl::patch::CodePatcher(addr).Write<uint32_t>(ARM64_RET);
    }
}

HOOK_DEFINE_TRAMPOLINE(SSAA_ENABLE) {
    static void Callback(int32_t ssaa, int32_t hd_res, int32_t ss_alpha_mask, bool npr, bool a5) {
        if (Config::ssaaMode == "on") {
            Orig(1, 0, ss_alpha_mask, npr, a5);
        } else {
            Orig(ssaa, hd_res, ss_alpha_mask, npr, a5);
        }
    }
};

// 1. Simplified MAG Filter Hook (FUN_004a0430)
HOOK_DEFINE_TRAMPOLINE(SetMagFilterHook) {
    static void Callback(uintptr_t render_ptr, int32_t filter_id) {
        if (Config::magFilterMode != "default") {
            if (Config::magFilterMode == "nearest")           filter_id = 0;
            else if (Config::magFilterMode == "bilinear")     filter_id = 1;
            else if (Config::magFilterMode == "sharpen_5tap") filter_id = 2;
            else if (Config::magFilterMode == "sharpen_4tap") filter_id = 3;
            else if (Config::magFilterMode == "cone_4tap")    filter_id = 4;
            else if (Config::magFilterMode == "cone_2tap")    filter_id = 5;
        }
        Orig(render_ptr, filter_id);
    }
};

// 2. Gamma Setter (FUN_004a0150)
HOOK_DEFINE_TRAMPOLINE(SetGammaHook) {
    static void Callback(uintptr_t render_ptr, float gamma_val) {
        if (Config::gamma != -1.0f) {
            gamma_val = Config::gamma;
        }
        Orig(render_ptr, gamma_val);
    }
};

// 3. Exposure Setter (0x4a0130)
HOOK_DEFINE_TRAMPOLINE(SetExposureHook) {
    static void Callback(float exposure, uintptr_t render_ptr) {
        float target_exposure = (Config::exposure != -1.0f) ? Config::exposure : exposure;
        Orig(target_exposure, render_ptr);
    }
};

// 4. FXAA Parameters Setter (FUN_004a0550)
HOOK_DEFINE_TRAMPOLINE(SetFxaaParamsHook) {
    static void Callback(uintptr_t render_ptr, float* params_array) {
        float new_params[3] = { params_array[0], params_array[1], params_array[2] };

        if (Config::fxaaQualitySubpix != -1.0f)
            new_params[0] = Config::fxaaQualitySubpix;

        if (Config::fxaaQualityEdgeThreshold != -1.0f)
            new_params[1] = Config::fxaaQualityEdgeThreshold;

        if (Config::fxaaQualityEdgeThresholdMin != -1.0f)
            new_params[2] = Config::fxaaQualityEdgeThresholdMin;

        Orig(render_ptr, new_params);
    }
};

HOOK_DEFINE_TRAMPOLINE(RandomInitHook) {
    static uint64_t Callback() {
        uint64_t result = Orig();

        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
        uintptr_t lvar1 = *(uintptr_t*)(base + 0x00cdf890);

        if (!lvar1) return result;

        uint32_t* adp_low  = (uint32_t*)(lvar1 + 0x65708f90);
        uint32_t* adp_high = (uint32_t*)(lvar1 + 0x65708f90 + 4);

        if (Config::disableAdp) {
            *adp_low = 0; // Disable ADP completely

            // Apply fixed scaler only when ADP is disabled
            if (Config::resScaler != 1.0f) {
                *(float*)(lvar1 + 0x6570907c) = Config::resScaler; // handheld
                *(float*)(lvar1 + 0x65709088) = Config::resScaler; // docked
            }
            if (Config::shadowIntensity != 1.0f) {
                *(float*)(lvar1 + 0x65709090) = Config::shadowIntensity;
            }
        } else {
            *adp_low = 1; // ADP enabled: game handles resolution internally
        }

        // Extra FT Graphics
        if (Config::extraFtGraphics) {
            *adp_high = 0x00B0811A;
        }

        // Disable reflections
        if (Config::disableReflections) {
            uintptr_t* ptr_to_flag = reinterpret_cast<uintptr_t*>(base + 0x00CE10D8);
            if (ptr_to_flag != nullptr && *ptr_to_flag != 0) {
                *reinterpret_cast<uint8_t*>(*ptr_to_flag) = 0x00;
            }
        }


        if (Config::force30fps) {
            *adp_high |= 0x800;
            *adp_high |= 0x1000;
        }

        return result;
    }
};

HOOK_DEFINE_TRAMPOLINE(GetReflectionQuality) {
    static float Callback() {
        if (Config::disableAdp) {
            uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;
            uintptr_t lvar1 = *(uintptr_t*)(base + 0x00cdf890);
            *(float*)(lvar1 + 0x65709098) = Config::reflectionQuality;
            return Config::reflectionQuality;
        }
        else {
            return Orig();
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(GetShadowOpacIntens) {
    static float Callback() {
        return Config::shadowIntensity;
    }
};

inline bool IsSafeFloat(float f) {
    uint32_t u = *(uint32_t*)&f;
    return ((u & 0x7F800000) != 0x7F800000);
}

static float Dummy_GetDeltaFrame(void* _this) {
    return 1.0f;
}
static uintptr_t s_DummyVtable[4] = { 0, 0, (uintptr_t)Dummy_GetDeltaFrame, 0 };
static uintptr_t s_DummyObject = (uintptr_t)s_DummyVtable;

// =========================================================
// 1. Initialization Hook (Cleans old leaves on restart)
// =========================================================
HOOK_DEFINE_TRAMPOLINE(Leaf_Init_Hook) {
    static void Callback(uint64_t param_1) {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        Orig(param_1);

        // Clear leaf buffer on retry
        if ((param_1 & 1) != 0) {
            uintptr_t ptcl_buf   = *(uintptr_t*)(base + 0xb2dfea70);
            int32_t*  ptcl_count = (int32_t*)(base + 0xb2dfea5c);
            if (ptcl_buf != 0 && ptcl_count != 0) {
                memset((void*)ptcl_buf, 0, 0x30000);
                *ptcl_count = 0;
            }
        }
    }
};

// =========================================================
// 2. Physics Hook (With proper scene timing)
// =========================================================
HOOK_DEFINE_TRAMPOLINE(Leaf_Physics_Fix) {
    static void Callback(uintptr_t param_1) {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        *(uintptr_t*)(param_1 + 0x68) = (uintptr_t)&s_DummyObject;

        Orig(param_1);

        // Sanity check (protection against NaN values)
        uintptr_t db_ptr   = *(uintptr_t*)(base + 0xb2dfea68);
        uintptr_t ptcl_buf = *(uintptr_t*)(base + 0xb2dfea70);
        int32_t   count_5c = *(int32_t*)(base + 0xb2dfea5c);

        if (db_ptr != 0 && ptcl_buf != 0 && count_5c > 0) {
            for (int i = 0; i < count_5c; i++) {
                float* ptcl = (float*)(ptcl_buf + (i * 0x60));
                if (!IsSafeFloat(ptcl[0]) || !IsSafeFloat(ptcl[1]) || !IsSafeFloat(ptcl[2])) {
                    *(int32_t*)&ptcl[21] = 0;
                    ptcl[0] = 0.0f;
                    ptcl[1] = -1000.0f;
                    ptcl[2] = 0.0f;
                }
            }
        }
    }
};

// =========================================================
// 3. Render Hook
// =========================================================
HOOK_DEFINE_TRAMPOLINE(Leaf_Render_Fix) {
    static void Callback(uintptr_t param_1, uintptr_t param_2) {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        uintptr_t ptcl_buf = *(uintptr_t*)(base + 0xb2dfea70);
        int32_t   count_5c = *(int32_t*)(base + 0xb2dfea5c);

        // Guard against division by zero during draw
        if (ptcl_buf != 0 && count_5c > 0) {
            for (int i = 0; i < count_5c; i++) {
                float* ptcl = (float*)(ptcl_buf + (i * 0x60));

                if (ptcl[3] <= 0.0001f) {
                    ptcl[3] = 1.0f;
                }

                if (!IsSafeFloat(ptcl[8]) || !IsSafeFloat(ptcl[9]) || !IsSafeFloat(ptcl[10])) {
                    ptcl[8]  = 0.0f;
                    ptcl[9]  = 0.0f;
                    ptcl[10] = 1.0f;
                }
            }
        }

        Orig(param_1, param_2);
    }
};

// =========================================================================
// 1. Main MLAA Entry Fix (Restores PC UV formula and scaling)
// =========================================================================
HOOK_DEFINE_TRAMPOLINE(PostProcess_MLAA_Fix) {
    static void Callback(uintptr_t param_1, uintptr_t* param_2, int32_t param_3, int32_t param_4, uint64_t param_5) {
        // If MLAA is disabled, perform standard copy
        if ((param_5 & 1) == 0) {
            Orig(param_1, param_2, param_3, param_4, param_5);
            return;
        }

        // Restore original PC formula
        float render_scale = *(float*)(*(uintptr_t*)(param_1 + 0x1a40) + 0x658);
        int32_t target_w   = *(int32_t*)(param_1 + 0x10f8);
        int32_t target_h   = *(int32_t*)(param_1 + 0x110c);

        int32_t render_w   = (int32_t)(render_scale * (float)target_w);
        int32_t render_h   = (int32_t)(render_scale * (float)target_h);

        float tex_scale_x  = *(float*)(param_1 + 0x1148);
        float tex_scale_y  = *(float*)(param_1 + 0x114c);

        // Correct aspect division from PC port
        float corrected_uv_x = ((float)render_w / (float)target_w) * tex_scale_x;
        float corrected_uv_y = ((float)render_h / (float)target_h) * tex_scale_y;

        // Temporarily set PC scaling parameters
        *(float*)(param_1 + 0x1148) = corrected_uv_x;
        *(float*)(param_1 + 0x114c) = corrected_uv_y;

        // Call original pipeline with corrected coordinates
        Orig(param_1, param_2, param_3, param_4, param_5);

        // Restore original values
        *(float*)(param_1 + 0x1148) = tex_scale_x;
        *(float*)(param_1 + 0x114c) = tex_scale_y;
    }
};

// =========================================================================
// 2. MLAA Pass 2 Fix (Fixes black artifact needles on arrows / FUN_004a8570)
// =========================================================================
HOOK_DEFINE_TRAMPOLINE(MLAA_Pass2_Fix) {
    static void Callback(float param_1, float param_2, uintptr_t param_3, uintptr_t* param_4,
                         uint32_t param_5, uint32_t param_6, int32_t param_7, int32_t param_8, uintptr_t* param_9) {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        auto bind_tex     = (void(*)(uintptr_t*, int, int, uintptr_t))(base + 0x0020a0d0);
        auto bind_sampler = (void(*)(uintptr_t*, int, int, uintptr_t))(base + 0x0020a150);
        auto bind_ubo0    = (void(*)(uintptr_t*, void*))(base + 0x00209e70);
        auto bind_ubo1    = (void(*)(uintptr_t*, void*))(base + 0x0020a030);
        auto get_tex_w    = (int(*)(uintptr_t*))(base + 0x00206000);
        auto get_tex_h    = (int(*)(uintptr_t*))(base + 0x00206010);
        auto draw_quad    = (void(*)(float, float, float, float, float, float, uintptr_t, uintptr_t*, int, int, void*))(base + 0x004a4830);

        bind_tex(param_4, 0, 1, (uintptr_t)param_9);
        bind_sampler(param_4, 0, 1, param_3 + 0x20); // Sampler 0

        bind_tex(param_4, 1, 1, param_3 + 0x620);    // mlaa_area_tex
        // Use Point Sampler 3 (+0x38 / Nearest) instead of (+0x28)
        bind_sampler(param_4, 1, 1, param_3 + 0x38);

        bind_ubo0(param_4, (void*)(param_3 + 0x418));
        bind_ubo1(param_4, (void*)(param_3 + 0x420));

        int w = get_tex_w(param_9);
        int h = get_tex_h(param_9);
        uint64_t data[2] = { 0x3f8000003f800000, 0x3f8000003f800000 };
        draw_quad(0.0f, param_1, param_2, 0.0f, 0.0f, 1.0f, param_3, param_4, w, h, data);
    }
};

// =========================================================================
// 3. MLAA Pass 3 Fix (Correct color blending / FUN_004a8680)
// =========================================================================
HOOK_DEFINE_TRAMPOLINE(MLAA_Pass3_Fix) {
    static void Callback(float param_1, float param_2, uintptr_t param_3, uintptr_t* param_4,
                         uint32_t param_5, uint32_t param_6, int32_t param_7, int32_t param_8,
                         uintptr_t* param_9, uintptr_t param_10, uint8_t param_11) {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        auto bind_tex     = (void(*)(uintptr_t*, int, int, uintptr_t))(base + 0x0020a0d0);
        auto bind_sampler = (void(*)(uintptr_t*, int, int, uintptr_t))(base + 0x0020a150);
        auto bind_ubo0    = (void(*)(uintptr_t*, void*))(base + 0x00209e70);
        auto bind_ubo1    = (void(*)(uintptr_t*, void*))(base + 0x0020a030);
        auto get_tex_w    = (int(*)(uintptr_t*))(base + 0x00206000);
        auto get_tex_h    = (int(*)(uintptr_t*))(base + 0x00206010);
        auto draw_quad    = (void(*)(float, float, float, float, float, float, uintptr_t, uintptr_t*, int, int, void*))(base + 0x004a4830);

        bind_tex(param_4, 0, 1, (uintptr_t)param_9);
        bind_sampler(param_4, 0, 1, param_3 + 0x28); // Sampler 1 (+0x28)

        bind_tex(param_4, 1, 1, param_10);
        bind_sampler(param_4, 1, 1, param_3 + 0x28); // Sampler 1 (+0x28)

        uintptr_t ubo_offset = ((param_11 & 1) == 0) ? 0x2c0 : 0x2d0;
        bind_ubo0(param_4, (void*)(param_3 + ubo_offset + 0x168));
        bind_ubo1(param_4, (void*)(param_3 + ubo_offset + 0x170));

        int w = get_tex_w(param_9);
        int h = get_tex_h(param_9);
        uint64_t data[2] = { 0x3f8000003f800000, 0x3f8000003f800000 };
        draw_quad(0.0f, param_1, param_2, 0.0f, 0.0f, 1.0f, param_3, param_4, w, h, data);
    }
};

// =========================================================================
// Future Tone Customization Menu Lighting
// =========================================================================

inline void* light_set_get_by_id(int32_t id) {
    auto func = (void*(*)(int32_t))(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x4082b0);
    return func(id);
}

inline void light_set_type(void* light_set, int32_t type) {
    auto func = (void(*)(void*, int32_t))(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x4082d0);
    func(light_set, type);
}

// NOTE: In ARM64, float arguments (s0-s3) and pointer arguments (x0) use separate registers.
// Ghidra decompiles this with floats coming first, so we match that order exactly.
inline void light_set_ambient(float r, float g, float b, float a, void* light_set) {
    auto func = (void(*)(float, float, float, float, void*))(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x4082f0);
    func(r, g, b, a, light_set);
}

inline void light_set_diffuse(float r, float g, float b, float a, void* light_set) {
    auto func = (void(*)(float, float, float, float, void*))(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x408310);
    func(r, g, b, a, light_set);
}

inline void light_set_specular(float r, float g, float b, float a, void* light_set) {
    auto func = (void(*)(float, float, float, float, void*))(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x408330);
    func(r, g, b, a, light_set);
}

inline void light_set_position(float x, float y, float z, void* light_set) {
    auto func = (void(*)(float, float, float, void*))(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x408350);
    func(x, y, z, light_set);
}

inline void* render_get() {
    auto func = (void*(*)())(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x49bbc0);
    return func();
}

// Float first, pointer second
inline void render_set_exposure(float exposure, void* render) {
    auto func = (void(*)(float, void*))(exl::util::GetMainModuleInfo().m_Total.m_Start + 0x4a0130);
    func(exposure, render);
}

// Hook for the actual UI lighting setup function you found
HOOK_DEFINE_TRAMPOLINE(CustomizeSetLightingInfoHook) {
    static void Callback() {
        Orig();

        if (Config::cstmMenuFtStyle) {
            void* set  = light_set_get_by_id(0); // LIGHT_SET_MAIN
            void* rend = render_get();

            if (set) {
                // Overwrite with Arcade / Future Tone parameters
                light_set_type(set, 1); // LIGHT_PARALLEL
                light_set_position(-0.2f, 0.39272901f, 0.70158201f, set);

                light_set_ambient(0.07f, 0.07f, 0.07f, 1.0f, set);
                light_set_diffuse(0.65f, 0.65f, 0.65f, 1.0f, set);
                light_set_specular(0.8f, 0.8f, 0.8f, 0.8f, set);
            }

            if (rend) {
                render_set_exposure(2.5f, rend); // Increase exposure for FT look
            }
        }
    }
};


void FTRestoration::init() {
    RandomInitHook::InstallAtOffset(0x1f4150);

    // Extra FT Graphics Restoration (adp getters)
    if (Config::extraFtGraphics) {
        // 9th bit
        exl::patch::CodePatcher(0x20b7e0).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(0x20b7e4).Write<uint32_t>(ARM64_RET);
        // 18th bit
        exl::patch::CodePatcher(0x20b940).Write<uint32_t>(ARM64_W0_1);
        exl::patch::CodePatcher(0x20b944).Write<uint32_t>(ARM64_RET);

        // Tonemap bit 6
        exl::patch::CodePatcher(0x20c140).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(0x20c144).Write<uint32_t>(ARM64_RET);

        // Force getter to return 0x1
        exl::patch::CodePatcher(0x20c9f0).Write<uint32_t>(ARM64_W0_1);
        exl::patch::CodePatcher(0x20c9f4).Write<uint32_t>(ARM64_RET);

        // Future Tone Blur Texture Buffers
        exl::patch::CodePatcher(ADDR_GET_BLUR_QUALITY_FLAG).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(ADDR_GET_BLUR_QUALITY_FLAG + 4).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(ADDR_BLUR_BUFFER_2_ARG_FIX).Write<uint32_t>(ARM64_W1_0);

        exl::patch::CodePatcher(0x20b020).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(0x20b040).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(0x20b060).Write<uint32_t>(ARM64_RET);

        //PatchSettersWithRet();
    }

    // 2. Anti-Aliasing (MLAA / FXAA / Off)
    if (Config::antiAliasing == "mlaa") {
        exl::patch::CodePatcher(0x20c590).Write<uint32_t>(ARM64_W0_1);
        exl::patch::CodePatcher(0x20c594).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(0x20c5e0).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(0x20c5e4).Write<uint32_t>(ARM64_RET);
    } else if (Config::antiAliasing == "fxaa") {
        exl::patch::CodePatcher(0x20c590).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(0x20c594).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(0x20c5e0).Write<uint32_t>(ARM64_W0_1);
        exl::patch::CodePatcher(0x20c5e4).Write<uint32_t>(ARM64_RET);
    } else if (Config::antiAliasing == "off") {
        exl::patch::CodePatcher(0x20c590).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(0x20c594).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(0x20c5e0).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(0x20c5e4).Write<uint32_t>(ARM64_RET);
    }

    exl::patch::CodePatcher(0x20c5b0).Write<uint32_t>(ARM64_RET);
    exl::patch::CodePatcher(0x20c600).Write<uint32_t>(ARM64_RET);

    // 3. Subsurface Scattering (SSS)
    if (Config::enableSss) {
        exl::patch::CodePatcher(ADDR_SSS_BIT_GETTER).Write<uint32_t>(ARM64_W0_1);
        exl::patch::CodePatcher(ADDR_SSS_BIT_GETTER + 4).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(ADDR_SSS_BIT_SETTER).Write<uint32_t>(ARM64_RET);
    }

    // 4. Customization Menu AFT/FT Style Graphics
    if (Config::cstmMenuFtStyle) {
        exl::patch::CodePatcher(ADDR_NPR_CSTMMENU_KILL).Write<uint32_t>(0xF901BE7F);
        exl::patch::CodePatcher(ADDR_FXAA_CSTMMENU_KILL).Write<uint32_t>(0xF901C27F);
    }
    CustomizeSetLightingInfoHook::InstallAtOffset(0x14deb0);

    // 5. Adaptive Performance (ADP) Removal
    if (Config::disableAdp) {
        exl::patch::CodePatcher(ADDR_ADP_GETTER).Write<uint32_t>(ARM64_W0_0);
        exl::patch::CodePatcher(ADDR_ADP_GETTER + 4).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(ADDR_ADP_SETTER).Write<uint32_t>(ARM64_RET);

        if (Config::resScaler != 1.0f) {
            // handheld
            exl::patch::CodePatcher(0x20c2b0).Write<uint32_t>(ARM64_RET);
            exl::patch::CodePatcher(0x20b624).Write<uint32_t>(ARM64_NOP);
            // docked
            exl::patch::CodePatcher(0x20c310).Write<uint32_t>(ARM64_RET);
            exl::patch::CodePatcher(0x20b630).Write<uint32_t>(ARM64_NOP);
        }

        if (Config::reflectionQuality != 1.0f) {
            GetReflectionQuality::InstallAtOffset(0x20c3b0);
        }

        // 30 FPS Lock
        if (Config::force30fps) {
            // handheld
            exl::patch::CodePatcher(0x20b2e8).Write<uint32_t>(0x5280002D);
            exl::patch::CodePatcher(0x20c1f4).Write<uint32_t>(0x5280002C);
            exl::patch::CodePatcher(0x20c6f8).Write<uint32_t>(0x52800029);
            exl::patch::CodePatcher(0x20c778).Write<uint32_t>(0x5280002C);
            exl::patch::CodePatcher(0x212d28).Write<uint32_t>(0x52800028);
            exl::patch::CodePatcher(0x212f18).Write<uint32_t>(0x52800028);
            exl::patch::CodePatcher(0x213c04).Write<uint32_t>(0x5280002C);
            exl::patch::CodePatcher(0x2145d8).Write<uint32_t>(0x5280002C);
            exl::patch::CodePatcher(0x214690).Write<uint32_t>(0x52800029);
            exl::patch::CodePatcher(0x21471c).Write<uint32_t>(0x5280002C);
            exl::patch::CodePatcher(0x2148d0).Write<uint32_t>(0x5280002C);
            // docked
            exl::patch::CodePatcher(0x20B2E4).Write<uint32_t>(0x5280002C);
            exl::patch::CodePatcher(0x20C1F0).Write<uint32_t>(0x5280002B);
            exl::patch::CodePatcher(0x20C6F4).Write<uint32_t>(0x5280002B);
            exl::patch::CodePatcher(0x20C774).Write<uint32_t>(0x5280002B);
            exl::patch::CodePatcher(0x212D24).Write<uint32_t>(0x5280002A);
            exl::patch::CodePatcher(0x212F14).Write<uint32_t>(0x5280002A);
            exl::patch::CodePatcher(0x213C00).Write<uint32_t>(0x5280002B);
            exl::patch::CodePatcher(0x2145D4).Write<uint32_t>(0x5280002B);
            exl::patch::CodePatcher(0x21468C).Write<uint32_t>(0x5280002B);
            exl::patch::CodePatcher(0x214714).Write<uint32_t>(0x5280002B);
            exl::patch::CodePatcher(0x2148CC).Write<uint32_t>(0x5280002B);

            exl::patch::CodePatcher(0x20c854).Write<uint32_t>(0x1F2003D5); // NOP
            exl::patch::CodePatcher(0x20c8a0).Write<uint32_t>(0x1F2003D5); // NOP
            exl::patch::CodePatcher(0x20c8ec).Write<uint32_t>(0x1F2003D5); // NOP
        }
    }

    // 6. Shadow Intensity / Opacity Multiplier (0.0 to 1.4)
    if (Config::shadowIntensity != 1.0f) {
        GetShadowOpacIntens::InstallAtOffset(0x20c330);
        exl::patch::CodePatcher(0x20c350).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(0x20b648).Write<uint32_t>(ARM64_NOP);
    }

    // 7. Shadow Map Resolution Handling
    if (Config::advancedGraphics) {
        exl::patch::CodePatcher(0x5e5dcc).Write<uint32_t>(Arm64Movz(1, Config::shadowMap1W));
        exl::patch::CodePatcher(0x5e5dd0).Write<uint32_t>(Arm64Movz(2, Config::shadowMap1H));

        exl::patch::CodePatcher(0x394940).Write<uint32_t>(Arm64Movz(3, Config::shadowViewportW));
        exl::patch::CodePatcher(0x394944).Write<uint32_t>(Arm64Movz(4, Config::shadowViewportH));

        exl::patch::CodePatcher(0x395220).Write<uint32_t>(Arm64Movz(19, Config::shadowTex1W));
        exl::patch::CodePatcher(0x395224).Write<uint32_t>(Arm64Movz(20, Config::shadowTex1H));

        exl::patch::CodePatcher(0x395270).Write<uint32_t>(Arm64Movz(1, Config::shadowTex2W));
        exl::patch::CodePatcher(0x395274).Write<uint32_t>(Arm64Movz(2, Config::shadowTex2H));

        exl::patch::CodePatcher(ADDR_SHADOW_W3).Write<uint32_t>(Arm64Movz(1, Config::shadowMap3W));
        exl::patch::CodePatcher(ADDR_SHADOW_H3).Write<uint32_t>(Arm64Movz(2, Config::shadowMap3H));

        exl::patch::CodePatcher(ADDR_SHADOW_W4).Write<uint32_t>(Arm64Movz(1, Config::shadowMap4W));
        exl::patch::CodePatcher(ADDR_SHADOW_H4).Write<uint32_t>(Arm64Movz(2, Config::shadowMap4H));

        exl::patch::CodePatcher(ADDR_SHADOW_W5).Write<uint32_t>(Arm64Movz(1, Config::shadowMap5W));
        exl::patch::CodePatcher(ADDR_SHADOW_H5).Write<uint32_t>(Arm64Movz(2, Config::shadowMap5H));

        exl::patch::CodePatcher(ADDR_SHADOW_W6).Write<uint32_t>(Arm64Movz(1, Config::shadowMap6W));
        exl::patch::CodePatcher(ADDR_SHADOW_H6).Write<uint32_t>(Arm64Movz(2, Config::shadowMap6H));
    } else if (Config::ftShadows) {
        exl::patch::CodePatcher(0x5e5dcc).Write<uint32_t>(0x52810001); // W1 = 2048
        exl::patch::CodePatcher(0x5e5dd0).Write<uint32_t>(0x52810002); // W2 = 2048

        exl::patch::CodePatcher(0x394940).Write<uint32_t>(0x52810003); // W3 = 2048
        exl::patch::CodePatcher(0x394944).Write<uint32_t>(0x52804004); // W4 = 512

        exl::patch::CodePatcher(0x395220).Write<uint32_t>(0x52810013); // W19 = 2048
        exl::patch::CodePatcher(0x395224).Write<uint32_t>(0x52804014); // W20 = 512

        exl::patch::CodePatcher(0x395270).Write<uint32_t>(0x5280a001); // W1 = 1280
        exl::patch::CodePatcher(0x395274).Write<uint32_t>(0x52805a02); // W2 = 720

        exl::patch::CodePatcher(ADDR_SHADOW_W3).Write<uint32_t>(0x52810001); // W1 = 2048
        exl::patch::CodePatcher(ADDR_SHADOW_H3).Write<uint32_t>(0x52810002); // W2 = 2048

        exl::patch::CodePatcher(ADDR_SHADOW_W4).Write<uint32_t>(0x52810001); // W1 = 2048
        exl::patch::CodePatcher(ADDR_SHADOW_H4).Write<uint32_t>(0x52810002); // W2 = 2048

        exl::patch::CodePatcher(ADDR_SHADOW_W5).Write<uint32_t>(0x52804001); // W1 = 512
        exl::patch::CodePatcher(ADDR_SHADOW_H5).Write<uint32_t>(0x52804002); // W2 = 512

        exl::patch::CodePatcher(ADDR_SHADOW_W6).Write<uint32_t>(0x52804001); // W1 = 512
        exl::patch::CodePatcher(ADDR_SHADOW_H6).Write<uint32_t>(0x52804002); // W2 = 512
    }

    // 8. Refract Resolution Handling (2:1 Ratio expected)
    if (Config::advancedGraphics) {
        exl::patch::CodePatcher(0x00B28CA8).Write<uint32_t>(Config::refractW);
        exl::patch::CodePatcher(0x00B28CAC).Write<uint32_t>(Config::refractH);
    } else if (Config::ftRefract) {
        exl::patch::CodePatcher(0x00B28CA8).Write<uint32_t>(1024);
        exl::patch::CodePatcher(0x00B28CAC).Write<uint32_t>(512);
    }

    // 9. Reflect Resolution Handling (2:1 Ratio expected)
    if (Config::advancedGraphics) {
        exl::patch::CodePatcher(0x00B28CBC).Write<uint32_t>(Config::reflectW);
        exl::patch::CodePatcher(0x00B28CC0).Write<uint32_t>(Config::reflectH);
    } else if (Config::ftReflect) {
        exl::patch::CodePatcher(0x00B28CBC).Write<uint32_t>(1024);
        exl::patch::CodePatcher(0x00B28CC0).Write<uint32_t>(512);
    }

    // 10. Install core rendering hooks
    SSAA_ENABLE::InstallAtOffset(0x4f1470);
    SetMagFilterHook::InstallAtOffset(0x4a0430);
    SetGammaHook::InstallAtOffset(0x4a0150);
    SetExposureHook::InstallAtOffset(0x4a0130);
    SetFxaaParamsHook::InstallAtOffset(0x4a0550);

    // MAG Filter patches
    if (Config::magFilterMode != "default") {
        uint32_t filter_id = 1; // Default: Bilinear

        if (Config::magFilterMode == "nearest")           filter_id = 0;
        else if (Config::magFilterMode == "bilinear")     filter_id = 1;
        else if (Config::magFilterMode == "sharpen_5tap") filter_id = 2;
        else if (Config::magFilterMode == "sharpen_4tap") filter_id = 3;
        else if (Config::magFilterMode == "cone_4tap")    filter_id = 4;
        else if (Config::magFilterMode == "cone_2tap")    filter_id = 5;

        // Generate ARM64 instruction: MOV W8, #filter_id
        uint32_t mov_w8_inst = 0x52800008 | (filter_id << 5);

        // Patch 1st read (Nearest vs Bilinear check)
        exl::patch::CodePatcher(0x4ac774).Write<uint32_t>(mov_w8_inst);

        // Patch 2nd read (Sharpen/Cone switch block)
        exl::patch::CodePatcher(0x4ac794).Write<uint32_t>(mov_w8_inst);

        // Create render buffer
        exl::patch::CodePatcher(0x4a1134).Write<uint32_t>(mov_w8_inst);
        exl::patch::CodePatcher(0x4a1138).Write<uint32_t>(ARM64_NOP);
    }

    if (Config::disableReflections) {
        // 1. Force flag to 0 (mov w8, wzr)
        exl::patch::CodePatcher(ADDR_FORCE_REFLECT_ZERO).Write<uint32_t>(0x2A1F03E8);

        // 2. NOP reflection render call
        exl::patch::CodePatcher(ADDR_REFLECT_FUNC_CALL).Write<uint32_t>(ARM64_NOP);
    }

    // Force Disable Self Shadow
    if (Config::disableSelfShadow) {
        exl::patch::CodePatcher(ADDR_SELF_SHADOW_DISABLE).Write<uint32_t>(0x3902013F);
    }

    // Force Disable Shadows
    if (Config::disableShadows) {
        exl::patch::CodePatcher(ADDR_FUN_SHADOW_ENABLE + 4).Write<uint32_t>(0x52800009);
        exl::patch::CodePatcher(ADDR_DAT_SHADOW_FLAG_1).Write<uint8_t>(0x00);
        exl::patch::CodePatcher(ADDR_DAT_SHADOW_FLAG_2).Write<uint8_t>(0x00);
    }

    // Force Disable DOF
    if (Config::disableDOF) {
        exl::patch::CodePatcher(0xb2e8b968).Write<uint8_t>(0x01);
        exl::patch::CodePatcher(0x49ba90).Write<uint32_t>(ARM64_RET);
        exl::patch::CodePatcher(0xb2e8b8f4).Write<uint8_t>(0x00);
        exl::patch::CodePatcher(0x49ba30).Write<uint32_t>(ARM64_RET);
    }

    // Leaf Effect Fix
    Leaf_Init_Hook::InstallAtOffset(0x3919e0);
    Leaf_Render_Fix::InstallAtOffset(0x00390fc0);
    Leaf_Physics_Fix::InstallAtOffset(0x00391d50);

    // MLAA Fix
    MLAA_Pass2_Fix::InstallAtOffset(0x004a8570);
    MLAA_Pass3_Fix::InstallAtOffset(0x004a8680);
}
