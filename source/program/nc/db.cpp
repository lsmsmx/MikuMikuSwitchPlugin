#include <map>
#include <array>
#include <string>
#include <string_view>
#include <cstring>

#include "lib.hpp"
#include "diva_nc.hpp"
#include "logger.hpp"
#include "nc_state.hpp"
#include "util.hpp"
#include "toml.hpp"
#include "db.hpp"
#include "ModLoader.hpp"

constexpr size_t MaxFilePerRom = 10;
constexpr std::array<std::string_view, GameStyle_Max> styles_names_internal = { "ARCADE", "CONSOLE", "MIXED" };
constexpr std::array<std::string_view, 21> pv_lv_names_internal = {
	"PV_LV_00_0", "PV_LV_00_5", "PV_LV_01_0", "PV_LV_01_5",
	"PV_LV_02_0", "PV_LV_02_5", "PV_LV_03_0", "PV_LV_03_5",
	"PV_LV_04_0", "PV_LV_04_5", "PV_LV_05_0", "PV_LV_05_5",
	"PV_LV_06_0", "PV_LV_06_5", "PV_LV_07_0", "PV_LV_07_5",
	"PV_LV_08_0", "PV_LV_08_5", "PV_LV_09_0", "PV_LV_09_5",
	"PV_LV_10_0"
};

struct ChartDatabase
{
	std::map<int32_t, db::SongEntry> entries;
	FileHandler file_handler = nullptr;
	int32_t state = 0;
	size_t rom_dir_index = 0;
	bool ready = false;
} static nc_db;

static bool TryFindNextNCDBFile(std::string* output_path)
{
	uintptr_t base_addr = exl::util::GetMainModuleInfo().m_Total.m_Start;

	auto* rom_dirs = *reinterpret_cast<libcxx_vector**>(base_addr + 0x00CDF7C0);
	if (!rom_dirs) {
        return false;
    }

	auto FileCheckExists_Switch = (bool(*)(libcxx_string*, libcxx_string*))(base_addr + 0x1f4480);

	bool found = false;
	while (!found)
	{
		if (nc_db.rom_dir_index >= rom_dirs->size()) {
			return false;
        }

		libcxx_string& root_cxx = rom_dirs->begin_[nc_db.rom_dir_index];
		std::string root = root_cxx.c_str() ? root_cxx.c_str() : "";

		if (root.size() >= 2 && root[0] == '.' && root[1] == '/')
		{
			nc_db.rom_dir_index++;
			continue;
		}

		std::string full_path = root + "/rom/nc_db.toml";

		libcxx_string cxx_file_path;
		cxx_file_path = full_path;

		if (FileCheckExists_Switch(&cxx_file_path, nullptr))
		{
			*output_path = full_path;
			found = true;
		}

		nc_db.rom_dir_index++;
	}

	return true;
}

static bool ParseDifficultyEntry(toml::array& node, db::DifficultyEntry& entry)
{
	size_t length = node.size() > MaxChartsPerDifficulty ? MaxChartsPerDifficulty : node.size();
	for (size_t i = 0; i < length; i++)
	{
		if (!node[i].is_table()) continue;

		toml::table& table = *node[i].as_table();
		std::string style = table["style"].value_or("ARCADE");
		std::string pv_level = table["level"].value_or("PV_LV_00_0");

		db::ChartEntry& chart = entry.FindOrCreateChart(util::GetIndex(styles_names_internal, style, GameStyle_Max));
		chart.difficulty_level = util::GetIndex(pv_lv_names_internal, pv_level, 0);
		chart.script_file_name = table["script_file_name"].value_or("(NULL)");
	}
	return true;
}

static bool ParsePVEntry(toml::table& node, db::SongEntry* entry)
{
	const char* difficulty_names[MaxDifficultyCount] = { "easy", "normal", "hard", "extreme", "encore" };
	for (size_t i = 0; i < MaxDifficultyCount; i++)
	{
		for (size_t ed = 0; ed < 2; ed++)
		{
			std::string name = util::Format("%s%s", ed > 0 ? "ex_" : "", difficulty_names[i]);
			if (toml::array* diff = node[name].as_array(); diff != nullptr)
			{
				auto& difficulty_entry = entry->difficulties[i + MaxDifficultyCount * ed];
				if (difficulty_entry.has_value())
				{
					difficulty_entry.value().charts.clear();
					ParseDifficultyEntry(*diff, difficulty_entry.value());
				}
			}
		}
	}

	entry->star_se_name = node["star_se_name"].value_or(DefaultStarSound);
	entry->double_se_name = node["double_se_name"].value_or(DefaultCopySound);
	entry->long_se_name = node["long_se_name"].value_or(DefaultCopySound);
	entry->star_w_se_name = node["star_w_se_name"].value_or(DefaultStarSound);
	entry->star_long_se_name = node["star_long_se_name"].value_or(DefaultStarSound);
	entry->link_se_name = node["link_se_name"].value_or(DefaultStarSound);

	entry->target_hit_effect_aetset_id = node["target_hit_effect_aetset_id"].value_or(0xFFFFFFFF);
	entry->target_hit_effect_scene_id  = node["target_hit_effect_scene_id"].value_or(0xFFFFFFFF);
	entry->target_hit_effect_sprset_id = node["target_hit_effect_sprset_id"].value_or(0xFFFFFFFF);

	return true;
}

