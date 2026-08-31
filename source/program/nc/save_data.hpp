#pragma once

#include <stdint.h>
#include <string>
#include <cstring>
#include <vector>

struct ConfigSet
{
	int8_t button_w_se_id  =  1;
	int8_t button_l_se_id  =  1;
	int8_t star_se_id      =  1;
	int8_t star_w_se_id    =  1;
	int8_t star_l_se_id    =  1;
	int8_t link_se_id      = -1;
	int8_t rush_se_id      = -1;
	int8_t tech_zone_style =  1;
	int32_t id = 0;
	uint8_t reserved[52];

	ConfigSet() { memset(reserved, 0, sizeof(reserved)); }
};

struct SharedData
{
	int32_t pv_sel_selected_style = 0;
	uint8_t stick_control_se = 0; // NOTE: Applies to console and mixed styles; Arcade is forced to slidechime.
	uint8_t _padding1[3]; // NOTE: Do not use this data; May be set from previous versions of the format.
	int32_t stick_sensitivity = 50;
	int32_t sound_prio = 0;
	int32_t tech_zone_style = 1;
	uint8_t reserved[236];
	SharedData()
	{
		memset(_padding1, 0, sizeof(_padding1));
		memset(reserved, 0, sizeof(reserved));
	}
};

static_assert(sizeof(ConfigSet) == 64, "ConfigSet struct size mismatch.");
static_assert(sizeof(SharedData) == 256, "SharedData struct size mismatch.");

namespace nc
{
	void ApplyConfig();
	ConfigSet* FindConfigSet(int32_t id, bool create_if_missing = true);
	void CreateDefaultSaveData();
	SharedData& GetSharedData();
	int32_t GetConfigSetID();
	ConfigSet* GetConfigSet();

	// Direct save/load methods
	void LoadSaveDataNC();
	void SaveSaveDataNC();

	void InstallSaveDataHooks();

	inline void init_save_data() {
		InstallSaveDataHooks();
	}

	int32_t GetCurrentStyleForSave();
	void* GetNCShadowScore(int32_t id, int32_t style, void* base_score);
	void SyncNCShadowScores();
}
