#include "InputComposition.h"

void InputComposition::SetButton(const std::string& action, InputControlId control)
{
	if (!action.empty())
		buttons[action].insert(control);
}

void InputComposition::SetAxis(const std::string& action, InputControlId control, float value)
{
	if (!action.empty())
		axes[action][control] = value;
}

ReleasedInputActions InputComposition::ReleaseControl(InputControlId control)
{
	ReleasedInputActions released;

	for (auto it = buttons.begin(); it != buttons.end();)
	{
		it->second.erase(control);
		if (it->second.empty())
		{
			released.Buttons.push_back(it->first);
			it = buttons.erase(it);
		}
		else
		{
			++it;
		}
	}

	for (auto it = axes.begin(); it != axes.end();)
	{
		it->second.erase(control);
		if (it->second.empty())
		{
			released.Axes.push_back(it->first);
			it = axes.erase(it);
		}
		else
		{
			++it;
		}
	}

	return released;
}

ReleasedInputActions InputComposition::ReleaseSource(InputSourceId source)
{
	ReleasedInputActions released;

	for (auto it = buttons.begin(); it != buttons.end();)
	{
		for (auto contributor = it->second.begin(); contributor != it->second.end();)
		{
			if (contributor->Source == source)
				contributor = it->second.erase(contributor);
			else
				++contributor;
		}
		if (it->second.empty())
		{
			released.Buttons.push_back(it->first);
			it = buttons.erase(it);
		}
		else
		{
			++it;
		}
	}

	for (auto it = axes.begin(); it != axes.end();)
	{
		for (auto contributor = it->second.begin(); contributor != it->second.end();)
		{
			if (contributor->first.Source == source)
				contributor = it->second.erase(contributor);
			else
				++contributor;
		}
		if (it->second.empty())
		{
			released.Axes.push_back(it->first);
			it = axes.erase(it);
		}
		else
		{
			++it;
		}
	}

	return released;
}

ReleasedInputActions InputComposition::Clear()
{
	ReleasedInputActions released;
	for (const auto& button : buttons)
		released.Buttons.push_back(button.first);
	for (const auto& axis : axes)
		released.Axes.push_back(axis.first);
	buttons.clear();
	axes.clear();
	return released;
}

bool InputComposition::IsButtonActive(const std::string& action) const
{
	auto it = buttons.find(action);
	return it != buttons.end() && !it->second.empty();
}

bool InputComposition::IsControlActive(int32_t control) const
{
	for (const auto& action : buttons)
	{
		for (const InputControlId& contributor : action.second)
		{
			if (contributor.Control == control)
				return true;
		}
	}
	return false;
}

float InputComposition::GetAxisValue(const std::string& action) const
{
	auto it = axes.find(action);
	if (it == axes.end())
		return 0.0f;

	float value = 0.0f;
	for (const auto& contributor : it->second)
		value += contributor.second;
	return value;
}
