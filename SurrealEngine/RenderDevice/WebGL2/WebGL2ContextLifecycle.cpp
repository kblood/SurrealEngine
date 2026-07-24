#include "WebGL2ContextLifecycle.h"

bool WebGL2ContextLifecycle::CompleteInitialCreation(bool success)
{
	if (state != WebGL2ContextState::Uninitialized)
		return false;
	state = success ? WebGL2ContextState::Ready : WebGL2ContextState::Failed;
	generation = success ? 1u : 0u;
	return true;
}

bool WebGL2ContextLifecycle::MarkLost()
{
	if (state != WebGL2ContextState::Ready)
		return false;
	state = WebGL2ContextState::Lost;
	lossCount++;
	return true;
}

bool WebGL2ContextLifecycle::BeginRestore()
{
	if (state != WebGL2ContextState::Lost)
		return false;
	state = WebGL2ContextState::Restoring;
	return true;
}

bool WebGL2ContextLifecycle::CompleteRestore(bool success)
{
	if (state != WebGL2ContextState::Restoring)
		return false;
	state = success ? WebGL2ContextState::Ready : WebGL2ContextState::Failed;
	if (success)
		generation++;
	return true;
}
