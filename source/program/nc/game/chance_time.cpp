#include "../nc_state.hpp"
#include "chance_time.hpp"

// Reordered arguments: float is first to match Switch AAPCS64 convention
static FUNCTION_PTR(void, __fastcall, SetNormalFrameAction, 0x1bbe30, float length_sec, PVGameUI* a1, bool in);
static FUNCTION_PTR(void, __fastcall, SetFrameAction, 0x1bbea0, float length, PVGameUI* a1, bool visible, int32_t index, int32_t action);

bool SetChanceTimeMode(PVGameUI* ui, int32_t mode)
{
	// Chance Time Start
	if (mode == ModeSelect_ChanceStart)
	{
		SetNormalFrameAction(1.0f, ui, false);
		SetFrameAction(60.0f, ui, false, 1, 3);

		ui->draw_combo_counter = true;

		state.ui.SetLayer(LayerUI_ChanceFrameTop, true, "chance_frame_top", 12, 0x10000);
		state.ui.SetLayer(LayerUI_ChanceFrameBottom, true, "chance_frame_bottom", 12, 0x10000);
		state.ui.SetLayer(LayerUI_StarGauge, true, "gauge_ch00", 13, 0x10000);
		state.ui.SetLayer(LayerUI_ChanceTxt, true, "chance_start_txt", 12, 0x20000);

		state.chance_time.enabled = true;
	}
	// Chance Time End
	else if (mode == ModeSelect_ChanceEnd)
	{
		SetNormalFrameAction(1.0f, ui, true);
		SetFrameAction(60.0f, ui, false, 1, 4);

		state.chance_time.enabled = false;
		state.ui.SetLayer(
			LayerUI_ChanceTxt,
			true,
			state.chance_time.successful ? "chance_result_success_txt" : "chance_end_txt",
			12,
			0x20000
		);
	}
	else
		return false;

	return true;
}

void SetChanceTimeStarFill(PVGameUI* ui, int32_t fill_rate)
{
	char name[32];
	sprintf(name, "gauge_ch%02d", state.chance_time.GetFillRate());
	state.ui.SetLayer(LayerUI_StarGauge, true, name, 13, 0x10000);
}

void SetChanceTimePosition(PVGameUI* ui)
{
	diva_nc::vec3 top = { 0.0f, -ui->frame_top_offset[1], 0.0f };
	diva_nc::vec3 bottom = { 0.0f, ui->frame_bottom_offset[1], 0.0f };

	aet::SetPosition(state.ui.aet_list[LayerUI_ChanceFrameTop], &top);
	aet::SetPosition(state.ui.aet_list[LayerUI_ChanceFrameBottom], &bottom);
	aet::SetPosition(state.ui.aet_list[LayerUI_StarGauge], &bottom);
}
