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
            bool is_ranking_mode;
            int32_t dword2592C;
            pvsel::SelPvList sel_pv_list;
        };
        struct {
            INSERT_PADDING(0x25DD0);
            libcxx_vector_base<pvsel::PvData> sorted_pv_lists[32];
            libcxx_vector_base<pvsel::PvData>* cur_pv_list;
            pvsel::PvData random_pv;
        };
        struct {
            INSERT_PADDING(0x26118);
            int32_t* cur_sort_index;        // 0x26118
            int32_t song_counts[21];        // 0x26120 - Category song counters
            int32_t all_sort_index;         // 0x26174 - Index of "All Songs" folder
            int32_t dword262A0;             // 0x26178
            int32_t pv_id;                  // 0x2617C
            int32_t dword262A8;             // 0x26180
            int32_t sort_indices[4];        // 0x26184 - Current folder index per sort mode
            int32_t sort_change_dir;        // 0x26194
            int32_t sort_mode;              // 0x26198 - Current sort mode (0: ABC, 1: Diff, 2: Voc, 3: Rank)
            uint8_t difficulty_max;         // 0x2619C
            uint8_t difficulty_min;         // 0x2619D
            uint8_t _pad_diff[2];           // 0x2619E
            int32_t difficulty;             // 0x261A0
            int32_t edition;                // 0x261A4
        };
    };
};

static_assert(offsetof(PVSelectorSwitch, sort_mode) == 0x26198, "Offset error: sort_mode");
static_assert(offsetof(PVSelectorSwitch, song_counts) == 0x26120, "Offset error: song_counts");
static_assert(offsetof(PVSelectorSwitch, sort_indices) == 0x26184, "Offset error: sort_indices");

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
    const void* data2, int32_t folder_idx, uint32_t folder_idx_dup, int32_t sort_mode, uint32_t difficulty, int32_t edition);

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

// Checks if folder_idx corresponds to the Favorites folder in the given sort mode
static inline bool IsFavoriteFolder(int32_t sort_mode, int32_t folder_idx)
{
    switch (sort_mode) {
        case 0: return folder_idx == 11; // Mode 0 (ABC): 11 is Favorites
        case 1: return folder_idx == 20; // Mode 1 (Difficulty): 20 is Favorites
        case 2: return folder_idx == 7;  // Mode 2 (Vocaloid): 7 is Favorites
        case 3: return folder_idx == 6;  // Mode 3 (Score Rank): 6 is Favorites
        default: return false;
    }
}

static bool CheckSongPertainsWrapper(PVSelectorSwitch* sel, const pvsel::PvData* pv, int32_t folder_idx)
{
    if (!pv || !pv->data2 || !pv->data2->pv_db_entry)
        return false;

    // CheckSongPertainsGlobal expects param_1 such that *(int*)*param_1 == pv_id.
    // When checking Favorites, it solely relies on pv_id to query FindScore.
    // If pv->data2 layout has padding before pv_db_entry, passing pv->data2 directly
    // causes FindScore to receive garbage. We provide a guaranteed pointer layout here:
    if (IsFavoriteFolder(sel->sort_mode, folder_idx))
    {
        // Construct a pointer whose first 8 bytes point directly to pv_db_entry
        int32_t* pvid_ptr = pv->data2->pv_db_entry;
        const void* fake_param_1 = &pvid_ptr;

        return CheckSongPertainsGlobal(
            fake_param_1,
            folder_idx,
            (uint32_t)folder_idx,
            sel->sort_mode,
            (uint32_t)sel->difficulty,
            sel->edition
        );
    }

    // For standard categories (Letters, Difficulties, Vocaloids), pass pv->data2 as original code does
    return CheckSongPertainsGlobal(
        pv->data2,
        folder_idx,
        (uint32_t)folder_idx,
        sel->sort_mode,
        (uint32_t)sel->difficulty,
        sel->edition
    );
}

static void CalculateAllSongCountSafe(PVSelectorSwitch* sel)
{
    // Reset all category counters
    for (int i = 0; i < 21; i++) {
        sel->song_counts[i] = 0;
    }

    if (!sel->cur_pv_list || !sel->cur_pv_list->begin_)
        return;

    uintptr_t current = reinterpret_cast<uintptr_t>(sel->cur_pv_list->begin_);
    uintptr_t end = reinterpret_cast<uintptr_t>(sel->cur_pv_list->end_);
    int32_t selected_style = pvsel::GetSelectedStyleOrDefault();

    while (current < end) {
        pvsel::PvData* pv = reinterpret_cast<pvsel::PvData*>(current);

        if (pv->data2 && pv->data2->pv_db_entry) {
            if (pvsel::CheckSongHasStyleAvailable(*pv->data2->pv_db_entry, sel->difficulty, sel->edition, selected_style)) {
                // Check every folder (0 to 20) without early break.
                // A song belongs to a letter/vocaloid AND can simultaneously be a Favorite.
                for (int folder = 0; folder < 21; folder++) {
                    if (CheckSongPertainsWrapper(sel, pv, folder)) {
                        sel->song_counts[folder]++;
                    }
                }
            }
        }
        current += 0x40; // Step size for Switch version
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
        static bool assets_cached = false;
        if (!assets_cached) {
            pvsel::RequestAssetsLoad();
            assets_cached = true;
        }
        state.nc_song_entry.reset();
        state.nc_chart_entry.reset();
        if (!pvsel::gs_win && !IsPlaylistMode()) {
            pvsel::gs_win = std::make_unique<pvsel::GSWindow>();
            pvsel::gs_win->SetPreferredStyle(nc::GetSharedData().pv_sel_selected_style);
        }
        return Orig(a1);
    }
};

HOOK_DEFINE_TRAMPOLINE(PVSelectorSwitchCtrlHook) {
    static bool Callback(PVSelectorSwitch* sel) {
        if (sel->state == 0) {
            if (!pvsel::CheckAssetsLoaded())
                return false;
        }
        bool ret = Orig(sel);
        if (sel->state == 6 && pvsel::gs_win) {
            if (pvsel::gs_win->Ctrl()) {
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
        pvsel::gs_win.reset();
        return Orig(a1);
    }
};

HOOK_DEFINE_TRAMPOLINE(PVSelectorSwitchDispHook) {
    static void Callback(uint64_t a1) {
        Orig(a1);
        if (pvsel::gs_win)
            pvsel::gs_win->Disp();
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
