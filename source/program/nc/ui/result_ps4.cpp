#include <stdint.h>
#include "lib.hpp"
#include "../util.hpp"
#include "../nc_state.hpp"
#include "common.hpp"
#include "result.hpp"

struct StageResultPS4
{
	uint8_t gap0[104];
	int32_t state;
	int32_t cmn_bg;
	int32_t win_base;
	int32_t win;
	int32_t win_almost;
	int32_t win_achieve;
	int32_t win_score;
	int32_t logo_img;
	int32_t jk_img;
	int32_t tit_clear;
	int32_t tit_value;
	int32_t eff_clear;
	int32_t eff_record;
	int32_t eff_clear_ahv;
	int32_t win_option_base;
	int32_t win_option;
	int32_t dwordA8;
	int32_t result_c;
	uint8_t gapB0[8];
	int32_t result_kansou_tit;
	int32_t belt_gam_rslt;
	int32_t rank_max_f;
	int32_t rank_max_t;
	int32_t result_tit;
	int32_t g_level;
	uint8_t gapD0[4];
	int32_t result_survival_tit;
	uint8_t gapD8[48];
	ScoreDetail* detail;
	uint8_t gap110[8];
	char char118;
	uint8_t gap119[23];
	uint64_t qword130;
	libcxx_string win_base_name;
	libcxx_string win_name;
	uint8_t gap178[199];
	bool score_win_in;
	uint8_t gap240[56];
	uint8_t byte278;
	uint8_t byte279;
};

static std::string GetStyleLayerName()
{
	const char* styles[GameStyle_Max] = { "", "console", "mixed" };
	return std::string("ps4_tit_") + styles[state.GetGameStyle()] + GetLanguageSuffix();
}

static std::string GetLocalizedLayerNameOnlyJP(std::string_view name)
{
	return std::string(name) + (GetGameLocale() == GameLocale_JP ? "_jp" : "_en");
}

static void PatchWinAlmost(StageResultPS4* result, const char* name, int32_t flags = 0x20000)
{
	if (result->win_almost > 0 && nc::ShouldUseConsoleStyleWin())
	{
		aet::Stop(&result->win_almost);
		result->win_almost = aet::PlayLayer(
			1279,
			3,
			flags,
			name,
			nullptr,
			nullptr,
			nullptr
		);
	}
}

class StyleBelt : public AetElement
{
private:
	AetElement style_txt;

public:
	StyleBelt()
	{
		SetScene(results::AetSceneID);
		SetLayer("ps4_gam_belt_rslt", 3, 14, AetAction_InOnce);
	}

	void Ctrl() override
	{
		if (Ended())
		{
			SetLayer("ps4_gam_belt_rslt", 3, 14, AetAction_Loop);
			style_txt.SetScene(results::AetSceneID);
			style_txt.SetLayer(GetStyleLayerName(), 3, 14, AetAction_InLoop);
		}
	}
};

static std::unique_ptr<StyleBelt> belt;

// Custom inline hook on Switch to override AetSceneID in StageResultPS4Disp (0x15a0a8)
HOOK_DEFINE_INLINE(WriteDispAetSceneIDHook) {
	static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
		// Set requested ID into W2 (3rd argument of LoadAetFrameH)
		ctx->W[2] = nc::ShouldUseConsoleStyleWin() ? 14010071 : 1279;
	}
};

static void SetWindowAet(StageResultPS4* result, const std::string& name, bool loop = false)
{
	if (!nc::ShouldUseConsoleStyleWin())
		return;

	aet::Stop(&result->win);
	result->win = aet::PlayLayer(results::AetSceneID, 3, loop ? 0x10000 : 0x20000, name.c_str(), nullptr, nullptr, nullptr);
	result->win_name = name;
}

// 1. PutScoreWindowIn (0x158080)
HOOK_DEFINE_TRAMPOLINE(PutScoreWindowInHook) {
	static void Callback(StageResultPS4* result) {
		Orig(result);
		SetWindowAet(result, GetLocalizedLayerNameOnlyJP("ps4_win_nc_in"));
		PatchWinAlmost(result, "win_almost_in");
	}
};

