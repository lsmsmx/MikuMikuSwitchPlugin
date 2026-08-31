#include <stdint.h>
#include <cstddef>
#include "lib.hpp"
#include "../diva_nc.hpp"
#include "../save_data.hpp"
#include "pv_sel.hpp"
#include "logger.hpp"

struct PVSelectorSwitch
{
	union {
		struct {
			INSERT_PADDING(0x68);
			int32_t state;
		};
		struct {
			INSERT_PADDING(0x2588B);
			bool is_ranking_mode;           // 0x2588B
			int32_t dword2592C;             // 0x2588C
			pvsel::SelPvList sel_pv_list;   // 0x25890
		};
		struct {
			INSERT_PADDING(0x25DD0);
			libcxx_vector_base<pvsel::PvData> sorted_pv_lists[32]; // 0x25DD0
			libcxx_vector_base<pvsel::PvData>* cur_pv_list;        // 0x260D0
			pvsel::PvData random_pv;                               // 0x260D8
		};
		struct {
			INSERT_PADDING(0x26118);
			int32_t* cur_sort_index;        // 0x26118
			int32_t song_counts[21];        // 0x26120
			int32_t all_sort_index;         // 0x26174
			int32_t dword262A0;             // 0x26178
			int32_t pv_id;                  // 0x2617C
			int32_t dword262A8;             // 0x26180
			int32_t sort_indices[4];        // 0x26184
			int32_t sort_change_dir;        // 0x26194
			int32_t sort_mode;              // 0x26198
			uint8_t difficulty_max;         // 0x2619C
			uint8_t difficulty_min;         // 0x2619D
			uint8_t _pad_diff[2];           // 0x2619E
			int32_t difficulty;             // 0x261A0
			int32_t edition;                // 0x261A4
		};
	};
};

static_assert(offsetof(PVSelectorSwitch, is_ranking_mode) == 0x2588B, "Offset error: is_ranking_mode");
static_assert(offsetof(PVSelectorSwitch, sel_pv_list) == 0x25890, "Offset error: sel_pv_list");
static_assert(offsetof(PVSelectorSwitch, sorted_pv_lists) == 0x25DD0, "Offset error: sorted_pv_lists");
static_assert(offsetof(PVSelectorSwitch, sort_mode) == 0x26198, "Offset error: sort_mode");
static_assert(offsetof(PVSelectorSwitch, difficulty) == 0x261A0, "Offset error: difficulty");

static bool style_dirty = false;

static void SetGlobalStateSelectedData(const PVSelectorSwitch* sel)
{
	static int32_t pv_id = -1;

	int32_t cur_pv = sel->pv_id;
	if (cur_pv == -2 && sel->random_pv.data2 != nullptr)
		cur_pv = *sel->random_pv.data2->pv_db_entry;

	if (cur_pv != pv_id || style_dirty)
	{
		if (cur_pv != -2)
		{
			state.nc_song_entry.reset();
			state.nc_chart_entry.reset();
		}

		if (int32_t style = pvsel::GetSelectedStyleOrDefault(); style != GameStyle_Arcade)
		{
			if (auto* entry = db::FindSongEntry(cur_pv); entry != nullptr)
			{
				state.nc_song_entry = *entry;
				if (auto* chart = entry->FindChart(sel->difficulty, sel->edition, style); chart != nullptr)
					state.nc_chart_entry = *chart;
			}
		}

		pv_id = cur_pv;
		style_dirty = false;
	}
}

static inline FUNCTION_PTR(bool, __fastcall, IsPlaylistMode, 0x8434e0);
static inline FUNCTION_PTR(void, __fastcall, PVSelectorSwitchChangeSortFilter, 0x883760, PVSelectorSwitch* a1, int32_t a2);
static inline FUNCTION_PTR(bool, __fastcall, CheckSongPertainsGlobal, 0x5d4740,
	const pvsel::PvData2* data2, int32_t param_2, uint32_t param_3, int32_t sort_mode, uint32_t difficulty, int32_t edition);

static inline void PVListSetSelectedIndex(PVSelectorSwitch* sel, int32_t index)
{
	uintptr_t base = reinterpret_cast<uintptr_t>(sel);
	*reinterpret_cast<int32_t*>(base + 0x25960) = index;
	*reinterpret_cast<uint8_t*>(base + 0x25998) = 0;
	*reinterpret_cast<uint8_t*>(base + 0x25999) = 1;
}

static int32_t GetSelectedIndex(PVSelectorSwitch* sel)
{
	if (sel->pv_id == -2 && !sel->is_ranking_mode)
		return static_cast<int32_t>(sel->sel_pv_list.GetSongCount()) - 1;

	return pvsel::GetSelectedPVIndex(sel);
}

static bool CheckSongPertainsWrapper(PVSelectorSwitch* sel, const pvsel::PvData* pv, int32_t param_3, int32_t param_4)
{
	if (!pv || !pv->data2) return false;

	// Guard against null pv_db_entry dereference in CheckSongPertainsGlobal
	if (!pv->data2->pv_db_entry) {
		return false;
	}

	int32_t mode = sel->sort_mode;
	if (mode != 0)
	{
		if (mode == 1) {
			if (param_3 == -1) param_3 = sel->sort_indices[1];
			if (param_4 == -1) param_4 = sel->sort_indices[1];
		}
		else if (mode == 2) {
			if (param_3 == -1) param_3 = sel->sort_indices[2];
			if (param_4 == -1) param_4 = sel->sort_indices[2];
		}
		else if (mode == 3) {
			if (param_3 == -1) param_3 = sel->sort_indices[3];
			if (param_4 == -1) param_4 = sel->sort_indices[3];
		}
	}

	if (param_3 == -1) param_3 = sel->sort_indices[0];
	if (param_4 == -1) param_4 = sel->sort_indices[0];

	return CheckSongPertainsGlobal(pv->data2, param_3, (uint32_t)param_4, mode, (uint32_t)sel->difficulty, sel->edition);
}

