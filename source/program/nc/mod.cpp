#include <stdint.h>
#include <vector>
#include <memory>
#include <utility>

#include "ModLoader.hpp"
#include "lib.hpp"
#include "nc_state.hpp"
#include "logger.hpp"
#include "save_data.hpp"
#include "util.hpp"
#include "db.hpp"
#include "diva_nc.hpp"

#include "game/hit_state.hpp"
#include "game/target.hpp"
#include "game/chance_time.hpp"
#include "game/game.hpp"
#include "game/dsc.hpp"
#include "game/sound_effects.hpp"

#include "ui/pv_sel.hpp"
#include "ui/customize_sel.hpp"
#include "ui/result.hpp"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

extern void ResetJitLinks(); // Forward declaration from target.cpp

// 1. TaskPvGameInit (0x64c910)
HOOK_DEFINE_TRAMPOLINE(TaskPvGameInitHook) {
	static bool Callback(uint64_t a1) {
		nc::ApplyConfig();
		state.files_loaded = false;
		state.dsc_loaded = false;
		state.file_state = 0;

		int32_t pv_id = game::GetPVLoadParam()->data[game::GetPVLoadParam()->data[0].int8].pv_id;
		if (pv_id == 700)
		{
			state.nc_song_entry.reset();
			state.nc_chart_entry.reset();
		}

		libcxx_string str;
		prj::string_view strv;
		aet::LoadAetSet(AetSetID, &str);
		spr::LoadSprSet(SprSetID, &strv);
		aet::LoadAetSet(14010080, &str);  // AET_NCGAM_TZ
		spr::LoadSprSet(14020080, &strv); // SPR_NCGAM_TZ

		if (state.nc_song_entry.has_value() && state.nc_song_entry->IsHitEffectsValid())
		{
			aet::LoadAetSet(state.nc_song_entry->target_hit_effect_aetset_id, &str);
			spr::LoadSprSet(state.nc_song_entry->target_hit_effect_sprset_id, &strv);
		}

		sound::RequestFarcLoad("rom/sound/se_nc.farc");

		state.Reset();
		se_mgr.Init();

		macro_state.sensivity = 1.0f - nc::GetSharedData().stick_sensitivity / 100.0f;
		return Orig(a1);
	}
};

// 2. TaskPvGameCtrl (0x64c940)
HOOK_DEFINE_TRAMPOLINE(TaskPvGameCtrlHook) {
	static bool Callback(uint64_t a1) {
		if (!state.files_loaded)
		{
			bool aet_loading = aet::CheckAetSetLoading(AetSetID);
			bool spr_loading = spr::CheckSprSetLoading(SprSetID);

			state.files_loaded = !aet_loading && !spr_loading &&
				!aet::CheckAetSetLoading(14010080) &&
				!spr::CheckSprSetLoading(14020080) &&
				!sound::IsFarcLoading("rom/sound/se_nc.farc");

			if (state.nc_song_entry.has_value() && state.nc_song_entry->IsHitEffectsValid())
			{
				state.files_loaded = state.files_loaded &&
					!aet::CheckAetSetLoading(state.nc_song_entry->target_hit_effect_aetset_id) &&
					!spr::CheckSprSetLoading(state.nc_song_entry->target_hit_effect_sprset_id);
			}
		}

		return Orig(a1);
	}
};

// 3. TaskPvGameDest (0x64c960)
HOOK_DEFINE_TRAMPOLINE(TaskPvGameDestHook) {
	static bool Callback(uint64_t a1) {
		state.ResetPlayState();
		state.ui.ResetAllLayers();

		if (state.files_loaded)
		{
			aet::UnloadAetSet(AetSetID);
			spr::UnloadSprSet(SprSetID);
			aet::UnloadAetSet(14010080);
			spr::UnloadSprSet(14020080);
			if (state.nc_song_entry.has_value() && state.nc_song_entry->IsHitEffectsValid())
			{
				aet::UnloadAetSet(state.nc_song_entry->target_hit_effect_aetset_id);
				spr::UnloadSprSet(state.nc_song_entry->target_hit_effect_sprset_id);
				state.fail_target_effect_map.clear();
				state.success_target_effect_map.clear();
			}
			sound::UnloadFarc("rom/sound/se_nc.farc");
			state.files_loaded = false;
		}

		return Orig(a1);
	}
};

