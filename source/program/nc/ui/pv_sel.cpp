#include <array>
#include <stdint.h>
#include <stdio.h>
#include "logger.hpp"
#include "../diva_nc.hpp"
#include "../nc_state.hpp"
#include "../db.hpp"
#include "../util.hpp"
#include "pv_sel.hpp"
#include "../../Config.hpp"

constexpr uint32_t AetSelSetID   = 14010050;
constexpr uint32_t AetSelSceneID = 14010051;
constexpr uint32_t SprSelSetID   = 14020050;
constexpr const char* StyleNamesInternal[4] = { "arcade", "console", "mixed", "max" };

namespace pvsel
{
	std::string GSWindow::GetModePrefix() { return (Config::enableFtUi || Config::forceFtUI) ? "ps4_" : "nsw_"; }
	std::string GSWindow::GetLanguageSuffix()
	{
		int32_t locale = GetGameLocale();


		if (locale < 0 || locale >= GameLocale_Max)
			return "_en";

		const char* suffixes[GameLocale_Max] = { "_jp", "_en", "_zh", "_tw", "_kr", "_fr", "_it", "_de", "_sp" };
		return suffixes[locale];
	}

	std::string GSWindow::GetBaseLayerName() { return GetModePrefix() + "game_style_win"; }
	std::string GSWindow::GetBaseTextLayerName() { return GetModePrefix() + "game_style_txt" + GetLanguageSuffix(); }
	std::string GSWindow::GetStyleTextLayerName(int32_t style)
	{
		return GetModePrefix() + "game_style_" + StyleNamesInternal[style] + GetLanguageSuffix();
	}

	bool GSWindow::SetAvailableOptions(std::array<int32_t, GameStyle_Max> song_counts)
	{
		int32_t prev_option = options[option_count];
		option_count = 0;
		selected_index = 0;

		for (int32_t i = 0; i < GameStyle_Max; i++)
		{
			if (song_counts[i] > 0)
			{
				if (i == preferred_style)
					selected_index = option_count;

				options[option_count++] = i;
			}
		}

		return options[selected_index] != prev_option;
	}

	void GSWindow::UpdateAet()
	{
		//WriteLog("[GS_AET_LOG] ================== UpdateAet START ==================\n");

		if (!base_win.IsPlaying())
		{
			//WriteLog("[GS_AET_LOG] 1. base_win is NOT playing. Setting scene %u & layer '%s'...\n", AetSelSceneID, GetBaseLayerName().c_str());
			base_win.SetScene(AetSelSceneID);
			base_win.SetLayer(GetBaseLayerName(), 0x10000, 10, 14, "", "", nullptr);
			//WriteLog("[GS_AET_LOG] 1. base_win layer set SUCCESS!\n");
		}

		if (!base_txt.IsPlaying())
		{
			//WriteLog("[GS_AET_LOG] 2. base_txt is NOT playing. Setting scene %u & layer '%s'...\n", AetSelSceneID, GetBaseTextLayerName().c_str());
			base_txt.SetScene(AetSelSceneID);
			base_txt.SetLayer(GetBaseTextLayerName(), 0x10000, 10, 14, "", "", nullptr);
			//WriteLog("[GS_AET_LOG] 2. base_txt layer set SUCCESS!\n");
		}

		if (options[selected_index] != previous_option)
		{
			if (previous_option != GameStyle_Max)
			{
				prev_style_txt.SetScene(AetSelSceneID);
				prev_style_txt.SetLayer(GetStyleTextLayerName(previous_option), 10, 14, AetAction_OutOnce);
			}

			cur_style_txt.SetScene(AetSelSceneID);
			cur_style_txt.SetLayer(GetStyleTextLayerName(options[selected_index]), 10, 14, AetAction_InLoop);
		}
		//WriteLog("[GS_AET_LOG] 5. Setting markers...\n");
		if (IsToggleable())
		{
			base_win.SetMarkers("st_on", "ed_on", true);
			base_txt.SetMarkers("st_on", "ed_on", true);
		}
		else
		{
			base_win.SetMarkers("st_off", "ed_off", true);
			base_txt.SetMarkers("st_off", "ed_off", true);
		}
		//WriteLog("[GS_AET_LOG] 5. Markers SUCCESS!\n");

		//WriteLog("[GS_AET_LOG] 6. Setting positions...\n");
		diva_nc::vec3 offset;
		offset.x = game::IsFutureToneMode() && !IsToggleable() ? -18.0f : 0.0f;
		offset.y = 0.0f;
		offset.z = 0.0f;
		prev_style_txt.SetPosition(offset);
		cur_style_txt.SetPosition(offset);
		//WriteLog("[GS_AET_LOG] 6. Positions SUCCESS!\n");

		//WriteLog("[GS_AET_LOG] ================== UpdateAet FINISHED ==================\n");
	}

