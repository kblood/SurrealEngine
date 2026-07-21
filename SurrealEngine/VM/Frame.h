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

	// M-C VM interception seam addition
	// (Docs/VR/CONTROLLER_AIM_WEAPON_PLAN.md's M-C section). InterceptCall
	// alone only supports two shapes: "fully replace this call" (return
	// true - what M-B's RenderOverlays intercept does) or "leave completely
	// alone" (return false). M-C's fire-scoped ViewRotation swap needs a
	// third shape InterceptCall cannot express: let the ORIGINAL script body
	// actually run (unlike M-B's InvCalcView/RenderOverlays intercept, which
	// deliberately skips it), while still running native code immediately
	// before AND after it (save/swap ViewRotation before TraceFire/
	// ProjectileFire's real dispatch, restore it after). Re-reading
	// Frame::Call confirmed InterceptCall's single return-value contract has
	// no way to say "proceed, but call me back when done" - so this adds a
	// second, symmetric hook consulted once at the very end of Frame::Call(),
	// right after the native/script dispatch has produced its result and
	// immediately before Call() returns, for EVERY call (unconditionally,
	// same null-by-default/zero-cost-when-unset contract as InterceptCall).
	// It receives the exact same (instance, func) Frame::Call was entered
	// with - those two locals are untouched by whatever the dispatch
	// recursed into internally, so a pre-hook/post-hook pair for the SAME
	// invocation is trivially identifiable by the installer re-checking its
	// own name/instance filter in both hooks, with no extra stack or token
	// needed in the VM itself (the C++ call stack's natural LIFO nesting
	// already guarantees correct pairing under recursion/re-entrancy - see
	// Engine::HandleFrameCallInterceptPost's doc comment for how the Engine
	// side additionally keeps its own small save/restore stack for the
	// ViewRotation values themselves, which DO need explicit re-entrancy
	// handling since multiple nested fire calls could in principle occur).
	using FrameCallPostHook = std::function<void(UObject* instance, UFunction* func, ExpressionValue& result)>;
	static FrameCallPostHook InterceptCallPost;
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
