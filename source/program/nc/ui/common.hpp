#pragma once

#include <utility>
#include <optional>
#include <functional>
#include <string_view>
#include <string>
#include <vector>
#include "../diva_nc.hpp"
#include "../../ModLoader.hpp"

enum KeyAction : int32_t
{
	KeyAction_None      = 0,
	KeyAction_MoveUp    = 1,
	KeyAction_MoveDown  = 2,
	KeyAction_MoveLeft  = 3,
	KeyAction_MoveRight = 4,
	KeyAction_Enter     = 5,
	KeyAction_Cancel    = 6,
	KeyAction_SwapLeft  = 7,
	KeyAction_SwapRight = 8,
	KeyAction_Preview   = 9
};

enum LimitMode : int32_t
{
	LimitMode_Disabled = 0,
	LimitMode_Clamp = 1,
	LimitMode_Wrap = 2
};

enum AnchorMode : int32_t
{
	AnchorMode_Left    = 0,
	AnchorMode_Right   = 1,
	AnchorMode_Top     = 2,
	AnchorMode_Bottom  = 3,
	AnchorMode_Center  = 4,
};

class AetElement
{
public:
	AetElement() = default;
	AetElement(uint32_t scene_id) : scene_id(scene_id) { }

	AetElement(AetElement&& other) noexcept
	{
		scene_id = other.scene_id;
		handle = other.handle;
		args = other.args;
		layer_name = other.layer_name;
		other.handle = 0;
	}

	~AetElement() { DeleteHandle(); }

	virtual void Ctrl();
	virtual void Disp();

	inline void SetScene(uint32_t id) { scene_id = id; }
	bool SetLayer(std::string name, int32_t flags, int32_t prio, int32_t res_mode, std::string_view start_marker, std::string_view end_marker, const diva_nc::vec3* pos);
	bool SetLayer(std::string name, int32_t prio, int32_t res_mode, int32_t action);

	std::optional<AetLayout> GetLayout(std::string layer_name) const;

	void SetPosition(const diva_nc::vec3& pos);
	void SetOpacity(float opacity);
	void SetLoop(bool loop);
	void SetVisible(bool visible);
	void SetColor(int32_t color);
	void SetMarkers(const std::string& start_marker, const std::string& end_marker);
	void SetMarkers(const std::string& start_marker, const std::string& end_marker, bool loop);

	inline uint32_t GetSceneID() { return scene_id; }
	inline const AetArgs& GetArgs() { return args; }
	inline bool Ended() const { return aet::GetEnded(handle); }
	inline bool IsPlaying() const { return handle != 0; }

	bool DrawSpriteAt(std::string layer_name, uint32_t id, int32_t prio = 1) const;
private:
	uint32_t scene_id = 0;
	AetHandle handle = 0;
	AetArgs args = { };
	libcxx_string layer_name;

	void DeleteHandle();
	void Remake();
};

class AetControl : public AetElement
{
public:
	void AllowInputsWhenBlocked(bool allow);
	void SetFocus(bool focused);
	inline bool IsFocused() const { return focused; }

	virtual void OnActionPressed(int32_t action);
	virtual void OnActionPressedOrRepeat(int32_t action);

	virtual void Ctrl() override;
protected:
	bool abs_input = false;
	bool focused = true;
};

class HorizontalSelector : public AetControl
{
public:
	using PreviewNotifier = std::function<void(HorizontalSelector*, const void*)>;

	float font_scale = 0.85f;
	float text_opacity = 1.0f;
	bool bold_text = true;
	bool squish_text = true;

	HorizontalSelector()
	{
		SetFocus(false);
	}

	HorizontalSelector(uint32_t scene_id, std::string layer_name, int32_t prio, int32_t res_mode)
	{
		SetFocus(false);
		SetScene(scene_id);
		SetLayer(layer_name, 0x10000, prio, res_mode, "st_in", "ed_in", nullptr);
	}

	virtual void OnActionPressedOrRepeat(int32_t action) override;
	virtual void OnActionPressed(int32_t action) override;
	virtual void Ctrl() override;
	virtual void Disp() override;

	inline void SetExtraData(const void* data) { extra_data = data; }
	inline void SetPreviewNotifier(PreviewNotifier func) { preview_notify = func; }

	void SetArrows(const std::string& left, const std::string& right);
protected:
	virtual std::string GetSelectedValue() = 0;
	virtual void ChangeValue(int32_t dir) = 0;

private:
	std::optional<PreviewNotifier> preview_notify;
	const void* extra_data = nullptr;
	bool focused_old = false;
	bool sp_ended = false;
	bool arrow_sp_ended = false;
	int32_t enter_anim_state = -1;

	AetElement arrow_l;
	AetElement arrow_r;
};

class HorizontalSelectorMulti : public HorizontalSelector
{
public:
	using Notifier = std::function<void(int32_t)>;

	std::vector<std::string> values;
	int32_t selected_index = 0;

	HorizontalSelectorMulti() = default;
	HorizontalSelectorMulti(uint32_t scene_id, std::string layer_name, int32_t prio, int32_t res_mode) :
		HorizontalSelector(scene_id, layer_name, prio, res_mode) { }

	inline void SetOnChangeNotifier(Notifier func) { notify = func; }
protected:
	std::optional<Notifier> notify;

	std::string GetSelectedValue() override;
	void ChangeValue(int32_t dir) override;
};

class HorizontalSelectorNumber : public HorizontalSelector
{
public:
	using Notifier = std::function<void(float)>;

	float value_step = 1.0f;
	float value_min = -1.0f;
	float value_max = -1.0f;
	int32_t travel_mode = LimitMode_Clamp;
	std::string format_string = "%.2f";

	HorizontalSelectorNumber() = default;
	HorizontalSelectorNumber(uint32_t scene_id, std::string layer_name, int32_t prio, int32_t res_mode) :
		HorizontalSelector(scene_id, layer_name, prio, res_mode) { }

	inline void SetOnChangeNotifier(Notifier func) { notify = func; }
	inline void SetValue(float v) { value = v; }
protected:
	float value = 0.0f;
	std::optional<Notifier> notify;

	std::string GetSelectedValue() override;
	void ChangeValue(int32_t dir) override;
};

std::pair<int32_t, int32_t> GetLayerAxisAnchor(std::string_view layer_name);
int32_t GetLayerSpriteAnchor(std::string_view layer_name);

diva_nc::vec2 GetLayoutAdjustedPosition(const AetLayout& layout, std::string_view layer_name);

void DrawSpriteAtLayout(const AetLayout& layout, std::string_view layer_name, uint32_t sprite_id, int32_t prio, int32_t res, bool adjust_pos = false);

std::string GetLanguageSuffix();