	void GSWindow::SetVisible(bool visible)
	{
		hidden = !visible;
		base_win.SetVisible(visible);
		base_txt.SetVisible(visible);
		prev_style_txt.SetVisible(visible);
		cur_style_txt.SetVisible(visible);
	}

	bool GSWindow::Ctrl()
	{
		// 1. Check if customization task blocks the UI
		bool is_custom_ready = game::IsCustomizeSelTaskReady();
		if (is_custom_ready)
		{
			static int custom_throttle = 0;
			if (custom_throttle++ % 60 == 0)
				//WriteLog("[GSWindow_Ctrl] BLOCKED by IsCustomizeSelTaskReady()!\n");

			SetVisible(false);
			return false;
		}

		diva_nc::InputState* is = diva_nc::GetInputState(0);

		// 2. Poll all possible button IDs to find the Switch equivalents
		for (int i = 0; i < 256; i++)
		{
			if (is->IsButtonTapped(i))
			{
				//WriteLog("[GSWindow_Ctrl] BUTTON TAPPED: ID = %d | IsToggleable: %d | OptionCount: %d\n", i, IsToggleable(), option_count);
			}
		}

		// 3. Throttle state logging to avoid log spam (prints once per second at 60fps)
		static int info_throttle = 0;
		if (info_throttle++ % 60 == 0)
		{
			// //WriteLog("[GSWindow_Ctrl] STATUS: Visible: %d | Toggleable: %d | OptCount: %d | SelIdx: %d | PrefStyle: %d\n",
			// 	!hidden, IsToggleable(), option_count, selected_index, preferred_style);
		}

		// PC/PS4 trigger IDs are usually 92 and 13.
		// NOTE: If your log shows ZL/ZR is, for example, 10 or 24, add it to this condition!
		if (IsToggleable() && (is->IsButtonTapped(92) || is->IsButtonTapped(13)))
		{
			//WriteLog("[GSWindow_Ctrl] >>> STYLE SWAP TRIGGERED! <<<\n");
			selected_index = util::Wrap(selected_index + 1, 0, option_count - 1);
			preferred_style = options[selected_index];
			dirty = true;
			sound::PlaySoundEffect(1, "se_ft_music_selector_sortfilter_change_01", 1.0f);
		}

		SetVisible(true);

		if (options[selected_index] != previous_option || aet_dirty)
		{
			UpdateAet();
			previous_option = options[selected_index];
			aet_dirty = false;
		}

		bool dirty_ret = this->dirty;
		this->dirty = false;
		return dirty_ret;
	}

	void GSWindow::Disp() const
	{
		if (!base_win.IsPlaying() || hidden || !IsToggleable())
			return;

		const uint32_t key_sprite_ids_nsw[6] = {
			2152960672, // JoyCon
            0
		};

		const uint32_t key_sprite_ids_ps4[6] = {
			3007666668, // JoyCon
			0
		};

		const uint32_t* sprites = (Config::enableFtUi || Config::forceFtUI) ? key_sprite_ids_ps4 : key_sprite_ids_nsw;
		base_win.DrawSpriteAt("p_key_icon_c", sprites[diva_nc::GetInputState(0)->GetDevice()]);
	}
}