static void CalculateAllSongCountSafe(PVSelectorSwitch* sel)
{
	// Reset counts
	for (int i = 0; i < 21; i++) {
		sel->song_counts[i] = 0;
	}

	if (!sel->cur_pv_list) return;

	uintptr_t current = reinterpret_cast<uintptr_t>(sel->cur_pv_list->begin_);
	uintptr_t end = reinterpret_cast<uintptr_t>(sel->cur_pv_list->end_);
	int32_t selected_style = pvsel::GetSelectedStyleOrDefault();

	// Query active folder tab
	int current_folder = sel->sort_indices[0];
	int all_folder = sel->all_sort_index;

	while (current < end) {
		pvsel::PvData* pv = reinterpret_cast<pvsel::PvData*>(current);

		if (pv->data2 && pv->data2->pv_db_entry) {
			if (pvsel::CheckSongHasStyleAvailable(*pv->data2->pv_db_entry, sel->difficulty, sel->edition, selected_style)) {

				// 1. Count for "All Songs"
				if (CheckSongPertainsWrapper(sel, pv, all_folder, all_folder)) {
					sel->song_counts[all_folder]++;
				}

				// 2. Count only for currently active folder to reduce CPU overhead
				if (current_folder != all_folder && current_folder >= 0 && current_folder < 21) {
					if (CheckSongPertainsWrapper(sel, pv, current_folder, current_folder)) {
						sel->song_counts[current_folder]++;
					}
				}
			}
		}
		current += 0x40; // Step to next PvData entry
	}
}

HOOK_DEFINE_TRAMPOLINE(PVSelectorSwitchCreateSortedPVListHook) {
	static bool Callback(PVSelectorSwitch* sel) {
		bool ret = Orig(sel);

		if (!ret || IsPlaylistMode())
			return ret;

		auto song_counts = pvsel::GetSongCountPerStyle(sel);

		if (pvsel::gs_win && pvsel::gs_win->SetAvailableOptions(song_counts))
			style_dirty = true;

		int32_t count = 0;
		for (int32_t style = pvsel::GetSelectedStyleOrDefault(); count < 3; style++)
		{
			if (song_counts[style % 3] > 0)
			{
				auto songs = pvsel::SortWithStyle(sel->sel_pv_list, sel->difficulty, sel->edition, style % 3);

				auto* game_vec = sel->sel_pv_list.pv_data;
				if (game_vec) {
					game_vec->end_ = game_vec->begin_;
					pvsel::PvData** ptr = game_vec->begin_;
					pvsel::PvData** cap = game_vec->cap_;
					for (auto* pv : songs) {
						if (ptr < cap) {
							*ptr = pv;
							ptr++;
						}
					}
					game_vec->end_ = ptr;
				}

				PVListSetSelectedIndex(sel, GetSelectedIndex(sel));

				// Safely recalculate song count
				CalculateAllSongCountSafe(sel);
				return true;
			}
			count++;
		}

		return false;
	}
};

HOOK_DEFINE_TRAMPOLINE(PVSelectorSwitchInitHook) {
    static bool Callback(uint64_t a1) {
        // Load texture assets once
        static bool assets_cached = false;
        if (!assets_cached) {
            pvsel::RequestAssetsLoad();
            assets_cached = true;
        }

        state.nc_song_entry.reset();
        state.nc_chart_entry.reset();

        // Instantiate style window on song select enter
        if (!pvsel::gs_win && !IsPlaylistMode())
        {
            pvsel::gs_win = std::make_unique<pvsel::GSWindow>();
            pvsel::gs_win->SetPreferredStyle(nc::GetSharedData().pv_sel_selected_style);
        }
        return Orig(a1);
    }
};

HOOK_DEFINE_TRAMPOLINE(PVSelectorSwitchCtrlHook) {
	static bool Callback(PVSelectorSwitch* sel) {
		if (sel->state == 0)
		{
			if (!pvsel::CheckAssetsLoaded()) {
				return false;
            }
		}

		bool ret = Orig(sel);

		static int last_state = -1;
		if (last_state != sel->state) {
			last_state = sel->state;
		}

        // State 6 = Main song select UI state
		if (sel->state == 6 && pvsel::gs_win)
		{
			if (pvsel::gs_win->Ctrl())
			{
				PVSelectorSwitchChangeSortFilter(sel, 0);
				style_dirty = true;
			}
			pvsel::SetSongToggleable(sel);
		}

		SetGlobalStateSelectedData(sel);
		nc::GetSharedData().pv_sel_selected_style = pvsel::GetPreferredStyleOrDefault();
		return ret;
	}
};

HOOK_DEFINE_TRAMPOLINE(PVSelectorSwitchDestHook) {
    static bool Callback(uint64_t a1) {
        // Reset style window
        pvsel::gs_win.reset();
        return Orig(a1);
    }
};

HOOK_DEFINE_TRAMPOLINE(PVSelectorSwitchDispHook) {
	static void Callback(uint64_t a1) {
		Orig(a1);
		if (pvsel::gs_win) pvsel::gs_win->Disp();
	}
};

void InstallPvSelSwitchHooks()
{
	PVSelectorSwitchCreateSortedPVListHook::InstallAtOffset(0x8882e0);
	PVSelectorSwitchInitHook::InstallAtOffset(0x882c10);
	PVSelectorSwitchCtrlHook::InstallAtOffset(0x883af0);
	PVSelectorSwitchDestHook::InstallAtOffset(0x8860f0);
	PVSelectorSwitchDispHook::InstallAtOffset(0x886340);
}