static int32_t ParseNCDB(const void* data, size_t size)
{
    if (!data) {
        return 0;
    }

	if (auto result = toml::parse(std::string_view(reinterpret_cast<const char*>(data), size)); result.succeeded())
	{
		int32_t new_count = 0;
		toml::table& root = result.table();
		if (toml::array* songs = root["songs"].as_array(); songs != nullptr)
		{
			for (auto& node : *songs)
			{
				if (!node.is_table()) continue;

				int32_t id = node.as_table()->at("id").value_or(-1);
				if (id > 0)
				{
					if (ParsePVEntry(*node.as_table(), &nc_db.entries[id]))
						new_count++;
				}
			}
		}
		return new_count;
	}

	return 0;
}

struct LibcxxVectorRaw {
	pv_db::PvDBDifficulty* start;
	pv_db::PvDBDifficulty* finish;
	pv_db::PvDBDifficulty* end_of_storage;
};

// 1. TaskPvDBParseEntry Hook
HOOK_DEFINE_TRAMPOLINE(TaskPvDBParseEntryHook) {
	static bool Callback(uint64_t a1, pv_db::PvDBEntry* entry, uint64_t a3, uint32_t id, uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8) {
		bool ret = Orig(a1, entry, a3, id, a5, a6, a7, a8);
		if (ret)
		{
			if (nc_db.ready)
				return true;

            if (!entry) {
                return ret;
            }

			db::SongEntry& nc_song = nc_db.entries[id];
			LibcxxVectorRaw* difficulties_array = reinterpret_cast<LibcxxVectorRaw*>(&entry->difficulties[0]);

			for (int32_t i = 0; i < 5; i++)
			{
				LibcxxVectorRaw& vec = difficulties_array[i];

				if (vec.start != nullptr && vec.finish != nullptr && vec.finish >= vec.start) {
					size_t count = vec.finish - vec.start;

                    if (count > 20) {
                        continue;
                    }

					for (size_t idx = 0; idx < count; idx++) {
						pv_db::PvDBDifficulty& diff = vec.start[idx];

						if (diff.edition != 0 && diff.edition != 1)
							continue;

						db::ChartEntry& chart = nc_song.FindOrCreateChart(diff.difficulty, diff.edition, GameStyle_Arcade);
						chart.difficulty_level = diff.level;
					}
				}
			}
		}
		return ret;
	}
};

namespace nc {
    void OnPvDbRead(uint64_t task) {
        int32_t pv_db_state = *reinterpret_cast<int32_t*>(task + 100);

        if (pv_db_state != 0 || nc_db.ready) {
            return;
        }

        std::string path = "";

        switch (nc_db.state) {
            case 0:
                if (TryFindNextNCDBFile(&path)) {
                    if (!path.empty()) {
                        FileRequestLoad(&nc_db.file_handler, path.c_str(), 1);
                        nc_db.state = 1;
                    }
                } else {
                    nc_db.state = 3;
                }
                break;
            case 1:
                if (!FileCheckNotReady(&nc_db.file_handler)) {
                    nc_db.state = 2;
                }
                break;
            case 2:
                ParseNCDB(FileGetData(&nc_db.file_handler), FileGetSize(&nc_db.file_handler));
                FileFree(&nc_db.file_handler);
                nc_db.state = 0;
                break;
            case 3:
                nc_db.ready = true;
                break;
        }
    }
}

inline FUNCTION_PTR(void*, __fastcall, GetMdataManager, 0x41aeb0, void);
inline FUNCTION_PTR(void*, __fastcall, MdataManager_GetDBPrefixes, 0x41c380, void* a1);