namespace pvsel
{
	static bool assets_loaded = false;

	void RequestAssetsLoad()
	{
		//WriteLog("[AET_DEBUG] RequestAssetsLoad() triggered. AET ID: %d, SPR ID: %d\n", AetSelSetID, SprSelSetID);

		libcxx_string str;
		prj::string_view strv;

		//WriteLog("[AET_DEBUG] Calling aet::LoadAetSet...\n");
		aet::LoadAetSet(AetSelSetID, &str);

		//WriteLog("[AET_DEBUG] Calling spr::LoadSprSet...\n");
		spr::LoadSprSet(SprSelSetID, &strv);

		//WriteLog("[AET_DEBUG] RequestAssetsLoad() finished successfully.\n");
	}

	bool CheckAssetsLoaded()
	{

		bool aet_loading = aet::CheckAetSetLoading(AetSelSetID);
		bool spr_loading = spr::CheckSprSetLoading(SprSelSetID);

		static int poll_throttle = 0;
		if ((aet_loading || spr_loading) && (poll_throttle++ % 60 == 0))
		{
			//WriteLog("[AET_DEBUG] Polling... aet_loading: %d | spr_loading: %d\n", aet_loading, spr_loading);
		}

		if (!aet_loading && !spr_loading && !assets_loaded)
		{
			//WriteLog("[AET_DEBUG] SUCCESS! Both AET and SPR loaded.\n");
		}

		assets_loaded = !aet_loading && !spr_loading;
		return assets_loaded;
	}

	void UnloadAssets()
	{
		//WriteLog("[AET_DEBUG] UnloadAssets() triggered.\n");
		aet::UnloadAetSet(AetSelSetID);
		spr::UnloadSprSet(SprSelSetID);
	}

	int32_t GetSelectedStyleOrDefault()
	{
		if (gs_win)
		{
			if (int32_t style = gs_win->GetSelectedStyle(); style != GameStyle_Max)
				return style;
		}

		return GameStyle_Arcade;
	}

	int32_t GetPreferredStyleOrDefault()
	{
		if (gs_win)
		{
			if (int32_t style = gs_win->GetPreferredStyle(); style != GameStyle_Max)
				return style;
		}

		return GameStyle_Arcade;
	}

	extern "C" {
		bool CheckSongHasStyleAvailable(int32_t pv, int32_t difficulty, int32_t edition, int32_t style)
		{
			const db::SongEntry* song = db::FindSongEntry(pv);
			if (!song)
			{
				return style == GameStyle_Arcade;
			}

			return song->FindChart(difficulty, edition, style) != nullptr;
		}
	}

	int32_t CalculateSongStyleCount(int32_t pv, int32_t difficulty, int32_t edition)
	{
		if (const db::SongEntry* song = db::FindSongEntry(pv); song != nullptr)
		{
			int32_t count = 0;
			for (int32_t i = 0; i < GameStyle_Max; i++)
				if (song->FindChart(difficulty, edition, i))
					count++;

			return count;
		}

		return 0;
	}

	prj::vector<pvsel::PvData*> SortWithStyle(const SelPvList& list, int32_t difficulty, int32_t edition, int32_t style)
	{
		prj::vector<pvsel::PvData*> songs;
		if (!list.pv_data)
			return songs;

		for (pvsel::PvData* pv : *list.pv_data)
		{
			if (!pv->data2 || pvsel::CheckSongHasStyleAvailable(*pv->data2->pv_db_entry, difficulty, edition, style))
				songs.push_back(pv);
		}

		return songs;
	}
}


void InstallPvSelSwitchHooks();
void InstallPvSelPS4Hooks();

void InstallPvSelHooks()
{
	if (Config::enableFtUi || Config::forceFtUI)
		InstallPvSelPS4Hooks();
	else
		InstallPvSelSwitchHooks();
}
