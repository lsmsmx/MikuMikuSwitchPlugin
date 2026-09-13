#include <stdint.h>
#include <cstring>
#include "lib.hpp"
#include "../nc_state.hpp"
#include "logger.hpp"
#include "../save_data.hpp"
#include "target.hpp"
#include "chance_time.hpp"
#include "hit_state.hpp"
#include "score.hpp"
#include "sound_effects.hpp"
#include "../diva_nc.hpp"

struct NCSharedGameState
{
	std::vector<PvGameTarget*> active_group;
	std::vector<std::pair<PvGameTarget*, TargetStateEx*>> group;
	bool mute_slide_chime;

	NCSharedGameState()
	{
		active_group.reserve(4);
		group.reserve(4);
		mute_slide_chime = false;
	}

	void Reset()
	{
		active_group.clear();
		group.clear();
		mute_slide_chime = false;
	}

	void PushActiveTarget(PvGameTarget* target)
	{
		active_group.push_back(target);
		if (group.empty())
		{
			group.emplace_back(target, GetTargetStateEx(target));
			if (target->multi_count < 0)
				return;

			for (PvGameTarget* prev = target->prev; prev && prev->multi_count == target->multi_count; prev = prev->prev)
				group.emplace_back(prev, GetTargetStateEx(prev));

			for (PvGameTarget* next = target->next; next && next->multi_count == target->multi_count; next = next->next)
				group.emplace_back(next, GetTargetStateEx(next));
		}
	}

} static game_state;

uint64_t g_used_held_buttons = 0;

static bool PlayNoteSoundEffectOnHit(PvGameTarget* target, TargetStateEx* ex);
static bool CheckContinuousNoteSoundEffects(PvGameTarget* target, TargetStateEx* ex);

// 0x430A0000 = 138.0f (default), 0x43150000 = 149.0f (Chance Time)
uint32_t g_target_height_bits = 0x430A0000;

// 1. Hook write to internal register W8 (0x1b98e0)
HOOK_DEFINE_INLINE(FrameHeightHook_W8) {
	static void Callback(exl::hook::nx64::InlineCtx* ctx) {
		ctx->W[8] = g_target_height_bits;
	}
};

// 2. Hook write to internal register X9 (0x1b98d8)
HOOK_DEFINE_INLINE(FrameHeightHook_X9) {
	static void Callback(exl::hook::nx64::InlineCtx* ctx) {
		uint64_t imm16 = (g_target_height_bits >> 16) & 0xFFFF;
		ctx->X[9] = (ctx->X[9] & 0x0000FFFFFFFFFFFFull) | (imm16 << 48);
	}
};

// 1. GetHitStateInternal (0x1b11f0)
HOOK_DEFINE_TRAMPOLINE(GetHitStateInternalHook) {
	static int32_t Callback(PVGameArcade* game, PvGameTarget* target, uint16_t a3, uint16_t a4) {
		game_state.PushActiveTarget(target);

		int32_t t_type = target ? target->target_type : -1;
		if (target->target_type < TargetType_Custom || target->target_type >= TargetType_Max)
			return Orig(game, target, a3, a4);

		TargetStateEx* ex = GetTargetStateEx(target);
		if (!ex)
			return HitState_Worst;
		ex->target_type = target->target_type;

		bool success = false;
		int32_t hit_state = nc::JudgeNoteHit(game, &target, &ex, 1, &success);

		if (success)
			GetPVGameData()->is_success_branch = true;

		if (hit_state != HitState_None)
		{
			if (ex->IsLongNoteStart())
			{
				if (ex->IsWrong())
				{
					state.PopTarget(ex);
					ex->StopAet();
					ex->next->force_hit_state = ex->hit_state;
				}
				else
					ex->SetLongNoteAet();
			}
			else if (ex->IsRushNote())
			{
				if (!ex->IsWrong())
				{
					ex->SetRushNoteAet();
					se_mgr.StartRushBackSE();
				}
				else
					state.PopTarget(ex);
			}
			else if (ex->IsLongNoteEnd())
			{
				ex->prev->StopAet();
				state.PopTarget(ex->prev);
			}
			else if (ex->IsLinkNoteEnd() && nc::IsHitGreat(hit_state))
			{
				TargetStateEx* chain = nullptr;
				for (chain = ex; chain != nullptr && chain->prev != nullptr; chain = chain->prev) {}

				if (chain != nullptr)
				{
					chain->StopAet();
				}
				ex->StopAet();
			}
		}
		return hit_state;
	}
};

