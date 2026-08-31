#pragma once

#include <array>

struct PVGameData;
struct TargetStateEx;

struct ScoreState
{
	float target_max_rate = 0.0f;
	int32_t max_ct_score_bonus = 0;
	int32_t max_sustain_bonus = 0;
	int32_t max_double_tap_bonus = 0;
	int32_t max_link_bonus = 0;

	int32_t ct_score_bonus = 0;
	int32_t double_tap_bonus = 0;
	int32_t sustain_bonus = 0;
	int32_t link_bonus = 0;
	int32_t rush_bonus = 0;
};

namespace score
{
	constexpr int32_t GetChanceTimeSuccessBonus() { return 50000; }
	constexpr int32_t GetTechZoneSuccessBonus() { return 5000; }

	int32_t GetChanceTimeScoreBonus(int32_t hit_state);
	float GetTechZoneRetainedRate();
	int32_t CalculateHitScoreBonus(TargetStateEx* target, int32_t* disp);
	int32_t CalculateSustainBonus(TargetStateEx* target);
	int32_t CalculateMaxSustainBonus(TargetStateEx* target);
	int32_t IncreaseRushPopCount(TargetStateEx* target);
	float CalculatePercentage(PVGameData* pv_game);
	void CalculateScoreReference(int32_t style, ScoreState* ref, PVGameData* pv_game);
}
