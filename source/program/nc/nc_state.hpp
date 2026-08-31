 
#pragma once

#include <optional>
#include <list>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <cmath>

#include "diva_nc.hpp"
#include "input.hpp"
#include "db.hpp"
#include "shared.hpp"
#include "game/score.hpp"
#include "game/tech_zone.hpp"
#include "ui/common.hpp"

constexpr float ChanceTimeRetainedRate = 0.05f; // 5%

enum TargetType : int32_t
{
	// FT
	TargetType_Triangle       = 0,
	TargetType_Circle         = 1,
	TargetType_Cross          = 2,
	TargetType_Square         = 3,
	TargetType_TriangleHold   = 4,
	TargetType_CircleHold     = 5,
	TargetType_CrossHold      = 6,
	TargetType_SquareHold     = 7,
	TargetType_Random         = 8,
	TargetType_RandomHold     = 9,
	TargetType_Previous       = 10,
	TargetType_0B             = 11,
	TargetType_SlideL         = 12,
	TargetType_SlideR         = 13,
	TargetType_0E             = 14,
	TargetType_ChainslideL    = 15,
	TargetType_ChainslideR    = 16,
	TargetType_11             = 17,
	TargetType_ChanceTriangle = 18,
	TargetType_ChanceCircle   = 19,
	TargetType_ChanceCross    = 20,
	TargetType_ChanceSquare   = 21,
	TargetType_16             = 22,
	TargetType_ChanceSlideL   = 23,
	TargetType_ChanceSlideR   = 24,

	// X (these are their actual IDs)
	TargetType_TriangleRush   = 25,
	TargetType_CircleRush     = 26,
	TargetType_CrossRush      = 27,
	TargetType_SquareRush     = 28,

	// PSP / F / F 2nd (Changed IDs so they don't overlap base game notes)
	TargetType_UpW            = 29,
	TargetType_RightW         = 30,
	TargetType_DownW          = 31,
	TargetType_LeftW          = 32,
	TargetType_TriangleLong   = 33,
	TargetType_CircleLong     = 34,
	TargetType_CrossLong      = 35,
	TargetType_SquareLong     = 36,
	TargetType_Star           = 37,
	TargetType_StarLong       = 38, // NOTE: Unused F mechanic, should I implement this?
	TargetType_StarW          = 39,
	TargetType_ChanceStar     = 40,
	TargetType_LinkStar       = 41,
	TargetType_LinkStarEnd    = 42,
	TargetType_StarRush       = 43,

	TargetType_Max,
	TargetType_Custom = 25
};

enum LinkStepState : int32_t
{
	LinkStepState_None      = 0,
	LinkStepState_FadeIn    = 1,
	LinkStepState_Normal    = 2,
	LinkStepState_GlowStart = 3,
	LinkStepState_Glow      = 4,
	LinkStepState_GlowEnd   = 5,
	LinkStepState_Wait      = 6,
	LinkStepState_Idle      = 7
};

enum SEType : int32_t
{
	SEType_Normal      = 1,
	SEType_Star        = 2,
	SEType_LongStart   = 3,
	SEType_LongRelease = 4,
	SEType_LongFail    = 5,
	SEType_Double      = 6,
	SEType_Cymbal      = 7,
	SEType_StarDouble  = 8,
	SEType_RushStart   = 9,
	SEType_RushPop     = 10,
	SEType_RushFail    = 11,
	SEType_LinkStart   = 12,
	SEType_LinkEnd     = 13,
	SEType_Max         = 14
};

// NOTE: Properties that should be shared between targets in a multi
struct TargetStateExShared
{
	int32_t ct_score_bonus = 0;

	void Reset();
};

struct TargetStateEx
{
	// NOTE: Static data; Information about the target.
	TargetStateEx* prev = nullptr;
	TargetStateEx* next = nullptr;
	int32_t target_type = -1;
	int32_t target_index = 0;
	int32_t sub_index = 0;
	float length = 0.0f;
	bool long_end = false;
	bool long_end_parsed = false;
	bool link_start = false;
	bool link_step = false;
	bool link_end = false;
	int32_t bal_max_hit_count = 0;