// 2. GetHitState (0x1aef20)
HOOK_DEFINE_TRAMPOLINE(GetHitStateHook) {
	static int32_t Callback(
		PVGameArcade* game,
		bool* play_default_se,
		size_t* rating_count,
		diva_nc::vec2* rating_pos,
		int32_t* a5,
		SoundEffect* se,
		int32_t* multi_count,
		float* player_hit_time,
		int32_t* target_index,
		bool* is_success_note,
		bool* slide,
		bool* slide_chain,
		bool* slide_chain_start,
		bool* slide_chain_max,
		bool* slide_chain_continues,
		void* a16
	) {
		int32_t final_hit_state = HitState_None;

		game_state.Reset();
		static int32_t last_update_frame = -1;
		macro_state.Update(game->ptr08, 0);

		if (ShouldUpdateTargets())
		{
			int32_t disp_score = 0;
			int32_t disp_count = 0;
			diva_nc::vec2 disp_pos = { };
			auto addTargetScoreDisp = [&](TargetStateEx* tgt)
			{
				disp_score += tgt->score_bonus + tgt->shared_data->ct_score_bonus;
				disp_pos += tgt->target_pos;
				disp_count++;
			};

			for (auto it = state.target_references.begin(); it != state.target_references.end();)
			{
				TargetStateEx* tgt = *it;

				// NOTE: Poll input for ongoing long notes
				if (tgt->IsLongNoteStart() && tgt->holding)
				{
					bool is_in_zone = false;

					// NOTE: Check if the end target is in it's timing window
					if (tgt->next && tgt->next->org != nullptr)
					{
						float time = tgt->next->org->flying_time_remaining;
						is_in_zone = time >= game->sad_late_window && time <= game->sad_early_window;
					}

					score::CalculateSustainBonus(tgt);
					addTargetScoreDisp(tgt);

					// NOTE: Check if the start target button has been released;
					//       if the end note is not inside its timing zone,
					//       automatically mark it as a fail.
					if (!nc::CheckLongNoteHolding(tgt) && !is_in_zone)
					{
						if (tgt->next)
							tgt->next->force_hit_state = HitState_Worst;
						tgt->StopAet();
						tgt->holding = false;
						it = state.target_references.erase(it);
						se_mgr.EndLongSE(true);
						GetPVGameData()->ui.RemoveBonusText();
						continue;
					}
				}
				// NOTE: Poll input for ongoing rush notes
				else if (tgt->IsRushNote() && tgt->holding)
				{
					if (nc::CheckRushNotePops(tgt))
					{
						GetPVGameData()->score += score::IncreaseRushPopCount(tgt);
						addTargetScoreDisp(tgt);
						state.PlayRushHitEffect(GetScaledPosition(tgt->target_pos), 0.6f * (1.0f + tgt->bal_scale), false);

						if (tgt->target_type == TargetType_StarRush)
						{
							se_mgr.PlayStarSE();
							game->mute_slide_chime = true;
							*play_default_se = false;
						}
					}
				}

				it++;
			}

			if (disp_score > 0 && disp_count > 0)
				GetPVGameData()->ui.SetBonusText(disp_score, disp_pos / disp_count);
		}

		final_hit_state = Orig(
			game,
			play_default_se,
			rating_count,
			rating_pos,
			a5,
			se,
			multi_count,
			player_hit_time,
			target_index,
			is_success_note,
			slide,
			slide_chain,
			slide_chain_start,
			slide_chain_max,
			slide_chain_continues,
			a16
		);

		if (!ShouldUpdateTargets())
			return final_hit_state;

		if (final_hit_state != HitState_None && game_state.group.size() > 0)
		{
			int32_t total_disp_score = 0;
			diva_nc::vec2 calc_target_pos = {};

			if (nc::IsHitCorrect(final_hit_state))
			{
				if (state.chance_time.CheckTargetInRange(game_state.group[0].first->target_index))
				{
					int32_t bonus = score::GetChanceTimeScoreBonus(nc::GetHitStateBase(final_hit_state));
					if (state.GetGameStyle() != GameStyle_Console) {
						GetPVGameData()->score += bonus;
					}
					state.score.ct_score_bonus += bonus;
					game_state.group[0].second->shared_data->ct_score_bonus = bonus;
					total_disp_score += bonus;
				}
			}

			for (auto& [target, ex] : game_state.group)
			{
				ex->hit_state = target->hit_state;

				if (nc::IsHitCorrect(ex->hit_state))
				{
					int32_t disp_score = 0;
					int32_t hit_bonus = score::CalculateHitScoreBonus(ex, &disp_score);
					if (state.GetGameStyle() != GameStyle_Console) {
						GetPVGameData()->score += hit_bonus;
					}

					total_disp_score += disp_score;
					calc_target_pos = calc_target_pos + target->target_pos;

					if (ex->IsLongNoteEnd())
						state.score.sustain_bonus += ex->prev->score_bonus;

					if (ex->target_hit_effect_id >= 0)
					{
						std::string effect_name =
							GetPVGameData()->is_success_branch
							? state.success_target_effect_map[ex->target_hit_effect_id]
							: state.fail_target_effect_map[ex->target_hit_effect_id];

						if (!effect_name.empty())
						{
							std::shared_ptr<AetElement> eff = state.ui.PushHitEffect();
							if (eff)
							{
								eff->SetLayer(effect_name, 0x20000, 7, 13, "", "", nullptr);
								eff->SetPosition(diva_nc::vec3(GetScaledPosition(ex->target_pos), 0.0f));
							}
						}
					}
				}

				PlayNoteSoundEffectOnHit(target, ex);
				CheckContinuousNoteSoundEffects(target, ex);

				if (ex->IsLongNoteStart() && nc::IsHitCorrect(ex->hit_state))
					*play_default_se = false;
			}

			game->mute_slide_chime |= game_state.mute_slide_chime;
			GetPVGameData()->ui.SetBonusText(total_disp_score, calc_target_pos / game_state.group.size());
		}

		if (nc::IsHitGreat(final_hit_state))
		{
			if (state.chance_time.CheckTargetInRange(*target_index))
				state.chance_time.targets_hit += 1;
		}

		if (final_hit_state != HitState_None)
		{
			for (TechZoneState& tz : state.tech_zones)
				tz.PushNewHitState(*target_index, final_hit_state);
		}

		int32_t snd_prio = nc::GetSharedData().sound_prio;
		bool should_play_star_se = true;

		if (snd_prio == 2 && game_state.group.size() > 0)
		{
			for (auto& [target, ex] : game_state.group)
			{
				if (target->flying_time_remaining >= game->sad_late_window &&
					target->flying_time_remaining <= game->sad_early_window)
				{
					if (target->target_type >= TargetType_UpW && target->target_type <= TargetType_LeftW)
						*play_default_se = false;
					else if (target->target_type == TargetType_StarW)
					{
						should_play_star_se = false;
						game->mute_slide_chime = true;
					}
				}
			}
		}

		if (should_play_star_se && *play_default_se && nc::GetSharedData().stick_control_se == 1 && state.GetGameStyle() != GameStyle_Arcade)
		{
			if (macro_state.GetStarHit())
			{
				se_mgr.PlayStarSE();
				game->mute_slide_chime = true;
			}
		}

		return final_hit_state;
	}
};

