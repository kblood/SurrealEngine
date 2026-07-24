#include "RenderDevice/WebGL2/WebGL2ContextLifecycle.h"

#include <iostream>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	WebGL2ContextLifecycle lifecycle;
	if (lifecycle.State() != WebGL2ContextState::Uninitialized || lifecycle.Generation() != 0 || lifecycle.LossCount() != 0)
		return Fail("new lifecycle did not start uninitialized");
	if (lifecycle.MarkLost() || lifecycle.BeginRestore() || lifecycle.CompleteRestore(true))
		return Fail("lifecycle accepted a transition before context creation");
	if (!lifecycle.CompleteInitialCreation(true) || lifecycle.State() != WebGL2ContextState::Ready || lifecycle.Generation() != 1)
		return Fail("successful context creation did not enter generation one");
	if (lifecycle.CompleteInitialCreation(true))
		return Fail("context creation was accepted twice");
	if (!lifecycle.MarkLost() || lifecycle.State() != WebGL2ContextState::Lost || lifecycle.LossCount() != 1)
		return Fail("context loss was not recorded");
	if (lifecycle.MarkLost())
		return Fail("duplicate loss changed the lifecycle");
	if (!lifecycle.BeginRestore() || lifecycle.State() != WebGL2ContextState::Restoring)
		return Fail("context restore did not begin");
	if (!lifecycle.CompleteRestore(true) || lifecycle.State() != WebGL2ContextState::Ready || lifecycle.Generation() != 2)
		return Fail("successful restore did not advance the generation");

	if (!lifecycle.MarkLost() || !lifecycle.BeginRestore() || !lifecycle.CompleteRestore(false))
		return Fail("failed restore transition was rejected");
	if (lifecycle.State() != WebGL2ContextState::Failed || lifecycle.Generation() != 2 || lifecycle.LossCount() != 2)
		return Fail("failed restore corrupted lifecycle counters");
	if (lifecycle.MarkLost() || lifecycle.BeginRestore() || lifecycle.CompleteRestore(true))
		return Fail("failed lifecycle accepted an implicit recovery");

	WebGL2ContextLifecycle failedCreation;
	if (!failedCreation.CompleteInitialCreation(false) || failedCreation.State() != WebGL2ContextState::Failed || failedCreation.Generation() != 0)
		return Fail("failed initial creation was not terminal");

	return 0;
}