	// NOTE: Gameplay state
	ButtonState* hold_button = nullptr;
	PvGameTarget* org = nullptr;
	int32_t force_hit_state = HitState_None;
	std::shared_ptr<TargetStateExShared> shared_data;
	int32_t hit_state = HitState_None;
	float hit_time = 0.0f;
	float flying_time_max = 0.0f;
	float flying_time_remaining = 0.0f;
	float delta_time_max = 0.0f;
	float delta_time = 0.0f;
	float length_remaining = 0.0f;
	float kiseki_time = 0.0f;
	float alpha = 0.0f;
	bool holding = false;
	std::chrono::steady_clock::time_point last_hold_confirmed_time = {};
	bool success = false; // NOTE: If this note is a chance star, this determines if it's successful or not
	bool current_step = false; // NOTE: If this target is the current step of the link star chain
	int32_t step_state = LinkStepState_None;
	bool link_ending = false;

	float sustain_bonus_time = 0.0f;
	int32_t score_bonus = 0;
	bool double_tapped = false;
	int32_t bal_hit_count = 0;

	// NOTE: Visual info for long notes. This is kind of a workaround as to not mess too much
	//       with the vanilla game structs.
	int32_t target_aet = 0;
	int32_t button_aet = 0;
	int32_t bal_effect_aet = 0;
	diva_nc::vec2 target_pos = { };
	diva_nc::vec2 kiseki_pos = { }; // NOTE: Position where the kiseki will be updated from
	diva_nc::vec2 kiseki_dir = { }; // NOTE: Direction of the note
	diva_nc::vec2 kiseki_dir_norot = { };
	std::vector<SpriteVertex> kiseki;
	size_t vertex_count_max = 0;
	bool fix_long_kiseki = false;
	float bal_time = -1.0f;
	float bal_start_time = -1.0f;
	float bal_end_time = -1.0f;
	float bal_scale = 0.0f;

	int32_t target_hit_effect_id = -1; // NOTE: Index of the composition within the root composition of the HIT scene to play

	void ResetPlayState();
	void ResetAetData();
	bool IsChainSucessful();
	void StopAet(bool button = true, bool target = true, bool kiseki = true);
	bool SetLongNoteAet();
	bool SetLinkNoteAet();
	bool SetRushNoteAet();

	inline bool IsWrong() const
	{
		return (hit_state >= HitState_WrongCool && hit_state <= HitState_WrongSad) || hit_state == HitState_Worst;
	}

	inline bool IsLongNote() const
	{
		return target_type == TargetType_TriangleLong ||
			target_type == TargetType_CircleLong ||
			target_type == TargetType_CrossLong ||
			target_type == TargetType_SquareLong ||
			target_type == TargetType_StarLong;
	}

	inline bool IsStarLikeNote() const
	{
		return target_type == TargetType_Star ||
			target_type == TargetType_ChanceStar ||
			target_type == TargetType_LinkStar ||
			target_type == TargetType_LinkStarEnd ||
			target_type == TargetType_StarRush;
	}

	inline bool IsRushNote() const
	{
		return target_type == TargetType_TriangleRush ||
			target_type == TargetType_CircleRush ||
			target_type == TargetType_CrossRush ||
			target_type == TargetType_SquareRush ||
			target_type == TargetType_StarRush;
	}

	inline bool IsNormalDoubleNote() const
	{
		return target_type == TargetType_UpW ||
			target_type == TargetType_RightW ||
			target_type == TargetType_DownW ||
			target_type == TargetType_LeftW;
	}

	inline bool IsLongNoteStart() const { return IsLongNote() && !long_end; }
	inline bool IsLongNoteEnd()   const { return IsLongNote() && long_end; }
	inline bool IsLinkNote()      const { return link_step; }
	inline bool IsLinkNoteStart() const { return link_step && link_start; }
	inline bool IsLinkNoteEnd()   const { return link_step && link_end; }
};

struct ChanceState
{
	int32_t first_target_index = -1;
	int32_t last_target_index = -1;
	int32_t targets_hit = 0;
	bool enabled = false;
	bool successful = false;
	float chance_star_hit_time = 0.0f;

	inline bool IsValid() const { return first_target_index != -1 && last_target_index != -1; }

	inline void ResetPlayState()
	{
		targets_hit = 0;
		chance_star_hit_time = 0.0f;
		enabled = false;
		successful = false;
	}

	inline int32_t GetTargetCount() const;

