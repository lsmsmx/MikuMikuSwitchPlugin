#include <stdint.h>
#include "../util.hpp"
#include "../nc_state.hpp"
#include "common.hpp"
#include "result.hpp"
#include "../../Config.hpp"
#include "logger.hpp"

static int32_t total_bonus_score = 0;
static std::pair<int32_t, int32_t> ct_perc;
static uint32_t ct_result_txt_id = 0;
static std::pair<int32_t, int32_t> tz_count;
static std::pair<int32_t, int32_t> tz_perc;

static void DrawNumberOrText(const AetComposition& comp, int32_t prio, const char* layer, const char* format, int32_t number = 0)
{
	if (auto it = comp.find(layer); it != comp.end())
	{
		const AetLayout& layout = it->second;

		uint32_t font_id = (Config::enableFtUi || Config::forceFtUI) ? 49485 : 53022;
		FontInfo font = FontInfo::CreateSpriteFont(font_id, 34, 32);
		font.SetSize(font.size.x * layout.matrix.row0.x, font.size.y * layout.matrix.row1.y);

		std::string formatted_text = util::Format(format, number);
		diva_nc::vec2 pos = GetLayoutAdjustedPosition(layout, layer);

		TextArgs args = { };
		args.print_work.text_current = pos;
		args.print_work.line_origin = pos;
		args.print_work.font = &font;
		args.print_work.res_mode = 14;
		args.print_work.prio = prio;
		args.print_work.SetColor(0xFFFFFFFF, 0xFF808080);
		args.print_work.SetOpacity(layout.opacity);

		spr::DrawTextA(&args, 0x2, formatted_text.c_str());
	}
}

static int32_t CalculateMaxRatePercentage(ScoreDetail* detail, int32_t index, float target_max_rate)
{
	float all_count = detail->judge_count[0] +
		detail->judge_count[1] +
		detail->judge_count[2] +
		detail->judge_count[3] +
		detail->judge_count[4];

	float perc = detail->judge_count[index] / all_count * target_max_rate * 100.0f;
	return static_cast<int32_t>(perc * 100);
}

bool nc::ShouldUseConsoleStyleWin() { return state.GetScoreMode() != ScoreMode_Arcade; }

void nc::InitResultsData(ScoreDetail* detail)
{
	total_bonus_score = state.CalculateTotalBonusScore();
	ct_perc = { -1, -1 };
	ct_result_txt_id = 0;

	if (state.chance_time.IsValid())
	{
		if (state.chance_time.successful)
		{
			ct_perc = {
				ChanceTimeRetainedRate * 100,
				static_cast<int32_t>(ChanceTimeRetainedRate * 100000) % 100
			};

			ct_result_txt_id = 3031571741;
		}
		else
		{
			ct_perc = { 0, 0 };
			ct_result_txt_id = 2360194960;
		}
	}

	if (detail)
	{
		if (state.GetScoreMode() == ScoreMode_F2nd)
		{
			detail->judge_perc[0] = CalculateMaxRatePercentage(detail, 0, state.score.target_max_rate);
			detail->judge_perc[1] = CalculateMaxRatePercentage(detail, 1, state.score.target_max_rate);
			detail->judge_perc[2] = 0;
			detail->judge_perc[3] = 0;
			detail->judge_perc[4] = 0;
		}
		else if (state.GetScoreMode() == ScoreMode_Franken)
		{
			for (int32_t i = 0; i < 5; i++)
				detail->judge_perc[i] = CalculateMaxRatePercentage(detail, i, state.score.target_max_rate);
		}
	}

	if (!state.tech_zones.empty())
	{
		tz_count.first = 0;
		tz_count.second = state.tech_zones.size();
		float perc = 0.0f;

		for (TechZoneState& tz : state.tech_zones)
		{
			if (tz.IsSuccessful())
			{
				tz_count.first++;
				perc += score::GetTechZoneRetainedRate();
			}
		}

		tz_perc = {
			perc * 100,
			static_cast<int32_t>(perc * 100000) % 100
		};
	}
	else
	{
		tz_count = { -1, -1 };
		tz_perc = { -1, -1 };
	}
}

void nc::DrawResultsWindowText(int32_t win, int32_t prio)
{
	AetComposition comp;
	aet::GetComposition(&comp, win);

	DrawNumberOrText(comp, prio, "p_ct_per_01_rt", ct_perc.first > -1 ? "%d" : "-", ct_perc.first);
	DrawNumberOrText(comp, prio, "p_ct_per_02_rt", ct_perc.second > -1 ? "%02d" : "--", ct_perc.second);
	if (ct_result_txt_id != 0)
	{
		if (auto it = comp.find("p_ct_result_txt"); it != comp.end())
			DrawSpriteAtLayout(it->second, "p_ct_result_txt", ct_result_txt_id, prio, 14);
	}

	DrawNumberOrText(comp, prio, "p_tz_count_num_01_rt", tz_count.first > -1 ? "%d" : "-", tz_count.first);
	DrawNumberOrText(comp, prio, "p_tz_count_num_02_rt", tz_count.second > -1 ? "%d" : "-", tz_count.second);
	DrawNumberOrText(comp, prio, "p_tz_per_01_rt", tz_perc.first > -1 ? "%d" : "-", tz_perc.first);
	DrawNumberOrText(comp, prio, "p_tz_per_02_rt", tz_perc.second > -1 ? "%02d" : "--", tz_perc.second);
	DrawNumberOrText(comp, prio, "p_bonus_num_rt", total_bonus_score <= 0 ? "%d" : "%+d", total_bonus_score);
}

void InstallResultPS4Hooks();
void InstallResultSwitchHooks();

void nc::InstallResultsHook()
{
	if (Config::enableFtUi || Config::forceFtUI)
		InstallResultPS4Hooks();
	else
		InstallResultSwitchHooks();
}
