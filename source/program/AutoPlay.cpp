#include "AutoPlay.hpp"
#include "lib.hpp"
#include "nc/diva_nc.hpp"
#include "nc/nc_state.hpp"
#include "nc/input.hpp"
#include "nc/game/target.hpp"
#include "Sound.hpp"
#include <cmath>

namespace AutoPlay {

bool g_enabled = false;

static int32_t s_auto_down[8]   = { 0 };
static int32_t s_auto_tapped[8] = { 0 };

// Dual slider fix: track both indices during simultaneous slide flick
static int32_t s_last_hit_target        = -1;
static int32_t s_last_hit_target_second = -1;

HOOK_DEFINE_TRAMPOLINE(GetPvKeyStateDownHook) {
    static int32_t Callback(void* handler, int32_t key) {
        int32_t orig = Orig(handler, key);
        if (g_enabled && key >= 0 && key < 8) {
            orig |= s_auto_down[key];
        }
        return orig;
    }
};

HOOK_DEFINE_TRAMPOLINE(GetPvKeyStateTappedHook) {
    static int32_t Callback(void* handler, int32_t key) {
        int32_t orig = Orig(handler, key);
        if (g_enabled && key >= 0 && key < 8) {
            orig |= s_auto_tapped[key];
        }
        return orig;
    }
};

static void TapButton(int32_t button_index, int32_t button_mask) {
    s_auto_tapped[button_index] |= button_mask;
    s_auto_down[button_index]   |= button_mask;
}

void CheckMenuToggle() {
}

void Update(PVGameArcade* game) {
    if (!g_enabled || !game || !game->target)
        return;

    for (int i = 0; i < 8; i++) {
        s_auto_tapped[i] = 0;
        s_auto_down[i]   = 0;
    }

    // 1. Sustain active hold notes
    for (TargetStateEx* tgt : state.target_references) {
        if (tgt && tgt->IsLongNoteStart() && tgt->holding) {
            switch (tgt->target_type) {
                case TargetType_TriangleLong: s_auto_down[2] |= 16; break;
                case TargetType_CircleLong:   s_auto_down[0] |= 32; break;
                case TargetType_CrossLong:    s_auto_down[1] |= 64; break;
                case TargetType_SquareLong:   s_auto_down[3] |= 128; break;
            }
        }
    }

    // 2. Rapid input spam for Rush notes
    static int32_t s_rush_timer = 0;
    static bool    s_rush_alt   = false;
    s_rush_timer++;

    for (TargetStateEx* tgt : state.target_references) {
        if (tgt && tgt->IsRushNote() && tgt->holding && tgt->length_remaining > 0.0f) {
            if (s_rush_timer >= 4) {
                s_rush_timer = 0;
                s_rush_alt = !s_rush_alt;

                switch (tgt->target_type) {
                    case TargetType_TriangleRush: TapButton(2, s_rush_alt ? 16 : 1); break;
                    case TargetType_CircleRush:   TapButton(0, s_rush_alt ? 32 : 2); break;
                    case TargetType_CrossRush:    TapButton(1, s_rush_alt ? 64 : 4); break;
                    case TargetType_SquareRush:   TapButton(3, s_rush_alt ? 128 : 8); break;
                }
            }
        }
    }

    // 3. Find closest upcoming target
    PvGameTarget* first_target = nullptr;
    for (PvGameTarget* t = game->target; t != nullptr; t = t->next) {
        if (t->hit_state != 21 && t->hit_state != HitState_None)
            continue;

        bool is_slide = t->slide_left || t->slide_right || t->slide_chain ||
                        (t->target_type >= 12 && t->target_type <= 16) ||
                        (t->target_type >= 23 && t->target_type <= 24);
        if (is_slide)
            continue;

        if (!first_target || t->target_index < first_target->target_index) {
            first_target = t;
        }
    }

    if (!first_target)
        return;

    float time_rem = first_target->flying_time_remaining;
    if (time_rem > 0.008f || time_rem < -0.028f)
        return;

    // 4. Process initial note hit
    for (PvGameTarget* target = game->target; target != nullptr; target = target->next) {
        if (target->hit_state != 21 && target->hit_state != HitState_None)
            continue;

        bool is_same_chord = false;
        if (target == first_target) {
            is_same_chord = true;
        } else if (first_target->multi_count >= 0 && target->multi_count == first_target->multi_count) {
            is_same_chord = true;
        }

        if (!is_same_chord)
            continue;

        TargetStateEx* ex = GetTargetStateEx(target);

        // Regular notes (0..7, 18..21)
        if ((target->target_type >= 0 && target->target_type <= 7) ||
            (target->target_type >= 18 && target->target_type <= 21)) {

            int32_t base_type = (target->target_type >= 18)
                                ? (target->target_type - 18)
                                : (target->target_type % 4);

            switch (base_type) {
                case 0: TapButton(2, 16); break;
                case 1: TapButton(0, 32); break;
                case 2: TapButton(1, 64); break;
                case 3: TapButton(3, 128); break;
            }

            if (target->target_type >= 18 && target->target_type <= 21) {
                if (GetPVGameData())
                    GetPVGameData()->is_success_branch = true;
            }
            continue;
        }

        // NC Double Notes (29..32)
        if (target->target_type >= TargetType_UpW && target->target_type <= TargetType_LeftW) {
            switch (target->target_type) {
                case TargetType_UpW:    TapButton(2, 16 | 1); break;
                case TargetType_RightW: TapButton(0, 32 | 2); break;
                case TargetType_DownW:  TapButton(1, 64 | 4); break;
                case TargetType_LeftW:  TapButton(3, 128 | 8); break;
            }
            continue;
        }

        // NC Long notes (33..36)
        if (target->target_type >= TargetType_TriangleLong && target->target_type <= TargetType_SquareLong) {
            if (!ex || !ex->long_end) {
                switch (target->target_type) {
                    case TargetType_TriangleLong: TapButton(2, 16); break;
                    case TargetType_CircleLong:   TapButton(0, 32); break;
                    case TargetType_CrossLong:    TapButton(1, 64); break;
                    case TargetType_SquareLong:   TapButton(3, 128); break;
                }
            } else {
                switch (target->target_type) {
                    case TargetType_TriangleLong: s_auto_down[2] &= ~16; break;
                    case TargetType_CircleLong:   s_auto_down[0] &= ~32; break;
                    case TargetType_CrossLong:    s_auto_down[1] &= ~64; break;
                    case TargetType_SquareLong:   s_auto_down[3] &= ~128; break;
                }
            }
            continue;
        }

        // Initial hit for Rush notes
        if (ex && ex->IsRushNote() && !ex->holding) {
            switch (target->target_type) {
                case TargetType_TriangleRush: TapButton(2, 16); break;
                case TargetType_CircleRush:   TapButton(0, 32); break;
                case TargetType_CrossRush:    TapButton(1, 64); break;
                case TargetType_SquareRush:   TapButton(3, 128); break;
            }
            continue;
        }
    }
}

void PostUpdate(PVGameArcade* game) {
    if (!g_enabled || !game || !game->target) return;

    // 1. Hold sustain logic with explicit button mapping
    for (TargetStateEx* tgt : state.target_references) {
        if (tgt && tgt->IsLongNoteStart() && tgt->holding) {
            int32_t btn_id = -1;
            switch (tgt->target_type) {
                case TargetType_TriangleLong: btn_id = Button_Triangle; break;
                case TargetType_CircleLong:   btn_id = Button_Circle;   break;
                case TargetType_CrossLong:    btn_id = Button_Cross;    break;
                case TargetType_SquareLong:   btn_id = Button_Square;   break;
            }
            if (btn_id != -1) {
                macro_state.buttons[btn_id].data[0].down = true;
                macro_state.buttons[btn_id].data[0].up   = false;
                macro_state.raw_buttons[btn_id].data[0].down = true;
                macro_state.raw_buttons[btn_id].data[0].up   = false;
            }
        }

        // 2. Trigger audio feedback and tap state for Rush spam
        if (tgt && tgt->IsRushNote() && tgt->holding) {
            int32_t btn_id = -1;
            switch (tgt->target_type) {
                case TargetType_TriangleRush: btn_id = Button_Triangle; break;
                case TargetType_CircleRush:   btn_id = Button_Circle;   break;
                case TargetType_CrossRush:    btn_id = Button_Cross;    break;
                case TargetType_SquareRush:   btn_id = Button_Square;   break;
            }
            if (btn_id != -1 && (s_auto_tapped[0] || s_auto_tapped[1] || s_auto_tapped[2] || s_auto_tapped[3])) {
                macro_state.buttons[btn_id].data[0].tapped = true;
            }
        }
    }

    PvGameTarget* first_target = nullptr;
    for (PvGameTarget* t = game->target; t != nullptr; t = t->next) {
        if (t->hit_state != 21 && t->hit_state != HitState_None) continue;
        bool is_slide = t->slide_left || t->slide_right || t->slide_chain || (t->target_type >= 12 && t->target_type <= 16) || (t->target_type >= 23 && t->target_type <= 24);
        if (is_slide) continue;
        if (!first_target || t->target_index < first_target->target_index) { first_target = t; }
    }
    if (!first_target) return;
    float time_rem = first_target->flying_time_remaining;
    if (time_rem > 0.008f || time_rem < -0.028f) return;

    for (PvGameTarget* target = game->target; target != nullptr; target = target->next) {
        if (target->hit_state != 21 && target->hit_state != HitState_None) continue;
        bool is_same_chord = false;
        if (target == first_target) { is_same_chord = true; } else if (first_target->multi_count >= 0 && target->multi_count == first_target->multi_count) { is_same_chord = true; }
        if (!is_same_chord) continue;

        TargetStateEx* ex = GetTargetStateEx(target);
        if (ex && ex->IsStarLikeNote() && target->target_type != 40 && target->target_type != TargetType_StarW) {
            macro_state.buttons[Button_LStick].data[0].tapped = true;
            macro_state.buttons[Button_LStick].data[0].down   = true;
            continue;
        }
        if (target->target_type == TargetType_StarW || target->target_type == 39) {
            macro_state.buttons[Button_LStick].data[0].tapped = true;
            macro_state.buttons[Button_LStick].data[0].down   = true;
            macro_state.buttons[Button_RStick].data[0].tapped = true;
            macro_state.buttons[Button_RStick].data[0].down   = true;
            continue;
        }
        if (target->target_type == 40) {
            macro_state.buttons[Button_LStick].data[0].tapped = true;
            macro_state.buttons[Button_LStick].data[0].down   = true;
            macro_state.buttons[Button_RStick].data[0].tapped = true;
            macro_state.buttons[Button_RStick].data[0].down   = true;
            if (GetPVGameData()) GetPVGameData()->is_success_branch = true;
            continue;
        }
        if (ex && ex->IsRushNote() && target->target_type == TargetType_StarRush) {
            macro_state.buttons[Button_LStick].data[0].tapped = true;
            continue;
        }

        // 3. Release hold notes
        if (target->target_type >= TargetType_TriangleLong && target->target_type <= TargetType_SquareLong) {
            if (ex && ex->long_end) {
                int32_t btn_id = -1;
                switch (target->target_type) {
                    case TargetType_TriangleLong: btn_id = Button_Triangle; break;
                    case TargetType_CircleLong:   btn_id = Button_Circle;   break;
                    case TargetType_CrossLong:    btn_id = Button_Cross;    break;
                    case TargetType_SquareLong:   btn_id = Button_Square;   break;
                }
                if (btn_id != -1) {
                    macro_state.buttons[btn_id].data[0].down     = false;
                    macro_state.buttons[btn_id].data[0].up       = true;
                    macro_state.buttons[btn_id].data[0].released = true;
                    macro_state.raw_buttons[btn_id].data[0].down     = false;
                    macro_state.raw_buttons[btn_id].data[0].released = true;
                }
            }
            continue;
        }
    }
}

void ProcessSlideHit(
    PVGameArcade* game, int32_t& final_hit_state, bool* play_default_se, size_t* rating_count,
    diva_nc::vec2* rating_pos, SoundEffect* se, int32_t* multi_count, float* player_hit_time,
    int32_t* target_index, bool* is_success_note, bool* slide, bool* slide_chain,
    bool* slide_chain_start, bool* slide_chain_max, bool* slide_chain_continues
) {
    if (!g_enabled || !game || !game->target) return;
    if (game->target->target_index < s_last_hit_target) {
        s_last_hit_target = -1;
        s_last_hit_target_second = -1;
    }

    // Check both target indices to prevent duplicate trigger on paired sliders
    if (*target_index != -1 && (*target_index == s_last_hit_target || *target_index == s_last_hit_target_second)) {
        final_hit_state = 21; *rating_count = 0; *play_default_se = false; *slide_chain = true; *slide_chain_continues = true; return;
    }
    if (final_hit_state != 21) return;

    auto play_flick_sound = [&]() {
        if (se != nullptr && se->slide.c_str() && se->slide.c_str()[0] != '\0') {
            sound::PlaySoundEffect(1, se->slide.c_str(), 1.0f);
        }
    };

    for (PvGameTarget* t = game->target; t != nullptr; t = t->next) {
        if (t->hit_state != 21) continue;
        bool is_slide = t->slide_left || t->slide_right || t->slide_chain || (t->target_type >= 12 && t->target_type <= 16) || (t->target_type >= 23 && t->target_type <= 24);
        if (!is_slide) continue;

        if (t->flying_time_remaining <= 0.038f && t->flying_time_remaining >= -0.04f) {
            s_last_hit_target = t->target_index;

            // Chain slide
            if (t->slide_chain) {
                t->hit_state = 0; t->player_hit_time = t->flying_time_remaining;
                final_hit_state = 0; *slide = false; *slide_chain = true; *slide_chain_start = t->slide_chain_start;
                *slide_chain_max = t->slide_chain_end; *slide_chain_continues = true; *rating_count = 1; *multi_count = 1;
                *target_index = t->target_index; *player_hit_time = t->flying_time_remaining; *rating_pos = t->target_pos; *play_default_se = false;
                diva_nc::vec2 p = GetScaledPosition(t->target_pos);
                aet::PlayLayer(3, 8, 0x20000, "hit_eff00", &p, 0, nullptr, nullptr, -1.0f, -1.0f, 0, nullptr);
                if (t->target_type == 23 || t->target_type == 24) {
                    if (is_success_note) *is_success_note = true;
                    if (GetPVGameData()) GetPVGameData()->is_success_branch = true;
                }
                break;
            }

            // Find paired slide (dual slider)
            PvGameTarget* second_slide = nullptr;
            for (PvGameTarget* t2 = t->next; t2 != nullptr; t2 = t2->next) {
                if (t2->hit_state == 21) {
                    bool is_s2 = t2->slide_left || t2->slide_right || t2->slide_chain || (t2->target_type >= 12 && t2->target_type <= 16) || (t2->target_type >= 23 && t2->target_type <= 24);
                    if (is_s2 && fabsf(t2->flying_time_remaining - t->flying_time_remaining) < 0.03f) {
                        second_slide = t2;
                        s_last_hit_target_second = t2->target_index;
                        break;
                    }
                }
            }

            t->hit_state = 0; t->player_hit_time = t->flying_time_remaining;
            final_hit_state = 0; *slide = true; *slide_chain = false; *slide_chain_start = false; *slide_chain_max = false;
            *slide_chain_continues = false; *rating_count = 1; *multi_count = 1; *target_index = t->target_index;
            *player_hit_time = t->flying_time_remaining; *play_default_se = false;

            if (second_slide != nullptr) {
                second_slide->hit_state = 0;
                second_slide->player_hit_time = second_slide->flying_time_remaining;
                diva_nc::vec2 center = (t->target_pos + second_slide->target_pos) * 0.5f;
                *rating_pos = center;
                diva_nc::vec2 p = GetScaledPosition(center);
                aet::PlayLayer(3, 8, 0x20000, "hit_eff00", &p, 0, nullptr, nullptr, -1.0f, -1.0f, 0, nullptr);
                play_flick_sound();
            } else {
                *rating_pos = t->target_pos;
                diva_nc::vec2 p = GetScaledPosition(t->target_pos);
                aet::PlayLayer(3, 8, 0x20000, "hit_eff00", &p, 0, nullptr, nullptr, -1.0f, -1.0f, 0, nullptr);
                play_flick_sound();
            }

            if (t->target_type == 23 || t->target_type == 24) {
                if (is_success_note) *is_success_note = true;
                if (GetPVGameData()) GetPVGameData()->is_success_branch = true;
            }
            break;
        }
    }
}

void init() {
    GetPvKeyStateDownHook::InstallAtOffset(0x1b7750);
    GetPvKeyStateTappedHook::InstallAtOffset(0x1b77a0);
}

} // namespace AutoPlay