// Register preserve stub ensuring proper compiler barrier without I/O overhead
__attribute__((noinline))
static void NC_GhostLog(int32_t type, int32_t state, bool correct, bool long_start, bool star_like) {
	__asm__ volatile("" : : "r"(type), "r"(state), "r"(correct), "r"(long_start), "r"(star_like) : "memory");
}

static bool PlayNoteSoundEffectOnHit(PvGameTarget* target, TargetStateEx* ex)
{
	if (ex->hit_state == HitState_Worst || ex->hit_state == HitState_None)
		return false;

	NC_GhostLog(ex->target_type, ex->hit_state, nc::IsHitCorrect(ex->hit_state), ex->IsLongNoteStart(), ex->IsStarLikeNote());

	if (nc::IsHitCorrect(ex->hit_state))
	{
		if (ex->IsNormalDoubleNote())
		{
			se_mgr.PlayDoubleSE();
			return true;
		}
		else if (ex->target_type == TargetType_StarW)
		{
			se_mgr.PlayStarDoubleSE();
			game_state.mute_slide_chime = true;
			return true;
		}
		else if (ex->IsRushNote())
		{
			if (!ex->IsStarLikeNote())
				se_mgr.PlayButtonSE();
			se_mgr.StartRushBackSE();
		}
		else if (ex->IsLinkNoteStart())
			se_mgr.StartLinkSE();
		else if (ex->IsLongNoteStart())
		{
			se_mgr.StartLongSE();
		}

		if (ex->IsStarLikeNote())
		{
			if (ex->target_type == TargetType_ChanceStar && state.chance_time.GetFillRate() == 15)
				se_mgr.PlayCymbalSE();
			else
			{
				se_mgr.PlayStarSE();
			}

			game_state.mute_slide_chime = true;
		}
	}
	else if (nc::IsHitWrong(ex->hit_state))
	{
		if (ex->IsNormalDoubleNote() || (ex->IsRushNote() && !ex->IsStarLikeNote()) || ex->IsLongNote())
			se_mgr.PlayButtonSE();
	}

	return true;
}

