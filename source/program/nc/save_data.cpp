#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <mutex>

#include "lib.hpp"
#include "diva_nc.hpp"
#include "logger.hpp"
#include "nc_state.hpp"
#include "save_data.hpp"

#include <nn/fs.hpp>
#include "Config.hpp"

#define SCORE_KEY(id, st) (id | (static_cast<uint64_t>(st) << 32))
#define NC_SAVE_PATH "ExlSD:/MikuMikuSwitchPlugin/Save/NewClassics.dat"

constexpr int32_t MaxHeaderSize = 128;
constexpr uint8_t CurrentFileVersion[2] = { 1, 3 };

struct DifficultyScore
{
	uint8_t _data[288];
};

static std::unordered_map<int32_t, ConfigSet> config_sets;
static std::unordered_map<uint64_t, std::array<DifficultyScore, 10>> scores;
static SharedData shared_data;

extern std::recursive_mutex g_SaveMtx;

namespace nc
{

	ConfigSet* FindConfigSet(int32_t id, bool create_if_missing)
	{
		if (auto it = config_sets.find(id); it != config_sets.end())
			return &it->second;
		else if (create_if_missing)
		{
			config_sets[id] = { };
			return &config_sets[id];
		}

		return nullptr;
	}

	static DifficultyScore* FindOrCreateDifficultyScore(int32_t pv, int32_t difficulty, int32_t edition, int32_t style)
	{
		if (difficulty < 0 || difficulty > 5 || pv < 0)
			return nullptr;

		std::scoped_lock lock(g_SaveMtx);
		uint64_t key = SCORE_KEY(pv, style);
		if (auto it = scores.find(key); it != scores.end())
			return &it->second[difficulty + (edition != 0 ? 5 : 0)];

		auto& score = scores[key];
		for (size_t i = 0; i < 10; i++)
			std::memset(&score[i], 0, sizeof(DifficultyScore));

		return &score[difficulty + (edition != 0 ? 5 : 0)];
	}

	void CreateDefaultSaveData()
	{
		config_sets[-1] = { };
		config_sets[-2] = { };
		config_sets[-3] = { };
	}

	SharedData& GetSharedData()
	{
		return shared_data;
	}

	int32_t GetConfigSetID()
	{
		int32_t set = *reinterpret_cast<int32_t*>(game::GetSaveData() + 0x151E08); // switch specific
		return set < 3 ? -(set + 1) : game::GetGlobalPvID();
	}

	ConfigSet* GetConfigSet() { return FindConfigSet(GetConfigSetID(), true); }
}

struct SaveDataFile
{
	struct Header
	{
		char signature[8];
		uint8_t version_major;
		uint8_t version_minor;
		uint16_t flags;
		int32_t header_size;
		size_t config_set_count;
		size_t score_ex_count;
		size_t shared_data_count;

		Header()
		{
			std::memcpy(signature, "NCSAVFIL", 8);
			version_major = CurrentFileVersion[0];
			version_minor = CurrentFileVersion[1];
			flags = 0;
			header_size = MaxHeaderSize;
			config_set_count = 0;
			score_ex_count = 0;
			shared_data_count = 0;
		}
	} header;

	struct ScoreEx
	{
		int32_t pv;
		int32_t style;
		DifficultyScore difficulties[10];
	};

	static_assert(sizeof(Header) <= MaxHeaderSize);
	char _header_padding[MaxHeaderSize - sizeof(Header)];

	SaveDataFile()
	{
		std::memset(_header_padding, 0, sizeof(_header_padding));
	}

	inline bool IsValid() const
	{
		return std::memcmp(header.signature, "NCSAVFIL", 8) == 0;
	}

	inline bool IsVersionUnsupported() const
	{
		return header.version_major > CurrentFileVersion[0] ||
			(header.version_major == CurrentFileVersion[0] && header.version_minor > CurrentFileVersion[1]);
	}

	inline const ConfigSet* GetConfigSets() const
	{
		return reinterpret_cast<const ConfigSet*>(reinterpret_cast<const uint8_t*>(this) + MaxHeaderSize);
	}

	inline const ScoreEx* GetScores() const
	{
		return reinterpret_cast<const ScoreEx*>(GetConfigSets() + header.config_set_count);
	}

	inline const SharedData* GetSharedData() const
	{
		return reinterpret_cast<const SharedData*>(GetScores() + header.score_ex_count);
	}
};

void nc::LoadSaveDataNC() {
	std::scoped_lock lock(g_SaveMtx);
	nn::fs::FileHandle h;

	if (R_SUCCEEDED(nn::fs::OpenFile(&h, NC_SAVE_PATH, nn::fs::OpenMode_Read))) {
		int64_t buffer_size = 0;
		nn::fs::GetFileSize(&buffer_size, h);

		if (buffer_size > 0) {
			std::vector<uint8_t> buffer(buffer_size);
			nn::fs::ReadFile(h, 0, buffer.data(), buffer_size);

			const SaveDataFile* data = reinterpret_cast<const SaveDataFile*>(buffer.data());
			if (data->IsValid() && !data->IsVersionUnsupported()) {
				for (size_t i = 0; i < data->header.config_set_count; i++) {
					const ConfigSet& set = data->GetConfigSets()[i];
					config_sets[set.id] = set;
				}

				for (size_t i = 0; i < data->header.score_ex_count; i++) {
					const SaveDataFile::ScoreEx& score = data->GetScores()[i];
					auto& score_new = scores[SCORE_KEY(score.pv, score.style)];
					std::memcpy(score_new.data(), score.difficulties, sizeof(DifficultyScore) * 10);
				}

				if (data->header.shared_data_count > 0)
					shared_data = *data->GetSharedData();

				if (data->header.version_major == 1 && data->header.version_minor == 3) {
					if (data->GetSharedData()->sound_prio == 1)
						shared_data.sound_prio = 0;
				}
			}
		}
		nn::fs::CloseFile(h);
	}
}

