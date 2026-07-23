#pragma once

namespace WebXR
{
	enum class FramePhase
	{
		Idle,
		Prepared,
		Rendered
	};

	class FramePhaseState
	{
	public:
		bool BeginPrepare();
		bool MarkRendered();
		bool Complete();
		void Reset();
		FramePhase Phase() const { return phase; }

	private:
		FramePhase phase = FramePhase::Idle;
	};
}
