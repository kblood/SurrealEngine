#pragma once

#include "ExpressionValue.h"
#include "Iterator.h"

class DebuggerWindow;
class Bytecode;
class UObject;
class UFunction;
class Expression;
struct ExpressionEvalResult;

// Transactional editor used by the pre-dispatch call hook. Reading arguments
// is free; only explicitly replaced elements are copied for rollback. This
// keeps the hook practical on the VM hot path while preserving untouched out-
// parameter identity and exception-safe restoration.
class MutableCallArguments
{
public:
	const Array<ExpressionValue>& Values() const { return Arguments; }
	size_t Size() const { return Arguments.size(); }
	bool Replace(size_t index, ExpressionValue value);

private:
	friend class Frame;
	struct Backup
	{
		size_t Index = 0;
		ExpressionValue Value;
	};

	explicit MutableCallArguments(Array<ExpressionValue>& args) : Arguments(args) { }
	~MutableCallArguments() noexcept;
	void Commit() noexcept { Committed = true; }

	Array<ExpressionValue>& Arguments;
	Array<Backup> Backups;
	bool Committed = false;
};

enum class FrameRunState
{
	Running,
	DebugBreak,
	StepInto,
	StepOver,
	StepOut
};

enum class LatentRunState
{
	Continue,
	Stop,
	Sleep,
	FinishAnim,
	FinishInterpolation,
	MoveTo,
	MoveToward,
	StrafeTo,
	StrafeFacing,
	TurnTo,
	TurnToward,
	WaitForLanding
};

struct Breakpoint
{
	NameString Class;
	NameString Function;
	NameString State;
	Expression* Expr = nullptr;
	UProperty* Property = nullptr; // For watchpoints. Not implemented yet.
	bool Enabled = true;
};

class LocalVariables
{
public:
	LocalVariables(UStruct* func);
	~LocalVariables();

	UStruct* Func = nullptr;
	void* Data = nullptr;
};

class Frame
{
public:
	// Called immediately before an enabled UnrealScript/native function is
	// dispatched. A hook may return a cleanup function for temporary state; the
	// cleanup is run when that exact call scope exits, including during exception
	// unwinding. Nested calls therefore clean up in normal LIFO order.
	//
	// The enter hook must either return its cleanup function or leave external
	// state unchanged if it throws. Cleanup functions must not throw.
	using CallScopeCleanup = std::function<void()>;
	using CallScopeHook = std::function<CallScopeCleanup(UFunction* func, UObject* instance, const Array<ExpressionValue>& args)>;
	// Called after optional arguments are materialized and immediately before
	// dispatch. Hooks inspect the const Values() view and replace only selected
	// elements through Replace(). Out-parameter elements retain their original
	// identity unless the hook explicitly replaces them.
	using MutableCallArgumentsHook = std::function<void(UFunction* func,
		UObject* instance, MutableCallArguments& args)>;
	// Called only after a dispatched call produced a result, while the matching
	// call-scope cleanup is still active. Observer exceptions are swallowed so an
	// auxiliary diagnostic can never replace a VM result or unwind the call.
	using CallResultObserver = std::function<void(UFunction* func, UObject* instance,
		const Array<ExpressionValue>& args, const ExpressionValue& result)>;

	static ExpressionValue Call(UFunction* func, UObject* instance, Array<ExpressionValue> args);
	static void SetCallScopeHook(CallScopeHook hook);
	static void SetMutableCallArgumentsHook(MutableCallArgumentsHook hook);
	static void SetCallResultObserver(CallResultObserver observer);
	static bool RunCallHookSelfTest();
	static std::string GetCallstack();
	static std::string GetDisassembly(Expression* statement);

	static bool AddBreakpoint(const NameString& cls, const NameString& func, const NameString& state = {}, int statementIndex = 0);

	static std::function<void()> RunDebugger;
	static Array<Breakpoint> Breakpoints;
	static Array<Frame*> Callstack;
	static FrameRunState RunState;
	static Frame* StepFrame;
	static Expression* StepExpression;
	static std::string ExceptionText;

	static void Break();
	static void Resume();
	static void StepInto();
	static void StepOver();
	static void StepOut();
	static void ThrowException(const std::string& text);

	static std::unique_ptr<Iterator> CreatedIterator;

	Frame(UObject* instance, UStruct* func);

	void SetState(UStruct* func);

	void GotoLabel(const NameString& label);
	void Tick();

	std::string GetName();

	LatentRunState LatentState = LatentRunState::Continue;

	std::unique_ptr<LocalVariables> Variables;
	UObject* Object = nullptr;
	UStruct* Func = nullptr;
	size_t StatementIndex = 0;
	Array<std::unique_ptr<Iterator>> Iterators;

private:
	ExpressionEvalResult Run();
	void ProcessSwitch(const ExpressionValue& condition);

	static ExpressionValue CallNative(UFunction* func, UObject* instance, Array<ExpressionValue>& args);
	static ExpressionValue CallScript(UFunction* func, UObject* instance, Array<ExpressionValue>& args);
	static void TraceCall(UFunction* func, UObject* instance, const Array<ExpressionValue>& args);
	static CallScopeHook CurrentCallScopeHook;
	static MutableCallArgumentsHook CurrentMutableCallArgumentsHook;
	static CallResultObserver CurrentCallResultObserver;
	static bool ApplyMutableCallArgumentsHook(UFunction* func, UObject* instance,
		Array<ExpressionValue>& args) noexcept;
	static void NotifyCallResultObserver(UFunction* func, UObject* instance,
		const Array<ExpressionValue>& args, const ExpressionValue& result) noexcept;

	struct ActiveCallScopeHook
	{
		ActiveCallScopeHook(UFunction* func, UObject* instance, const Array<ExpressionValue>& args);
		~ActiveCallScopeHook() noexcept;

		ActiveCallScopeHook(const ActiveCallScopeHook&) = delete;
		ActiveCallScopeHook& operator=(const ActiveCallScopeHook&) = delete;

		CallScopeCleanup Cleanup;
	};

	struct ActiveCallStackFrame
	{
		ActiveCallStackFrame(Frame* frame) { Frame::Callstack.push_back(frame); }
		~ActiveCallStackFrame() { Frame::Callstack.pop_back(); }
	};
};
