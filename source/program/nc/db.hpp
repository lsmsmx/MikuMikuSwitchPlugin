#pragma once

#include <array>
#include <string>
#include <optional>
#include <stdint.h>
#include "shared.hpp"

constexpr size_t MaxChartsPerDifficulty = 3;
constexpr size_t MaxDifficultyCount = 5;
constexpr size_t MaxEditionCount = 2;
constexpr const char* DefaultStarSound = "scratch1_mmv";
constexpr const char* DefaultCopySound = "(TARGET)";

namespace db
{
	struct ChartAttributes
	{
		bool has_hold   = false;
		bool has_multi  = false;
		bool has_slide  = false;
		bool has_double = false;
		bool has_long   = false;
		bool has_rush   = false;
		bool has_star   = false;
	};

	struct ChartEntry
	{
		int32_t style = GameStyle_Max;
		int32_t difficulty_level = 0;
		ChartAttributes attributes = { };
		std::string script_file_name = "(NULL)";
	};

	struct DifficultyEntry
	{
		std::vector<ChartEntry> charts;

		DifficultyEntry() { charts.reserve(MaxChartsPerDifficulty); }

		ChartEntry& FindOrCreateChart(int32_t style);
	};

	struct SongEntry
	{
		std::string star_se_name      = DefaultStarSound;
		std::string double_se_name    = DefaultCopySound;
		std::string long_se_name      = DefaultCopySound;
		std::string star_long_se_name = DefaultStarSound;
		std::string star_w_se_name    = DefaultStarSound;
		std::string link_se_name      = DefaultStarSound;
		uint32_t target_hit_effect_aetset_id = 0xFFFFFFFF;
		uint32_t target_hit_effect_scene_id = 0xFFFFFFFF;
		uint32_t target_hit_effect_sprset_id = 0xFFFFFFFF;
		std::array<std::optional<DifficultyEntry>, MaxDifficultyCount * MaxEditionCount> difficulties;

		ChartEntry& FindOrCreateChart(int32_t difficulty, int32_t edition, int32_t style);
		const ChartEntry* FindChart(int32_t difficulty, int32_t edition, int32_t style) const;

		inline bool IsHitEffectsValid() const
		{
			return target_hit_effect_aetset_id != 0xFFFFFFFF &&
				target_hit_effect_scene_id != 0xFFFFFFFF &&
				target_hit_effect_sprset_id != 0xFFFFFFFF;
		}
	};

	extern "C" {
		const SongEntry* FindSongEntry(int32_t pv);
		const  DifficultyEntry* FindDifficultyEntry(int32_t pv, int32_t difficulty, int32_t edition);
		const  ChartEntry* FindChart(int32_t pv, int32_t difficulty, int32_t edition, int32_t style);
		bool DbReady();
	}
}

void InstallDatabaseHooks();

namespace db {
	inline void init() {
		InstallDatabaseHooks();
	}
}