	inline int32_t GetFillRate() const
	{
		const float max_threshold = 0.875f;
		const float target_count = GetTargetCount();
		if (target_count > 0.0f)
		{
			float percent = fminf(targets_hit / target_count / max_threshold, 1.0f);
			return static_cast<int32_t>(percent * 15);
		}

		return 0;
	}

	inline bool CheckTargetInRange(int32_t index) const
	{
		return IsValid() && (index >= first_target_index && index <= last_target_index);
	}
};

enum LayerUI : int32_t
{
	LayerUI_ChanceFrameTop = 0,
	LayerUI_ChanceFrameBottom,
	LayerUI_StarGaugeBase,
	LayerUI_StarGauge,
	LayerUI_ChanceTxt,
	LayerUI_BonusZone,
	LayerUI_BonusZoneText,
	LayerUI_Max
};

struct UIState
{
	int32_t aet_list[LayerUI_Max];
	std::shared_ptr<AetElement> elements[LayerUI_Max];
	bool aet_visibility[LayerUI_Max];

	std::shared_ptr<AetElement> hit_effect_buffer[64];
	int32_t hit_effect_index;

	UIState()
	{
		memset(aet_list, 0, sizeof(aet_list));
		memset(aet_visibility, 0, sizeof(aet_visibility));
		hit_effect_index = 0;
	}

	void SetLayer(int32_t index, bool visible, const char* name, int32_t prio, int32_t flags);
	std::shared_ptr<AetElement>& GetLayer(int32_t id);
	std::shared_ptr<AetElement> PushHitEffect();
	void ResetAllLayers();
};

struct StateEx
{
	static constexpr int32_t MaxHitEffectCount = 4;

	std::list<TargetStateEx*> target_references;
	FileHandler file_handler = nullptr;
	int32_t file_state = 0;
	bool dsc_loaded = false;
	bool files_loaded = false;
	std::vector<TargetStateEx> target_ex;
	ChanceState chance_time;
	std::vector<TechZoneState> tech_zones;
	TechZoneDispState tz_disp;
	size_t tech_zone_index = 0;
	UIState ui;
	int32_t effect_buffer[MaxHitEffectCount] = { };
	int32_t effect_index = 0;
	std::optional<db::SongEntry> nc_song_entry;
	std::optional<db::ChartEntry> nc_chart_entry;
	std::map<int32_t, std::string> fail_target_effect_map;
	std::map<int32_t, std::string> success_target_effect_map;
	ScoreState score;

	void ResetPlayState();
	void ResetAetData();
	void Reset();
	bool PushTarget(TargetStateEx* ex);
	bool PopTarget(TargetStateEx* ex);
	void PlayRushHitEffect(const diva_nc::vec2& pos, float scale, bool pop);

	int32_t GetScoreMode() const;
	int32_t GetGameStyle() const;
	int32_t CalculateTotalBonusScore() const;
};

// NOTE: Constants
constexpr uint32_t AetSetID = 16014000;
constexpr uint32_t AetSceneID = 16014001;
constexpr uint32_t SprSetID = 3068121241;

constexpr float KisekiRate = 60.0f; // NOTE: Framerate where the long note kiseki will be updated
constexpr float KisekiInterval = 1.0f / KisekiRate;

// NOTE: Global state
inline MacroState macro_state = { };
inline StateEx state = { };

// NOTE: Helper functions
TargetStateEx* GetTargetStateEx(int32_t index, int32_t sub_index);
TargetStateEx* GetTargetStateEx(const PvGameTarget* org);
inline int32_t ChanceState::GetTargetCount() const
{
	if (!IsValid()) return 0;
	int32_t count = 0;
	PVGameData* game = GetPVGameData();
	if (game != nullptr)
	{
		for (int32_t i = first_target_index; i <= last_target_index; i++)
		{
			auto& group = game->pv_data.targets[i];
			bool is_hittable = false;
			for (int sub = 0; sub < group.target_count; sub++)
			{
				TargetStateEx* ex = GetTargetStateEx(i, sub);
				if (ex != nullptr && !ex->long_end && !ex->link_end) {
					is_hittable = true;
					break;
				}
			}
			if (is_hittable) count++;
		}
	}
	return count;
}

extern "C" {
	StateEx* GetState();
	int32_t GetStateGameStyle();
	bool SetStateSong(int32_t pv, int32_t difficulty, int32_t edition, int32_t style);
	void ResetStateSong();
}