void SyncAndLinkLongNotes()
{
	PVGameData* pv_game = GetPVGameData();
	if (!pv_game) return;

	// 1. Synchronize note types from loaded chart
	int32_t index = 0;
	for (PvDscTargetGroup& group : pv_game->pv_data.targets) {
		for (int i = 0; i < group.target_count; i++) {
			if (TargetStateEx* ex = GetTargetStateEx(index, i)) {
				ex->target_type = group.targets[i].type;
			}
		}
		index++;
	}

	// 2. Reset previous note links
	for (TargetStateEx& ex : state.target_ex) {
		ex.next = nullptr;
		ex.prev = nullptr;
		if (!ex.long_end_parsed)
			ex.long_end = false;
	}

	// 3. Mark sustain note ends
	bool open_longs[5] = { false, false, false, false, false };
	for (TargetStateEx& ex : state.target_ex) {
		int type = ex.target_type;
		if (type >= TargetType_TriangleLong && type <= TargetType_SquareLong) {
			int idx = type - TargetType_TriangleLong;
			if (ex.long_end_parsed) {
				open_longs[idx] = !ex.long_end;
				continue;
			}
			ex.long_end = open_longs[idx];
			open_longs[idx] = !open_longs[idx];
		} else if (type == TargetType_StarLong) {
			if (ex.long_end_parsed) {
				open_longs[4] = !ex.long_end;
				continue;
			}
			ex.long_end = open_longs[4];
			open_longs[4] = !open_longs[4];
		}
	}

	// 4. Link note start and end, calculate exact duration
	for (size_t i = 0; i < state.target_ex.size(); i++) {
		TargetStateEx* ex = &state.target_ex[i];

		if (ex->IsLongNoteStart()) {
			for (size_t j = i + 1; j < state.target_ex.size(); j++) {
				TargetStateEx* candidate = &state.target_ex[j];

				if (candidate->target_type == ex->target_type && candidate->long_end) {
					ex->next = candidate;
					candidate->prev = ex;

					// Calculate sustain duration directly from target hit times
					if (ex->length < 0.0f) {
						int64_t hit_time_start = pv_game->pv_data.targets[ex->target_index].hit_time;
						int64_t hit_time_end = pv_game->pv_data.targets[candidate->target_index].hit_time;
						ex->length = static_cast<float>(hit_time_end - hit_time_start) / 1000000000.0f;
					}
					break;
				}
			}
		}
	}
}

// 4. PVGameReset
HOOK_DEFINE_TRAMPOLINE(PVGameResetHook) {
	static void Callback(PVGameData* pv_game) {
		state.ResetPlayState();
		Orig(pv_game);
	}
};

// 5. PVGameLoaderFinishUp
HOOK_DEFINE_TRAMPOLINE(PVGameLoaderFinishUpHook) {
	static bool Callback(void* a1) {
		state.ResetPlayState();
		return Orig(a1);
	}
};

// 6. PVGameArcadeReset
HOOK_DEFINE_TRAMPOLINE(PVGameArcadeResetHook) {
	static void Callback(PVGameArcade* game) {
		state.ResetAetData();
		Orig(game);
	}
};

