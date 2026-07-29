#pragma once

#include "InputComposition.h"

#include <string>

class InputCommandTarget
{
public:
	virtual ~InputCommandTarget() = default;
	virtual void InputCommand(const std::string& command, InputControlId control, float delta) = 0;
	virtual void ReleaseInputControl(InputControlId control) = 0;
	virtual void ReleaseInputSource(InputSourceId source) = 0;
};
