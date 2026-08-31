#include "../diva_nc.hpp"
#include "lib.hpp"
#include "logger.hpp"
#include "../nc_state.hpp"
#include "../util.hpp"
#include "common.hpp"
#include "result.hpp"
#include "../../ModLoader.hpp"
#include <unordered_set>

static std::unordered_set<void*> patched_scene_controllers;
static bool patch_scene = false;

struct CAetController {
	void** vftable;
	AetArgs args;
};

static void GetModeLayerName(libcxx_string& out, int32_t kind, bool no_fail) {
	static const char* styles[4] = { "", "console", "mixed", "max" };
	static const char* kinds[3] = { "in", "loop", "out" };
	std::string name = std::string("nsw_mode_tit_") + styles[state.GetGameStyle()] + "_" + (no_fail ? "nofail_" : "") + kinds[kind] + GetLanguageSuffix().c_str();
	out.assign(name.c_str(), name.length());
}

static void GetWindowLayerName(libcxx_string& out, int32_t kind) {
	static const char* kinds[4] = { "slide in", "slide loop", "slide_out", "count" };
	std::string name = std::string("nsw_win_arcard_") + kinds[kind] + (GetGameLocale() == GameLocale_JP ? "_jp" : "_en");
	out.assign(name.c_str(), name.length());
}

static bool CheckLayerNameMatchesNC(std::string_view name) {
	static const char* checks[2] = { "nsw_win_arcard_slide", "nsw_mode_tit" };
	for (int32_t i = 0; i < 2; i++) {
		if (util::Contains(name, checks[i])) return true;
	}
	return false;
}

HOOK_DEFINE_TRAMPOLINE(CStageResultAetControllerInLoopOutGetSceneIDHook) {
	static uint32_t Callback(void* a1) {
		uint32_t id = Orig(a1);
		bool patched = patch_scene || (patched_scene_controllers.count(a1) != 0);
		return patched ? results::AetSceneID : id;
	}
};

HOOK_DEFINE_TRAMPOLINE(CStageResultAetControllerGetSceneIDHook2) {
	static uint32_t Callback(void* a1) {
		uint32_t id = Orig(a1);
		bool patched = patch_scene || (patched_scene_controllers.count(a1) != 0);
		return patched ? results::AetSceneID : id;
	}
};

HOOK_DEFINE_TRAMPOLINE(CAetControllerInLoopOutSetLayerHook) {
	static void Callback(void* a1, const void* in, const void* loop, const void* out, int32_t prio) {
		const libcxx_string* in_str = reinterpret_cast<const libcxx_string*>(in);
		const libcxx_string* loop_str = reinterpret_cast<const libcxx_string*>(loop);
		const libcxx_string* out_str = reinterpret_cast<const libcxx_string*>(out);

		libcxx_string patched_in, patched_lp, patched_out;
		if (in_str) patched_in.assign(in_str->c_str(), in_str->length());
		if (loop_str) patched_lp.assign(loop_str->c_str(), loop_str->length());
		if (out_str) patched_out.assign(out_str->c_str(), out_str->length());

		bool did_patch_names = false;

		if (in_str && loop_str) {
			if (util::StartsWith(in_str->c_str(), "mode_tit_arcade") && util::StartsWith(loop_str->c_str(), "mode_tit_arcade")) {
				if (state.GetGameStyle() != GameStyle_Arcade) {
					bool no_fail = util::Contains(in_str->c_str(), "comp") || util::Contains(loop_str->c_str(), "comp");
					GetModeLayerName(patched_in, 0, no_fail);
					GetModeLayerName(patched_lp, 1, no_fail);
					did_patch_names = true;
				}
			}
			else if (util::StartsWith(in_str->c_str(), "win_arcard") && util::StartsWith(loop_str->c_str(), "win_arcard")) {
				if (nc::ShouldUseConsoleStyleWin()) {
					GetWindowLayerName(patched_in, 0);
					GetWindowLayerName(patched_lp, 1);
					if (out_str) GetWindowLayerName(patched_out, 2);
					did_patch_names = true;
				}
			}
		}

		if (did_patch_names) {
			patched_scene_controllers.insert(a1);
		}

		Orig(a1, in_str ? &patched_in : nullptr, loop_str ? &patched_lp : nullptr, out_str ? &patched_out : nullptr, prio);
	}
};