static bool CheckContinuousNoteSoundEffects(PvGameTarget* target, TargetStateEx* ex)
{
	if (ex->hit_state == HitState_None)
		return false;

	if (ex->IsLinkNoteEnd())
		se_mgr.EndLinkSE();
	else if (ex->IsLongNoteEnd())
	{
		se_mgr.EndLongSE(!nc::IsHitCorrect(ex->hit_state));
	}
	else if (ex->IsRushNote() && ex->length_remaining <= 0.0f)
		se_mgr.EndRushBackSE(ex->bal_hit_count >= ex->bal_max_hit_count);

	return true;
}

// 3. UpdateLife (0x173880)
HOOK_DEFINE_TRAMPOLINE(UpdateLifeHook) {
	static void Callback(PVGameData* a1, int32_t hit_state, bool a3, bool is_challenge_time, int32_t a5, bool a6, bool a7, bool a8) {
		Orig(
			a1,
			hit_state,
			a3,
			is_challenge_time || state.chance_time.enabled,
			a5,
			a6,
			a7,
			a8
		);
	}
};

// 4. ExecuteModeSelect (0x18bb00)
HOOK_DEFINE_TRAMPOLINE(ExecuteModeSelectHook) {
	static void Callback(PVGamePvData* pv_data, int32_t op) {
		int32_t difficulty = pv_data->script_buffer[pv_data->script_pos + 1];
		int32_t mode = pv_data->script_buffer[pv_data->script_pos + 2];

		if (dsc::IsCurrentDifficulty(difficulty) && !game::IsPvMode())
		{
			switch (mode)
			{
			case ModeSelect_ChallengeStart:
				g_target_height_bits = 0x430A0000; // Reset to 138.0f
				break;
			case ModeSelect_ChanceStart:
				g_target_height_bits = 0x43150000; // Set to 149.0f
				SetChanceTimeMode(&pv_data->pv_game->ui, ModeSelect_ChanceStart);
				break;
			case ModeSelect_ChanceEnd:
				if (state.chance_time.successful && state.GetGameStyle() == GameStyle_Console)
					pv_data->pv_game->score += score::GetChanceTimeSuccessBonus();

				SetChanceTimeMode(&pv_data->pv_game->ui, ModeSelect_ChanceEnd);
				g_target_height_bits = 0x430A0000; // Reset to 138.0f
				break;
			case ModeSelect_TechZoneStart:
				if (state.tech_zone_index < state.tech_zones.size())
				{
					state.tz_disp.data = &state.tech_zones[state.tech_zone_index];
					state.tz_disp.end = false;
				}
				break;
			case ModeSelect_TechZoneEnd:
				if (state.tech_zone_index < state.tech_zones.size())
				{
					if (TechZoneState& tz = state.tech_zones[state.tech_zone_index]; tz.IsValid())
					{
						if (tz.IsSuccessful() && state.GetGameStyle() != GameStyle_Arcade)
							pv_data->pv_game->score += score::GetTechZoneSuccessBonus();
					}

					state.tz_disp.end = true;
					state.tech_zone_index++;
				}
				break;
			}
		}

		Orig(pv_data, op);
	}
};