void nc::SaveSaveDataNC() {
	std::scoped_lock lock(g_SaveMtx);
	if (config_sets.empty() && scores.empty())
		return;

	SaveDataFile::Header header;
	header.config_set_count = config_sets.size();
	header.score_ex_count = scores.size();
	header.shared_data_count = 1;

	size_t total_size = MaxHeaderSize +
		config_sets.size() * sizeof(ConfigSet) +
		scores.size() * sizeof(SaveDataFile::ScoreEx) +
		sizeof(SharedData);

	std::vector<uint8_t> buffer(total_size);

	size_t pos = 0;
	auto writeData = [&](const void* src, size_t size) {
		if (pos + size <= buffer.size()) {
			std::memcpy(&buffer[pos], src, size);
			pos += size;
		}
	};

	writeData(&header, sizeof(SaveDataFile::Header));
	pos = MaxHeaderSize;

	for (const auto& [id, set] : config_sets) {
		ConfigSet setw = set;
		setw.id = id;
		writeData(&setw, sizeof(ConfigSet));
	}

	for (const auto& [key, value] : scores) {
		int32_t pv = key & 0xFFFFFFFF;
		int32_t style = key >> 32;
		writeData(&pv, sizeof(int32_t));
		writeData(&style, sizeof(int32_t));
		writeData(value.data(), sizeof(DifficultyScore) * 10);
	}

	writeData(&shared_data, sizeof(SharedData));

	nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin");
	nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin/Save");

	nn::fs::DeleteFile(NC_SAVE_PATH);
	nn::fs::CreateFile(NC_SAVE_PATH, total_size);

	nn::fs::FileHandle h;
	if (R_SUCCEEDED(nn::fs::OpenFile(&h, NC_SAVE_PATH, nn::fs::OpenMode_Write))) {
		nn::fs::WriteFile(h, 0, buffer.data(), total_size, nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
		nn::fs::CloseFile(h);
	}
}

// Fast shadow score cache
struct ShadowContext {
	void* base_ptr = nullptr;
	std::vector<uint8_t> buffer;
};

static std::unordered_map<uint64_t, ShadowContext> g_ncShadowMap;

int32_t nc::GetCurrentStyleForSave() {
	if (state.nc_chart_entry.has_value()) {
		return state.nc_chart_entry.value().style;
	}
	return nc::GetSharedData().pv_sel_selected_style;
}

void* nc::GetNCShadowScore(int32_t id, int32_t style, void* base_score) {
	if (!base_score) return nullptr;

	std::scoped_lock lock(g_SaveMtx);
	uint64_t key = SCORE_KEY(id, style);

	auto& ctx = g_ncShadowMap[key];
	ctx.base_ptr = base_score;

	if (ctx.buffer.empty()) {
		ctx.buffer.resize(0x11F4, 0);

		// Base score copy
		std::memcpy(ctx.buffer.data(), base_score, 0x11F4);

		// Load NewClassics score entries
		for (int diff = 0; diff < 10; diff++) {
			int edition = diff >= 5 ? 1 : 0;
			int d = diff % 5;
			if (auto* nc_diff = FindOrCreateDifficultyScore(id, d, edition, style)) {
				std::memcpy(ctx.buffer.data() + 4 + (diff * 288), nc_diff, 288);
			}
		}
	}

	return ctx.buffer.data();
}

void nc::SyncNCShadowScores() {
	std::scoped_lock lock(g_SaveMtx);
	constexpr size_t mod_offset = 0xB44;
	constexpr size_t mod_size = 0x11F4 - mod_offset;

	int32_t current_style = nc::GetCurrentStyleForSave();

	for (auto& [key, ctx] : g_ncShadowMap) {
		if (ctx.buffer.empty()) continue;

		int32_t id = key & 0xFFFFFFFF;
		int32_t style = key >> 32;
		const uint8_t* score_data = ctx.buffer.data();

		// 1. Save NC score records
		for (int diff = 0; diff < 10; diff++) {
			int edition = diff >= 5 ? 1 : 0;
			int d = diff % 5;
			if (auto* nc_diff = FindOrCreateDifficultyScore(id, d, edition, style)) {
				std::memcpy(nc_diff, score_data + 4 + (diff * 288), 288);
			}
		}

		// 2. Sync module customization between Console and Arcade
		if (ctx.base_ptr != nullptr) {
			uint8_t* shadow_mods = ctx.buffer.data() + mod_offset;
			uint8_t* base_mods = reinterpret_cast<uint8_t*>(ctx.base_ptr) + mod_offset;

			if (current_style == 0) {
				std::memcpy(shadow_mods, base_mods, mod_size);
			} else if (current_style == style) {
				std::memcpy(base_mods, shadow_mods, mod_size);
			}
		}
	}
}

void nc::InstallSaveDataHooks()
{
	// Hook 0xcac90 disabled: GetNCShadowScore overrides buffer directly
}
