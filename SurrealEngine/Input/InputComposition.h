#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// Identifies an independent producer of engine input. Existing window input
// uses KeyboardMouse by default; optional platform and XR integrations can
// publish through their own source without overwriting another source's state.
enum class InputSourceId : uint8_t
{
	KeyboardMouse,
	Gamepad,
	XRLeft,
	XRRight,
	Synthetic
};

struct InputControlId
{
	InputSourceId Source = InputSourceId::KeyboardMouse;
	int32_t Control = 0;

	bool operator<(const InputControlId& other) const
	{
		if (Source != other.Source)
			return Source < other.Source;
		return Control < other.Control;
	}
};

struct ReleasedInputActions
{
	std::vector<std::string> Buttons;
	std::vector<std::string> Axes;
};

// Collects input contributions without depending on a platform, game, UObject,
// or presentation provider. A button remains active while any contributor is
// held. Axis contributions are added, which gives opposing bindings and
// simultaneous desktop/XR input a deterministic composition rule.
class InputComposition
{
public:
	using ButtonContributors = std::set<InputControlId>;
	using AxisContributors = std::map<InputControlId, float>;

	void SetButton(const std::string& action, InputControlId control);
	void SetAxis(const std::string& action, InputControlId control, float value);

	ReleasedInputActions ReleaseControl(InputControlId control);
	ReleasedInputActions ReleaseSource(InputSourceId source);
	ReleasedInputActions Clear();

	bool IsButtonActive(const std::string& action) const;
	bool IsControlActive(int32_t control) const;
	float GetAxisValue(const std::string& action) const;

	const std::map<std::string, ButtonContributors>& Buttons() const { return buttons; }
	const std::map<std::string, AxisContributors>& Axes() const { return axes; }

private:
	std::map<std::string, ButtonContributors> buttons;
	std::map<std::string, AxisContributors> axes;
};