// 2. PutWinCount (0x158240)
HOOK_DEFINE_TRAMPOLINE(PutWinCountHook) {
	static void Callback(StageResultPS4* result) {
		Orig(result);
		SetWindowAet(result, GetLocalizedLayerNameOnlyJP("ps4_win_nc_count"));
		PatchWinAlmost(result, "win_almost_count");
	}
};

// 3. PutWinLoop (0x1587d0)
HOOK_DEFINE_TRAMPOLINE(PutWinLoopHook) {
	static void Callback(StageResultPS4* result) {
		Orig(result);
		SetWindowAet(result, GetLocalizedLayerNameOnlyJP("ps4_win_nc_loop"), true);
		PatchWinAlmost(result, "win_almost_loop", 0x10000);
	}
};

// 4. PutWinOut (0x1589e0)
HOOK_DEFINE_TRAMPOLINE(PutWinOutHook) {
	static void Callback(StageResultPS4* result) {
		Orig(result);
		SetWindowAet(result, GetLocalizedLayerNameOnlyJP("ps4_win_nc_out"));
		PatchWinAlmost(result, "win_almost_out");
	}
};

// 5. StageResultPS4Init (0x154d70)
HOOK_DEFINE_TRAMPOLINE(StageResultPS4InitHook) {
	static bool Callback(StageResultPS4* result) {
		bool ret = Orig(result);
		state.ui.ResetAllLayers();

		libcxx_string str;
		prj::string_view strv;
		aet::LoadAetSet(results::AetSetID, &str);
		spr::LoadSprSet(results::SprSetID, &strv);

		nc::InitResultsData(result->detail);

		return ret;
	}
};

// 6. StageResultPS4Ctrl (0x1553f0)
HOOK_DEFINE_TRAMPOLINE(StageResultPS4CtrlHook) {
	static bool Callback(StageResultPS4* result) {
		if (result->state == 0)
		{
			if (aet::CheckAetSetLoading(results::AetSetID) || spr::CheckSprSetLoading(results::SprSetID))
				return false;
		}

		bool ret = Orig(result);

		if (result->state == 10)
		{
			if (!belt && state.GetGameStyle() != GameStyle_Arcade)
				belt = std::make_unique<StyleBelt>();
		}

		if (belt)
			belt->Ctrl();

		return ret;
	}
};

// 7. StageResultPS4Dest (0x1599e0)
HOOK_DEFINE_TRAMPOLINE(StageResultPS4DestHook) {
	static bool Callback(uint64_t a1) {
		belt.reset();
		aet::UnloadAetSet(results::AetSetID);
		spr::UnloadSprSet(results::SprSetID);
		return Orig(a1);
	}
};

// 8. StageResultPS4Disp (0x159ce0)
HOOK_DEFINE_TRAMPOLINE(StageResultPS4DispHook) {
	static void Callback(StageResultPS4* result) {
		Orig(result);
		if (!result->win)
			return;

		if (nc::ShouldUseConsoleStyleWin())
			nc::DrawResultsWindowText(result->win, 6);
	}
};

void InstallResultPS4Hooks()
{
	PutScoreWindowInHook::InstallAtOffset(0x158080);
	PutWinCountHook::InstallAtOffset(0x158240);
	PutWinLoopHook::InstallAtOffset(0x1587d0);
	PutWinOutHook::InstallAtOffset(0x1589e0);
	StageResultPS4InitHook::InstallAtOffset(0x154d70);
	StageResultPS4CtrlHook::InstallAtOffset(0x1553f0);
	StageResultPS4DestHook::InstallAtOffset(0x1599e0);
	StageResultPS4DispHook::InstallAtOffset(0x159ce0);

	WriteDispAetSceneIDHook::InstallAtOffset(0x15a0a8);
}