// 7. ParseTargets (0x175f00)
HOOK_DEFINE_TRAMPOLINE(ParseTargetsHook) {
	static int32_t Callback(PVGameData* pv_game) {
		int32_t ret = Orig(pv_game);

		// Step 1. Initialize custom note structures
		int32_t index = 0;
		for (PvDscTargetGroup& group : pv_game->pv_data.targets) {
			auto shared_data = std::make_shared<TargetStateExShared>();
			shared_data->Reset();
			for (int i = 0; i < group.target_count; i++) {
				if (TargetStateEx* ex = GetTargetStateEx(index, i); ex != nullptr) {
					ex->target_type = group.targets[i].type;
					ex->shared_data = shared_data;
					continue;
				}
				TargetStateEx ex = { };
				ex.target_index = index;
				ex.sub_index = i;
				ex.target_type = group.targets[i].type;
				ex.shared_data = shared_data;
				ex.link_start = false;
				ex.link_step = false;
				ex.link_end = false;
				ex.long_end = false;
				ex.next = nullptr;
				ex.prev = nullptr;
				ex.length = -1.0f; // -1 indicates automatic duration calculation
				ex.target_hit_effect_id = -1;
				ex.ResetPlayState();
				state.target_ex.push_back(ex);
			}
			index++;
		}
		auto findNextTarget = [&pv_game](size_t start_index, int32_t start_sub, int32_t type, int32_t type2, bool end) {
			for (size_t i = start_index; i < pv_game->pv_data.targets.size(); i++) {
				PvDscTargetGroup* group = &pv_game->pv_data.targets[i];
				for (int sub = start_sub; sub < group->target_count; sub++) {
					TargetStateEx* ex = GetTargetStateEx(i, sub);
					if (group->targets[sub].type == type || group->targets[sub].type == type2 || type == -1) {
						if (ex->long_end == end) return std::pair(&group->targets[sub], ex);
					}
				}
			}
			return std::pair<PvDscTarget*, TargetStateEx*>(nullptr, nullptr);
		};
		for (size_t i = 0; i < pv_game->pv_data.targets.size(); i++) {
			PvDscTargetGroup* group = &pv_game->pv_data.targets[i];
			for (int sub = 0; sub < group->target_count; sub++) {
				PvDscTarget* tgt = &group->targets[sub];
				TargetStateEx* ex = GetTargetStateEx(i, sub);

				if (ex->IsLongNoteStart()) {
					TargetStateEx* next = findNextTarget(i + 1, sub, tgt->type, -1, true).second;
					if (next != nullptr) {
						ex->next = next;
						next->prev = ex;
						if (ex->length < 0.0f) {
							PvDscTargetGroup* next_group = &pv_game->pv_data.targets[next->target_index];
							ex->length = static_cast<float>(next_group->hit_time - group->hit_time) / 1000000000.0f;
						}
					}
				}
				else if (tgt->type == TargetType_LinkStar || tgt->type == TargetType_LinkStarEnd) {
					if (tgt->type == TargetType_LinkStar) {
						if (ex->prev == nullptr) ex->link_start = true;
						TargetStateEx* next = findNextTarget(i + 1, sub, TargetType_LinkStar, TargetType_LinkStarEnd, false).second;
						if (next != nullptr) {
							ex->next = next;
							next->prev = ex;
						}
						ex->link_step = true;
					}
					else if (tgt->type == TargetType_LinkStarEnd) {
						ex->link_step = true;
						ex->link_end = true;
					}
				}
				else if (ex->IsRushNote()) ex->bal_max_hit_count = ex->length * 4.5f;
			}
		}

		// Step 2. Parse tech zones and chance time bounds from DSC
		state.tech_zones.clear();
		state.chance_time.first_target_index = -1;
		state.chance_time.last_target_index = -1;

		int64_t chance_start_time = -1;
		int64_t chance_end_time = -1;
		std::vector<std::pair<int64_t, int64_t>> tech_zone_times;

		int32_t* dsc_buf = pv_game->pv_data.script_buffer;

		if (dsc_buf != nullptr)
		{
			int64_t current_time_ns = 0;
			int32_t i = 0;

			if (dsc_buf[0] >= 0x10000000) {
				// Search for standard TIME opcode with time = 0
				bool found_start = false;
				for (int k = 1; k < 50; k++) {
					if (dsc_buf[k] == 1 && dsc_buf[k+1] == 0) {
						i = k;
						found_start = true;
						break;
					}
				}

				// Fallback for charts starting with non-zero/negative timestamps
				if (!found_start) {
					for (int k = 1; k < 50; k++) {
						if (dsc_buf[k] == 1) {
							i = k;
							found_start = true;
							break;
						}
					}
					if (!found_start) {
						i = 1;
					}
				}
			}

			while (true)
			{
				int32_t opcode = dsc_buf[i];
				if (opcode == 0) {
					break;
				}

				if (opcode == 1) // TIME
				{
					current_time_ns = static_cast<int64_t>(dsc_buf[i + 1]) * 10000LL;
				}
				else if (opcode == 26) // MODE_SELECT
				{
					int32_t difficulty = dsc_buf[i + 1];
					int32_t mode = dsc_buf[i + 2];
					bool diff_ok = dsc::IsCurrentDifficulty(difficulty);

					if (diff_ok)
					{
						switch (mode)
						{
						case ModeSelect_ChanceStart:
							chance_start_time = current_time_ns;
							break;
						case ModeSelect_ChanceEnd:
							chance_end_time = current_time_ns;
							break;
						case ModeSelect_TechZoneStart:
							tech_zone_times.emplace_back(current_time_ns, -1);
							break;
						case ModeSelect_TechZoneEnd:
							if (!tech_zone_times.empty()) {
								tech_zone_times.back().second = current_time_ns;
							}
							break;
						}
					}
				}

				auto* info = dsc::GetOpcodeInfo(opcode);
				if (info != nullptr) {
					i += info->length + 1;
				}
				else {
					// Stop parsing on invalid/unknown opcode to prevent infinite freeze
					break;
				}
			}
		}

		// Step 3. Map events to target notes
		if (chance_start_time != -1 && chance_end_time != -1)
		{
			for (size_t t = 0; t < pv_game->pv_data.targets.size(); t++)
			{
				int64_t ht = pv_game->pv_data.targets[t].hit_time;

				if (ht >= chance_start_time && ht <= chance_end_time)
				{
					if (state.chance_time.first_target_index == -1)
						state.chance_time.first_target_index = static_cast<int32_t>(t);
					state.chance_time.last_target_index = static_cast<int32_t>(t);
				}
			}
		}

		for (const auto& tz_time : tech_zone_times)
		{
			if (tz_time.first == -1 || tz_time.second == -1) continue;

			TechZoneState tz = { };
			tz.first_target_index = -1;
			tz.last_target_index = -1;
			tz.ResetPlayState();

			for (size_t t = 0; t < pv_game->pv_data.targets.size(); t++)
			{
				int64_t ht = pv_game->pv_data.targets[t].hit_time;
				if (ht >= tz_time.first && ht <= tz_time.second)
				{
					if (tz.first_target_index == -1)
						tz.first_target_index = static_cast<int32_t>(t);
					tz.last_target_index = static_cast<int32_t>(t);
				}
			}

			if (tz.IsValid()) {
				state.tech_zones.push_back(tz);
			}
		}

		if (state.GetGameStyle() != GameStyle_Arcade) {
			score::CalculateScoreReference(state.GetGameStyle(), &state.score, pv_game);
		}
		return pv_game->reference_score;
	}
};

void InstallModHooks()
{
	// Patch target type check in PVGameTarget::CreateAet (0x1b48a0)
	exl::patch::CodePatcher(0x1b48a0).Write<uint32_t>(0xd503201f);

	TaskPvGameInitHook::InstallAtOffset(0x64c910);
	TaskPvGameCtrlHook::InstallAtOffset(0x64c940);
	TaskPvGameDestHook::InstallAtOffset(0x64c960);
	PVGameResetHook::InstallAtOffset(0x1722d0);
	PVGameLoaderFinishUpHook::InstallAtOffset(0x19bd60);
	PVGameArcadeResetHook::InstallAtOffset(0x1ad6f0);
	ParseTargetsHook::InstallAtOffset(0x175f00);
}