HOOK_DEFINE_TRAMPOLINE(InitMdataPvDbPathsHook) {
	static void Callback() {
		Orig();

		uintptr_t base_addr = exl::util::GetMainModuleInfo().m_Total.m_Start;

		// Dereference global pointer rather than taking its address directly
		uintptr_t* dat_slot = reinterpret_cast<uintptr_t*>(base_addr + 0xce1b90);
		uintptr_t list_base = *dat_slot;

		if (!list_base) {
			return;
		}

		libcxx_list* paths = reinterpret_cast<libcxx_list*>(list_base + 0x68);

		auto FileCheckExists_Switch = (bool(*)(libcxx_string*, libcxx_string*))(base_addr + 0x1f4480);

		void* mgr = GetMdataManager();
		void* list_ptr = MdataManager_GetDBPrefixes(mgr);

		libcxx_list* prefixes = reinterpret_cast<libcxx_list*>(list_ptr);
		libcxx_list_node* curr = prefixes->end_next;
		libcxx_list_node* end_node = reinterpret_cast<libcxx_list_node*>(prefixes);

		while (curr != end_node) {
			std::string prefix_str(curr->value.c_str() ? curr->value.c_str() : "");

			std::string std_db_path = "rom/" + prefix_str + "nc_pv_db.txt";
			std::string std_field_path = "rom/" + prefix_str + "nc_pv_field.txt";

			libcxx_string db_path_cxx;
			db_path_cxx = std_db_path;
			libcxx_string resolved_db_path;
			if (FileCheckExists_Switch(&db_path_cxx, &resolved_db_path)) {
				paths->push_back(resolved_db_path.c_str());
			}

			libcxx_string field_path_cxx;
			field_path_cxx = std_field_path;
			libcxx_string resolved_field_path;
			if (FileCheckExists_Switch(&field_path_cxx, &resolved_field_path)) {
				paths->push_back(resolved_field_path.c_str());
			}

			curr = curr->next;
		}
	}
};

db::ChartEntry& db::DifficultyEntry::FindOrCreateChart(int32_t style)
{
	for (ChartEntry& chart : charts)
		if (chart.style == style)
			return chart;

	ChartEntry& chart = charts.emplace_back();
	chart.style = style;
	return chart;
}

db::ChartEntry& db::SongEntry::FindOrCreateChart(int32_t difficulty, int32_t edition, int32_t style)
{
	int32_t index = MaxDifficultyCount * edition + difficulty;
	if (index < 0 || index >= static_cast<int32_t>(difficulties.size())) {
		static db::DifficultyEntry dummy;
		return dummy.FindOrCreateChart(style);
	}

	auto& diff = difficulties[index];
	if (!diff.has_value())
		diff = db::DifficultyEntry();

	return diff.value().FindOrCreateChart(style);
}

const db::ChartEntry* db::SongEntry::FindChart(int32_t difficulty, int32_t edition, int32_t style) const
{
	if (difficulty < 0 || difficulty >= static_cast<int32_t>(MaxDifficultyCount) ||
		edition < 0 || edition >= static_cast<int32_t>(MaxEditionCount))
		return nullptr;

	int32_t index = MaxDifficultyCount * edition + difficulty;
	if (index < 0 || index >= static_cast<int32_t>(difficulties.size()))
		return nullptr;

	const auto& diff = difficulties[index];
	if (!diff.has_value())
		return nullptr;

	for (const ChartEntry& chart : diff.value().charts)
		if (chart.style == style)
			return &chart;

	return nullptr;
}

extern "C" {
	const db::SongEntry* db::FindSongEntry(int32_t pv)
	{
		if (auto it = nc_db.entries.find(pv); it != nc_db.entries.end())
			return &it->second;
		return nullptr;
	}

	const db::DifficultyEntry* db::FindDifficultyEntry(int32_t pv, int32_t difficulty, int32_t edition)
	{
		if (difficulty < 0 || difficulty >= static_cast<int32_t>(MaxDifficultyCount) ||
			(edition != 0 && edition != 1))
			return nullptr;

		if (auto* song = FindSongEntry(pv); song != nullptr)
		{
			if (const auto& entry = song->difficulties[MaxDifficultyCount * edition + difficulty]; entry.has_value())
				return &entry.value();
		}

		return nullptr;
	}

	const db::ChartEntry* db::FindChart(int32_t pv, int32_t difficulty, int32_t edition, int32_t style)
	{
		if (style < 0 || style >= GameStyle_Max)
			return nullptr;

		if (auto* entry = FindDifficultyEntry(pv, difficulty, edition); entry != nullptr)
		{
			for (auto& chart : entry->charts)
				if (chart.style == style)
					return &chart;
		}

		return nullptr;
	}

	bool db::DbReady() {
		return nc_db.ready;
	}
}

void InstallDatabaseHooks()
{
	TaskPvDBParseEntryHook::InstallAtOffset(0x4c2d30);
	InitMdataPvDbPathsHook::InstallAtOffset(0x4be140);
}