// UpdateGaugeFrame (0x1c07e0)
HOOK_DEFINE_TRAMPOLINE(UpdateGaugeFrameHook) {
	static void Callback(PVGameUI* ui) {
		Orig(ui);

		if (state.chance_time.enabled)
			SetChanceTimeStarFill(ui, state.chance_time.GetFillRate());
		SetChanceTimePosition(ui);
		state.tz_disp.Ctrl();
	}
};

// CalculatePercentage (0x173fb0)
HOOK_DEFINE_TRAMPOLINE(CalculatePercentageHook) {
	static void Callback(PVGameData* pv_game) {
		if (pv_game->scoring_enabled) {
			if (state.GetScoreMode() == ScoreMode_F2nd || state.GetScoreMode() == ScoreMode_Franken) {
				float perc = score::CalculatePercentage(pv_game);
				pv_game->percentage = perc;
				return;
			}
		}

		Orig(pv_game);
	}
};

// 6. GetHitStatePlaySE (0x1af5a0)
HOOK_DEFINE_INLINE(GetHitStatePlaySEHook) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        // 1. Save CPU flags (NZCV) before register manipulation
        uint64_t nzcv_backup;
        __asm__ volatile ("mrs %0, nzcv" : "=r"(nzcv_backup));

        // 2. Resolve GP registers X0-X29
        uint64_t* gp_regs = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(ctx) + 0x200);

        // X23 holds target pointer
        PvGameTarget* target = reinterpret_cast<PvGameTarget*>(gp_regs[23]);

        if (target != nullptr) {
            int32_t target_type = target->target_type;

            // Mute original sound effect for New Classics custom notes
            if (target_type >= TargetType_Custom && target_type <= TargetType_Max) {
                static const char* empty_se = "";
                gp_regs[8] = reinterpret_cast<uint64_t>(empty_se);
                gp_regs[9] = reinterpret_cast<uint64_t>(empty_se);
            }
        }

        // 3. Restore CPU condition flags
        __asm__ volatile ("msr nzcv, %0" : : "r"(nzcv_backup) : "cc");
    }
};

void InstallGameHooks()
{
	GetHitStateInternalHook::InstallAtOffset(0x1b11f0);
	GetHitStateHook::InstallAtOffset(0x1aef20);
	UpdateLifeHook::InstallAtOffset(0x173880);
	ExecuteModeSelectHook::InstallAtOffset(0x18bb00);
	UpdateGaugeFrameHook::InstallAtOffset(0x1c07e0);

    CalculatePercentageHook::InstallAtOffset(0x173fb0);
    exl::patch::CodePatcher(0x00171184).Write<uint32_t>(0xAA1303E0); // mov   x0, x19
    exl::patch::CodePatcher(0x00171188).Write<uint32_t>(0x94000B8A); // bl    0x00173FB0 (CalculatePercentage)
    exl::patch::CodePatcher(0x0017118C).Write<uint32_t>(0xB841B328); // ldur  w8, [x25, #0x1b]
    exl::patch::CodePatcher(0x00171190).Write<uint32_t>(0x1E220100); // scvtf s0, w8
    exl::patch::CodePatcher(0x00171194).Write<uint32_t>(0x7100051F); // cmp   w8, #1
    exl::patch::CodePatcher(0x00171198).Write<uint32_t>(0x14000035); // b     0x0017126C

    GetHitStatePlaySEHook::InstallAtOffset(0x1af5a0);

    exl::patch::CodePatcher(0x1ad3dc).Write<uint32_t>(0xD503201F);
    exl::patch::CodePatcher(0x1af8dc).Write<uint32_t>(0xD503201F);

	FrameHeightHook_W8::InstallAtOffset(0x1b98e0);
	FrameHeightHook_X9::InstallAtOffset(0x1b98d8);
}
