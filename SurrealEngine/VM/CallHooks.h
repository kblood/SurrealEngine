#pragma once

#include "ExpressionValue.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class UFunction;
class UObject;

// Provides a const view of a VM call's arguments and explicit, transactional
// replacement of selected arguments. If a hook throws, only replacements made
// by that hook are rolled back. Arguments which are not replaced retain their
// original variable/out-parameter identity.
class VMCallArguments
{
public:
	const Array<ExpressionValue>& Values() const { return Arguments; }
	size_t Size() const { return Arguments.size(); }
	bool Replace(size_t index, ExpressionValue value);

private:
	friend class VMCallHookRegistry;

	struct Backup
	{
		size_t Index = 0;
		ExpressionValue Value;
	};

	explicit VMCallArguments(Array<ExpressionValue>& arguments) : Arguments(arguments) { }
	~VMCallArguments() noexcept;
	void Commit() noexcept { Committed = true; }

	Array<ExpressionValue>& Arguments;
	std::vector<Backup> Backups;
	bool Committed = false;
};

using VMCallHookHandle = uint64_t;
using VMCallHookCleanup = std::function<void()>;

struct VMCallHook
{
	// Enter runs before native or script dispatch. It may replace selected
	// arguments and return cleanup work for temporary external state. A thrown
	// exception rolls back this hook's argument replacements and disables its
	// result/cleanup callbacks for the current call. An Enter callback which
	// changes external state must restore it itself if it throws before returning.
	std::function<VMCallHookCleanup(UFunction*, UObject*, VMCallArguments&)> Enter;

	// ObserveResult runs only when dispatch returned normally. It runs before
	// Cleanup and receives the post-dispatch argument state, including out params.
	std::function<void(UFunction*, UObject*, const Array<ExpressionValue>&, const ExpressionValue&)> ObserveResult;

	// Lower order values enter first. Equal order values use registration order.
	int Order = 0;
};

// Ordered registry for optional VM extensions. A call snapshots the registered
// hooks when it begins, allowing callbacks to safely register or unregister
// hooks without invalidating the current call. The VM executes on one thread;
// registration and dispatch are consequently not thread-safe.
class VMCallHookRegistry
{
public:
	class CallScope
	{
	public:
		CallScope(CallScope&& other) noexcept;
		~CallScope() noexcept;

		CallScope(const CallScope&) = delete;
		CallScope& operator=(const CallScope&) = delete;
		CallScope& operator=(CallScope&&) = delete;

		// Observers run in reverse entry order, like nested middleware. Exceptions
		// from observers are isolated and do not change the VM result.
		void ObserveResult(const ExpressionValue& result) noexcept;

	private:
		friend class VMCallHookRegistry;

		struct ActiveHook
		{
			VMCallHook Hook;
			VMCallHookCleanup Cleanup;
		};

		CallScope(UFunction* function, UObject* instance, Array<ExpressionValue>& arguments,
			std::vector<VMCallHook> hooks);
		void RunCleanups() noexcept;

		UFunction* Function = nullptr;
		UObject* Instance = nullptr;
		Array<ExpressionValue>* Arguments = nullptr;
		std::vector<ActiveHook> ActiveHooks;
		bool CleanedUp = false;
	};

	VMCallHookHandle Register(VMCallHook hook);
	bool Unregister(VMCallHookHandle handle);
	void Clear();
	CallScope BeginCall(UFunction* function, UObject* instance, Array<ExpressionValue>& arguments);
	size_t Size() const { return Hooks.size(); }

private:
	struct RegisteredHook
	{
		VMCallHookHandle Handle = 0;
		uint64_t Sequence = 0;
		VMCallHook Hook;
	};

	std::vector<RegisteredHook> Hooks;
	VMCallHookHandle NextHandle = 1;
	uint64_t NextSequence = 0;
};