HOOK_DEFINE_TRAMPOLINE(CStageResultAetControllerSetLayerHook) {
	static void Callback(void* a1, void* name, int32_t prio, int32_t action) {
		const libcxx_string* name_str = reinterpret_cast<const libcxx_string*>(name);

		if (name_str) {
			patch_scene = CheckLayerNameMatchesNC(name_str->c_str());
		}
		Orig(a1, name, prio, action);
		patch_scene = false;
	}
};

HOOK_DEFINE_TRAMPOLINE(CAetControllerGetLayoutHook) {
	static void Callback(CAetController* a1, void* a2, void* a3) {
		patch_scene = CheckLayerNameMatchesNC(a1->args.layer_name);
		Orig(a1, a2, a3);
		patch_scene = false;
	}
};

HOOK_DEFINE_TRAMPOLINE(StageResultSwitchInitHook) {
	static bool Callback(void* a1) {
		// Clear tracking set before results stage initialization
		patched_scene_controllers.clear();
		patch_scene = false;

		state.ui.ResetAllLayers();
		libcxx_string out;
		prj::string_view strv;
		aet::LoadAetSet(results::AetSetID, &out);
		spr::LoadSprSet(results::SprSetID, &strv);
		return Orig(a1);
	}
};

HOOK_DEFINE_TRAMPOLINE(StageResultSwitchWaitLoadHook) {
	static void Callback(void* a1) {
		bool aet_loading = aet::CheckAetSetLoading(results::AetSetID);
		bool spr_loading = spr::CheckSprSetLoading(results::SprSetID);
		if (aet_loading || spr_loading) {
			return;
		}
		Orig(a1);
	}
};

HOOK_DEFINE_TRAMPOLINE(StageResultSwitchDestHook) {
	static bool Callback(void* a1) {
		// Clear tracking set upon exiting results stage
		patched_scene_controllers.clear();
		patch_scene = false;

		aet::UnloadAetSet(results::AetSetID);
		spr::UnloadSprSet(results::SprSetID);
		return Orig(a1);
	}
};

HOOK_DEFINE_TRAMPOLINE(StageResultSwitchDetailInitHook) {
	static void Callback(char* a1) {
		Orig(a1);

		ScoreDetail* detail = *reinterpret_cast<ScoreDetail**>(a1 + 0x18);
		nc::InitResultsData(detail);
	}
};

HOOK_DEFINE_TRAMPOLINE(StageResultSwitchDetailDispHook) {
	static void Callback(char* a1) {
		int32_t val1 = *reinterpret_cast<const int32_t*>(a1 + 0x194);
		int32_t val2 = *reinterpret_cast<const int32_t*>(a1 + 0xC);

		nc::DrawResultsWindowText(val1, val2);
		Orig(a1);
	}
};

void InstallResultSwitchHooks()
{
	CStageResultAetControllerInLoopOutGetSceneIDHook::InstallAtOffset(0x6db7e0);
	CStageResultAetControllerGetSceneIDHook2::InstallAtOffset(0x6dc080);
	CAetControllerInLoopOutSetLayerHook::InstallAtOffset(0x71b120);
	CStageResultAetControllerSetLayerHook::InstallAtOffset(0x719c30);
	CAetControllerGetLayoutHook::InstallAtOffset(0x71a1f0);
	StageResultSwitchInitHook::InstallAtOffset(0x6dd1c0);
	StageResultSwitchWaitLoadHook::InstallAtOffset(0x6dd420);
	StageResultSwitchDestHook::InstallAtOffset(0x6ddac0);
	StageResultSwitchDetailInitHook::InstallAtOffset(0x6d2090);
	StageResultSwitchDetailDispHook::InstallAtOffset(0x6d35f0);
}
