#include <stdint.h>
#include <cstddef>
#include "lib.hpp"
#include "../diva_nc.hpp"
#include "../save_data.hpp"
#include "pv_sel.hpp"
#include "logger.hpp"

struct CommonMenu
{
	int32_t selected_index;
	uint8_t gap4[8];
	int32_t max_count;
};

struct PvDataPS4
{
	pvsel::PvData2* data2;
	uint8_t gap8[64];
};

struct PVselPS4
{
	INSERT_PADDING(0x6C);
	int32_t state;
	INSERT_PADDING(0x4FC8);
	CommonMenu cmn_menu;

	INSERT_PADDING(0x2E8B8);
	pvsel::SelPvList sel_pv_list;

	INSERT_PADDING(0x33AA0 - 0x33900 - sizeof(pvsel::SelPvList));
	libcxx_vector_base<PvDataPS4> sorted_pv_lists[26];
	libcxx_vector_base<PvDataPS4>* cur_pv_list;

	INSERT_PADDING(0x18);
	PvDataPS4 random_pv;
	int32_t song_counts[21];

	int32_t all_sort_index;
	int32_t* cur_sort_index;

	INSERT_PADDING(0x4);
	int32_t pv_id;
	int32_t sort_mode;
	int32_t sort_indices[4];
	int32_t sort_change_dir;
	int32_t difficulty;
	int32_t edition;
};

static_assert(offsetof(PVselPS4, sort_mode) == 0x33DE0, "Offset error: sort_mode");
static_assert(offsetof(PVselPS4, difficulty) == 0x33DF8, "Offset error: difficulty");

static bool style_dirty = false;

static inline void PVListSetSelectedIndexPS4(PVselPS4* sel, int32_t index)
{
	uintptr_t base = reinterpret_cast<uintptr_t>(sel);
	*reinterpret_cast<int32_t*>(base + 0x339D0) = index;
	*reinterpret_cast<uint8_t*>(base + 0x33A08) = 0;
	*reinterpret_cast<uint8_t*>(base + 0x33A09) = 1;
}

static void SetGlobalStateSelectedData(const PVselPS4* sel)
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
			if (const auto* entry = db::FindSongEntry(cur_pv); entry != nullptr)
			{
				state.nc_song_entry = *entry;
				if (const auto* chart = entry->FindChart(sel->difficulty, sel->edition, style); chart != nullptr)
					state.nc_chart_entry = *chart;
			}
		}

		pv_id = cur_pv;
		style_dirty = false;
	}
}

static int32_t GetSelectedIndex(PVselPS4* sel)
{
	if (sel->pv_id == -2)
		return sel->sel_pv_list.GetSongCount() - 1;
	return pvsel::GetSelectedPVIndex(sel);
}

static inline FUNCTION_PTR(bool, __fastcall, IsPlaylistMode, 0x148250);
static inline FUNCTION_PTR(bool, __fastcall, IsSurvivalMode, 0x15f230);
static inline FUNCTION_PTR(void, __fastcall, PVselPS4ChangeSortFilter, 0x116cb0, PVselPS4* sel, int32_t dir);
static inline FUNCTION_PTR(void, __fastcall, InitCommonMenuPVList, 0x11c0f0, CommonMenu* cmn, pvsel::SelPvList* pv_list);

// NOTE: Original CheckSongPertainsGlobal function on Switch (6 params)
static inline FUNCTION_PTR(bool, __fastcall, CheckSongPertainsGlobal, 0x5d4740,
	const pvsel::PvData2* data2, int32_t param_2, uint32_t param_3, int32_t sort_mode, uint32_t difficulty, int32_t edition);

// NOTE: Wrapper for PS4 mode struct
static bool CheckSongPertainsWrapperPS4(PVselPS4* sel, const PvDataPS4* pv, int32_t param_3, int32_t param_4)
{
	if (!pv || !pv->data2) return false;

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

HOOK_DEFINE_TRAMPOLINE(PVselPS4CreateSortedPVListHook) {
	static void Callback(PVselPS4* sel) {
		Orig(sel);
		if (IsSurvivalMode() || IsPlaylistMode())
			return;

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

				sel->song_counts[*sel->cur_sort_index] = song_counts[style % 3];
				pvsel::CalculateAllSongCount(sel, CheckSongPertainsWrapperPS4);

				PVListSetSelectedIndexPS4(sel, GetSelectedIndex(sel));
				InitCommonMenuPVList(&sel->cmn_menu, &sel->sel_pv_list);
				return;
			}

			count++;
		}
	}
};

HOOK_DEFINE_TRAMPOLINE(PVselPS4InitHook) {
	static bool Callback(PVselPS4* sel) {
		pvsel::RequestAssetsLoad();
		if (!pvsel::gs_win && !IsPlaylistMode())
		{
			pvsel::gs_win = std::make_unique<pvsel::GSWindow>();
			pvsel::gs_win->SetPreferredStyle(nc::GetSharedData().pv_sel_selected_style);
		}

		state.nc_song_entry.reset();
		state.nc_chart_entry.reset();

		return Orig(sel);
	}
};

HOOK_DEFINE_TRAMPOLINE(PVselPS4CtrlHook) {
	static bool Callback(PVselPS4* sel) {
		if (sel->state == 0)
		{
			if (!pvsel::CheckAssetsLoaded())
				return false;
		}

		bool ret = Orig(sel);

		if (!IsSurvivalMode() && !IsPlaylistMode())
		{
			if (sel->state == 6)
			{
				pvsel::gs_win->SetVisible(true);
				if (pvsel::gs_win->Ctrl())
				{
					PVselPS4ChangeSortFilter(sel, 0);
					style_dirty = true;
				}

				pvsel::SetSongToggleable(sel);
			}

			SetGlobalStateSelectedData(sel);
			nc::GetSharedData().pv_sel_selected_style = pvsel::GetSelectedStyleOrDefault();
		}
		else
		{
			if (pvsel::gs_win)
				pvsel::gs_win->SetVisible(false);
		}

		return ret;
	}
};

HOOK_DEFINE_TRAMPOLINE(PVselPS4DestHook) {
	static bool Callback(uint64_t a1) {
		pvsel::gs_win.reset();
		pvsel::UnloadAssets();
		return Orig(a1);
	}
};

HOOK_DEFINE_TRAMPOLINE(PVselPS4DispHook) {
	static void Callback(uint64_t a1) {
		Orig(a1);
		if (pvsel::gs_win)
			pvsel::gs_win->Disp();
	}
};

void InstallPvSelPS4Hooks()
{
	PVselPS4CreateSortedPVListHook::InstallAtOffset(0x10dee0);
	PVselPS4InitHook::InstallAtOffset(0x10c1e0);
	PVselPS4CtrlHook::InstallAtOffset(0x10e9c0);
	PVselPS4DestHook::InstallAtOffset(0x12c5d8);
	PVselPS4DispHook::InstallAtOffset(0x118050);
}
