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
#include "../SaveStream.hpp"
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

	bool SongHasStyle(int32_t pv, int32_t style)
	{
		// Arcade is the engine's own score table (mirrored in DivaModData.dat), so
		// it never needs shadow storage, and Max is not a style at all.
		if (style <= GameStyle_Arcade || style >= GameStyle_Max)
			return false;

		const db::SongEntry* song = db::FindSongEntry(pv);
		if (song == nullptr)
			return false;

		for (size_t edition = 0; edition < MaxEditionCount; edition++)
			for (size_t difficulty = 0; difficulty < MaxDifficultyCount; difficulty++)
				if (song->FindChart(static_cast<int32_t>(difficulty), static_cast<int32_t>(edition), style) != nullptr)
					return true;

		return false;
	}
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

// The whole header block is read/written as one unit, so it has to be exactly
// MaxHeaderSize bytes on disk.
static_assert(sizeof(SaveDataFile) == MaxHeaderSize, "SaveDataFile header block size mismatch.");

// Ten zeroed difficulty slots carry nothing: FindOrCreateDifficultyScore builds
// an empty entry on demand, so a record that is absent reads back exactly like
// one that is present and zero.
static bool IsEmptyRecord(const std::array<DifficultyScore, 10>& record)
{
	for (const DifficultyScore& diff : record)
		for (uint8_t byte : diff._data)
			if (byte != 0)
				return false;

	return true;
}

// Storage that can never be read back. Records for a PV the DB does not know are
// *kept*: a mod that is disabled or missing this session must not cost the player
// its console/mixed data, so only a PV the DB knows -- without a chart in that
// style -- counts as junk.
static bool IsJunkRecord(int32_t pv, int32_t style, const std::array<DifficultyScore, 10>& record)
{
	if (IsEmptyRecord(record))
		return true;

	if (!db::DbReady())
		return false;

	if (db::FindSongEntry(pv) == nullptr)
		return false;

	return !nc::SongHasStyle(pv, style);
}

void nc::LoadSaveDataNC() {
	std::scoped_lock lock(g_SaveMtx);

	savestream::Reader reader;
	if (!reader.Open(NC_SAVE_PATH))
		return;

	if (reader.Size() < static_cast<int64_t>(MaxHeaderSize)) {
		reader.Close();
		return;
	}

	SaveDataFile data;
	if (!reader.Read(&data, sizeof(SaveDataFile))) {
		reader.Close();
		return;
	}

	const SaveDataFile::Header& header = data.header;

	if (data.IsValid() && !data.IsVersionUnsupported()) {
		for (size_t i = 0; i < header.config_set_count; i++) {
			ConfigSet set;
			if (!reader.Read(&set, sizeof(ConfigSet)))
				break;

			config_sets[set.id] = set;
		}

		for (size_t i = 0; i < header.score_ex_count; i++) {
			int32_t pv = 0;
			int32_t style = 0;
			if (!reader.Read(&pv, sizeof(pv)) || !reader.Read(&style, sizeof(style)))
				break;

			const uint64_t key = SCORE_KEY(pv, style);
			auto& score_new = scores[key];
			if (!reader.Read(score_new.data(), sizeof(DifficultyScore) * 10))
				break;

			// Junk is dropped here rather than kept in RAM until the next save.
			if (IsJunkRecord(pv, style, score_new))
				scores.erase(key);
		}

		if (header.shared_data_count > 0) {
			SharedData shared;
			if (reader.Read(&shared, sizeof(SharedData))) {
				shared_data = shared;

				if (header.version_major == 1 && header.version_minor == 3 && shared_data.sound_prio == 1)
					shared_data.sound_prio = 0;
			}
		}
	}

	reader.Close();
}

void nc::SaveSaveDataNC() {
	std::scoped_lock lock(g_SaveMtx);
	if (config_sets.empty() && scores.empty())
		return;

	// Drop storage that can never be read back, before the size is computed:
	// empty slots, and records for a PV the DB knows but which has no chart in
	// that style. This is what keeps the file to the PVs that really have
	// console/mixed charts instead of one record per PV per style.
	for (auto it = scores.begin(); it != scores.end(); )
	{
		const int32_t pv = static_cast<int32_t>(it->first & 0xFFFFFFFF);
		const int32_t style = static_cast<int32_t>(it->first >> 32);

		if (IsJunkRecord(pv, style, it->second))
			it = scores.erase(it);
		else
			++it;
	}

	if (config_sets.empty() && scores.empty())
	{
		// Nothing worth keeping; do not leave the old file behind.
		nn::fs::DeleteFile(NC_SAVE_PATH);
		return;
	}

	SaveDataFile::Header header;
	header.config_set_count = config_sets.size();
	header.score_ex_count = scores.size();
	header.shared_data_count = 1;

	const size_t total_size = MaxHeaderSize +
		config_sets.size() * sizeof(ConfigSet) +
		scores.size() * sizeof(SaveDataFile::ScoreEx) +
		sizeof(SharedData);

	nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin");
	nn::fs::CreateDirectory("ExlSD:/MikuMikuSwitchPlugin/Save");

	nn::fs::DeleteFile(NC_SAVE_PATH);
	nn::fs::CreateFile(NC_SAVE_PATH, total_size);

	savestream::Writer writer;
	if (!writer.Open(NC_SAVE_PATH, total_size))
		return;

	// Header struct followed by zero padding up to MaxHeaderSize.
	static const uint8_t zero_header[MaxHeaderSize - sizeof(SaveDataFile::Header)] = { };
	writer.Write(&header, sizeof(SaveDataFile::Header));
	writer.Write(zero_header, sizeof(zero_header));

	for (const auto& [id, set] : config_sets) {
		ConfigSet setw = set;
		setw.id = id;
		writer.Write(&setw, sizeof(ConfigSet));
	}

	for (const auto& [key, value] : scores) {
		const int32_t pv = static_cast<int32_t>(key & 0xFFFFFFFF);
		const int32_t style = static_cast<int32_t>(key >> 32);
		writer.Write(&pv, sizeof(pv));
		writer.Write(&style, sizeof(style));
		writer.Write(value.data(), sizeof(DifficultyScore) * 10);
	}

	writer.Write(&shared_data, sizeof(SharedData));

	writer.PadToEnd();
	writer.Close();
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

void* nc::GetScoreForCurrentStyle(int32_t pv, void* base_score) {
	const int32_t style = GetCurrentStyleForSave();

	// Console/Mixed only, and only for a PV that has a chart in that style:
	// anything else belongs to the engine's own score table.
	if (style != GameStyle_Arcade && SongHasStyle(pv, style))
		return GetNCShadowScore(pv, style, base_score);

	return base_score;
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
/*
void nc::InstallSaveDataHooks()
{
	// Hook 0xcac90 disabled: GetNCShadowScore overrides buffer directly
}*/
