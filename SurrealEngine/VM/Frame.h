#pragma once

#include "ExpressionValue.h"
#include "Iterator.h"

class DebuggerWindow;
class Bytecode;
class UObject;
class UFunction;
class Expression;
struct ExpressionEvalResult;

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
	static ExpressionValue Call(UFunction* func, UObject* instance, Array<ExpressionValue> args);
	static std::string GetCallstack();
	static std::string GetDisassembly(Expression* statement);

	static bool AddBreakpoint(const NameString& cls, const NameString& func, const NameString& state = {}, int statementIndex = 0);

	static std::function<void()> RunDebugger;

	// M-B VM interception seam (Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's
	// M-B section) - consulted at the top of Frame::Call(), just before it
	// decides native vs. script dispatch. Returns true if it fully handled
	// the call itself (`result` becomes Frame::Call's return value, the
	// normal dispatch is skipped entirely) or false to continue normal
	// dispatch completely unchanged. Null by default, which makes
	// Frame::Call byte-identical to its pre-M-B behavior - verified by the
	// no-VR/no-`--debugvrhands` regression run producing zero OpenXR log
	// lines and an otherwise unchanged flatscreen session. `Engine` is the
	// only installer (only while a VR session - real or `--debugvrhands` -
	// is active); this VM module has zero dependency on Engine or on what
	// the hook does - all name/instance filtering is the installer's
	// business, not the VM's.
	using FrameCallHook = std::function<bool(UObject* instance, UFunction* func, Array<ExpressionValue>& args, ExpressionValue& result)>;
	static FrameCallHook InterceptCall;
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

	static ExpressionValue CallNative(UFunction* func, UObject* instance, Array<ExpressionValue> args);
	static ExpressionValue CallScript(UFunction* func, UObject* instance, Array<ExpressionValue> args);
	static void TraceCall(UFunction* func, UObject* instance, const Array<ExpressionValue>& args);

	struct ActiveCallStackFrame
	{
		ActiveCallStackFrame(Frame* frame) { Frame::Callstack.push_back(frame); }
		~ActiveCallStackFrame() { Frame::Callstack.pop_back(); }
	};
};
