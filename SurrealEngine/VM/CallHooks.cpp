#include "Precomp.h"
#include "CallHooks.h"
#include <algorithm>

bool VMCallArguments::Replace(size_t index, ExpressionValue value)
{
	if (index >= Arguments.size())
		return false;

	auto backup = std::find_if(Backups.begin(), Backups.end(),
		[index](const Backup& item) { return item.Index == index; });
	if (backup == Backups.end())
		Backups.push_back({ index, Arguments[index] });

	Arguments[index] = std::move(value);
	return true;
}

VMCallArguments::~VMCallArguments() noexcept
{
	if (Committed)
		return;

	for (auto it = Backups.rbegin(); it != Backups.rend(); ++it)
	{
		try
		{
			Arguments[it->Index] = std::move(it->Value);
		}
		catch (...)
		{
		}
	}
}

VMCallHookRegistry::CallScope::CallScope(UFunction* function, UObject* instance,
	Array<ExpressionValue>& arguments, std::vector<VMCallHook> hooks)
	: Function(function), Instance(instance), Arguments(&arguments)
{
	ActiveHooks.reserve(hooks.size());
	for (VMCallHook& hook : hooks)
	{
		VMCallHookCleanup cleanup;
		VMCallArguments editor(arguments);
		try
		{
			if (hook.Enter)
				cleanup = hook.Enter(function, instance, editor);
			ActiveHooks.push_back({ std::move(hook), std::move(cleanup) });
			editor.Commit();
		}
		catch (...)
		{
			// VM extensions are auxiliary. The editor restores this hook's
			// replacements and dispatch continues through the remaining hooks.
		}
	}
}

VMCallHookRegistry::CallScope::CallScope(CallScope&& other) noexcept
	: Function(other.Function), Instance(other.Instance), Arguments(other.Arguments),
	ActiveHooks(std::move(other.ActiveHooks)), CleanedUp(other.CleanedUp)
{
	other.CleanedUp = true;
}

VMCallHookRegistry::CallScope::~CallScope() noexcept
{
	RunCleanups();
}

void VMCallHookRegistry::CallScope::ObserveResult(const ExpressionValue& result) noexcept
{
	for (auto it = ActiveHooks.rbegin(); it != ActiveHooks.rend(); ++it)
	{
		if (!it->Hook.ObserveResult)
			continue;
		try
		{
			it->Hook.ObserveResult(Function, Instance, *Arguments, result);
		}
		catch (...)
		{
		}
	}
}

void VMCallHookRegistry::CallScope::RunCleanups() noexcept
{
	if (CleanedUp)
		return;
	CleanedUp = true;

	for (auto it = ActiveHooks.rbegin(); it != ActiveHooks.rend(); ++it)
	{
		if (!it->Cleanup)
			continue;
		try
		{
			it->Cleanup();
		}
		catch (...)
		{
		}
	}
}

VMCallHookHandle VMCallHookRegistry::Register(VMCallHook hook)
{
	const VMCallHookHandle handle = NextHandle++;
	Hooks.push_back({ handle, NextSequence++, std::move(hook) });
	std::stable_sort(Hooks.begin(), Hooks.end(), [](const RegisteredHook& a, const RegisteredHook& b)
	{
		if (a.Hook.Order != b.Hook.Order)
			return a.Hook.Order < b.Hook.Order;
		return a.Sequence < b.Sequence;
	});
	return handle;
}

bool VMCallHookRegistry::Unregister(VMCallHookHandle handle)
{
	auto it = std::find_if(Hooks.begin(), Hooks.end(),
		[handle](const RegisteredHook& hook) { return hook.Handle == handle; });
	if (it == Hooks.end())
		return false;
	Hooks.erase(it);
	return true;
}

void VMCallHookRegistry::Clear()
{
	Hooks.clear();
}

VMCallHookRegistry::CallScope VMCallHookRegistry::BeginCall(UFunction* function,
	UObject* instance, Array<ExpressionValue>& arguments)
{
	std::vector<VMCallHook> snapshot;
	snapshot.reserve(Hooks.size());
	for (const RegisteredHook& hook : Hooks)
		snapshot.push_back(hook.Hook);
	return CallScope(function, instance, arguments, std::move(snapshot));
}
