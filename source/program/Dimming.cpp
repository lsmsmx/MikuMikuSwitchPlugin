#include "Dimming.hpp"
#include "Config.hpp"
#include "lib.hpp"
#include "nc/diva_nc.hpp"
#include <map>

namespace sound_effects {
    void UpdateSELoop();
}

namespace Dimming {

    struct SprRect {
        float x;
        float y;
        float width;
        float height;
    };

    static constexpr uintptr_t ADDR_DRAW_RECT          = 0x00612640;
    static constexpr uintptr_t ADDR_TASK_PV_GAME_INIT  = 0x0064c910;
    static constexpr uintptr_t ADDR_TASK_PV_GAME_DISP  = 0x0064c9b0;
    static constexpr uintptr_t ADDR_CURRENT_PV_ID      = 0x00be3888bc;

    using DrawRect_t = void* (*)(SprRect* rect, int32_t res, int32_t layer, uint32_t color, int32_t prio);
    static DrawRect_t s_DrawRect = nullptr;
    static const int32_t* s_CurrentPvID = nullptr;

    static std::map<int32_t, int32_t> s_pvSettings;

    static inline int32_t Clamp(int32_t v, int32_t min, int32_t max) {
        if (v > max) return max;
        if (v < min) return min;
        return v;
    }

    static inline int32_t GetLivePvId() {
        if (s_CurrentPvID && *s_CurrentPvID > 0) {
            return *s_CurrentPvID;
        }
        return game::GetGlobalPvID();
    }

    void SetPvDim(int32_t pv_id, int32_t dim_percent) {
        s_pvSettings[pv_id] = dim_percent;
    }

    // Apply saved dim percentage when song initializes
    HOOK_DEFINE_TRAMPOLINE(TaskPvGameInit_Hook) {
        static int64_t Callback(uintptr_t a1) {
            int64_t ret = Orig(a1);

            // If per-song dim setting exists, apply it
            int32_t pv_id = GetLivePvId();
            if (auto it = s_pvSettings.find(pv_id); it != s_pvSettings.end()) {
                Config::dim = it->second;
            }

            return ret;
        }
    };

    // Per-frame rendering
    HOOK_DEFINE_TRAMPOLINE(TaskPvGameDisp_Hook) {
        static uint64_t Callback(uintptr_t a1) {
            // Called every frame during gameplay
            sound_effects::UpdateSELoop();

            // Read Config::dim directly each frame
            int32_t currentDim = (Config::dim > 0) ? Config::dim : 0;

            if (currentDim > 0 && currentDim <= 100) {
                SprRect rect = { 0.0f, 0.0f, 1920.0f, 1080.0f };

                uint32_t alpha = Clamp(static_cast<int32_t>((static_cast<float>(currentDim) / 100.0f) * 255.0f), 0, 255);
                uint32_t color = (alpha & 0xFF) << 24;

                if (s_DrawRect) {
                    s_DrawRect(&rect, 14, 2, color, 0);
                }
            }

            return Orig(a1);
        }
    };

    void init() {
        uintptr_t base = exl::util::GetMainModuleInfo().m_Total.m_Start;

        s_DrawRect    = reinterpret_cast<DrawRect_t>(base + ADDR_DRAW_RECT);
        s_CurrentPvID = reinterpret_cast<const int32_t*>(base + ADDR_CURRENT_PV_ID);

        TaskPvGameInit_Hook::InstallAtOffset(ADDR_TASK_PV_GAME_INIT);
        TaskPvGameDisp_Hook::InstallAtOffset(ADDR_TASK_PV_GAME_DISP);
    }

} // namespace Dimming
