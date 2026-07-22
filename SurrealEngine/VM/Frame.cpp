
#include "Precomp.h"
#include "Frame.h"
#include "Bytecode.h"
#include "ExpressionEvaluator.h"
#include "NativeFunc.h"
#include "UObject/UTextBuffer.h"
#include "UObject/USubsystem.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Utils/AlignedAlloc.h"
#include "Commandlet/VM/DisassemblyCommandlet.h"
#include <cstring>

std::function<void()> Frame::RunDebugger;
Array<Breakpoint> Frame::Breakpoints;
Array<Frame*> Frame::Callstack;
FrameRunState Frame::RunState = FrameRunState::Running;
Frame* Frame::StepFrame = nullptr;
Expression* Frame::StepExpression = nullptr;
std::string Frame::ExceptionText;
std::unique_ptr<Iterator> Frame::CreatedIterator;
Frame::CallScopeHook Frame::CurrentCallScopeHook;
Frame::MutableCallArgumentsHook Frame::CurrentMutableCallArgumentsHook;
Frame::CallResultObserver Frame::CurrentCallResultObserver;

bool MutableCallArguments::Replace(size_t index, ExpressionValue value)
{
	if (index >= Arguments.size())
		return false;
	for (const Backup& backup : Backups)
	{
		if (backup.Index == index)
		{
			Arguments[index] = std::move(value);
			return true;
		}
	}
	Backups.push_back({ index, Arguments[index] });
	Arguments[index] = std::move(value);
	return true;
}

MutableCallArguments::~MutableCallArguments() noexcept
{
	if (Committed)
		return;
	for (auto it = Backups.rbegin(); it != Backups.rend(); ++it)
	{
		try { Arguments[it->Index] = std::move(it->Value); } catch (...) { }
	}
}

Frame::Frame(UObject* instance, UStruct* func)
{
	Object = instance;
	SetState(func);
}

void Frame::SetState(UStruct* func)
{
	Func = func;
	Variables = std::make_unique<LocalVariables>(func);
}

bool Frame::AddBreakpoint(const NameString& clsName, const NameString& funcName, const NameString& stateName, int statementIndex)
{
	Breakpoint bp;
	bp.Class = clsName;
	bp.Function = funcName;
	bp.State = stateName;

	UClass* cls = engine->packages->FindClass(clsName);
	if (!cls)
		return false;

	if (stateName.IsNone())
	{
		for (UField* child = cls->Children; child; child = child->Next)
		{
			if (child->Name == funcName && UObject::IsType<UFunction>(child))
			{
				UFunction* func = UObject::Cast<UFunction>(child);
				if (statementIndex < 0 || (size_t)statementIndex >= func->Code->Statements.size())
					return false;
				bp.Expr = func->Code->Statements[statementIndex];
				Breakpoints.push_back(bp);
				return true;
			}
		}
	}
	else
	{
		for (UField* child = cls->Children; child; child = child->Next)
		{
			if (child->Name == stateName && UObject::IsType<UState>(child))
			{
				UState* state = UObject::Cast<UState>(child);
				for (UField* stateChild = state->Children; stateChild; stateChild = stateChild->Next)
				{
					if (stateChild->Name == funcName && UObject::IsType<UFunction>(stateChild))
					{
						UFunction* func = UObject::Cast<UFunction>(stateChild);
						if (statementIndex < 0 || (size_t)statementIndex >= func->Code->Statements.size())
							return false;
						bp.Expr = func->Code->Statements[statementIndex];
						Breakpoints.push_back(bp);
						return true;
					}
				}
			}
		}
	}
	return false;
}

void Frame::Break()
{
	RunState = FrameRunState::DebugBreak;

	if (RunDebugger)
	{
		engine->audiodev->BreakpointTriggered();
		RunDebugger();
	}
	else
	{
		if (!ExceptionText.empty())
		{
			std::string callstack = Frame::GetCallstack();
			std::string message = "Script execution error:\r\n\r\n";
			message += ExceptionText;
			message += "\r\n\r\nCall stack:\r\n\r\n" + callstack;
			Exception::Throw(message);
		}
	}
}

void Frame::Resume()
{
	RunState = FrameRunState::Running;
}

void Frame::StepInto()
{
	StepFrame = Callstack.back();
	RunState = FrameRunState::StepInto;
}

void Frame::StepOver()
{
	StepFrame = Callstack.back();
	RunState = FrameRunState::StepOver;
}

void Frame::StepOut()
{
	StepFrame = Callstack.back();
	RunState = FrameRunState::StepOut;
}

void Frame::ThrowException(const std::string& text)
{
#if defined(_DEBUG) && defined(WIN32)
	DebugBreak();
#endif

	ExceptionText = text;
	Break();
}

std::string debugCallstack;
const char* GetCallStack()
{
	debugCallstack = Frame::GetCallstack();
	return debugCallstack.c_str();
}

std::string Frame::GetName()
{
	std::string name;
	if (Func)
	{
		for (UStruct* s = Func; s != nullptr; s = s->StructParent)
		{
			if (name.empty())
				name = s->Name.ToString();
			else
				name = s->Name.ToString() + "." + name;
		}
	}
	return name;
}

std::string Frame::GetDisassembly(Expression* statement)
{
	std::string result;
	PrintPrettyExpression::Print([&](const std::string& text) { result += text; }, statement);
	return result;
}

std::string Frame::GetCallstack()
{
	std::string result;

#ifdef WIN32
	std::string newline = "\r\n";
#else
	std::string newline = "\n";
#endif

	for (auto it = Callstack.rbegin(); it != Callstack.rend(); ++it)
	{
		Frame* frame = *it;
		std::string name = frame->GetName();
		if (UStruct* func = frame->Func)
		{
			name += " line " + std::to_string(func->Line);

			if (frame->StatementIndex > 0) // StatementIndex points at the NEXT statement to be executed
			{
				name += ": ";
				name += GetDisassembly(func->Code->Statements[frame->StatementIndex - 1]);
			}
		}
		if (!result.empty()) result += newline;
		result += "at " + name;
	}
	return result;
}

ExpressionValue Frame::Call(UFunction* func, UObject* instance, Array<ExpressionValue> args)
{
	if (!instance)
	{
		LogMessage("Accessed None when calling " + func->Name.ToString());
		LogMessage(Frame::GetCallstack());
		return ExpressionValue::NothingValue();
	}

	TraceCall(func, instance, args);

	if (!instance->IsEventEnabled(func->Name))
	{
		return ExpressionValue::NothingValue();
	}

	// Trailing optional args may be missing. Add nothing values so the args list matches the function signature.
	int argindex = 0;
	for (UField* field = func->Children; field != nullptr; field = field->Next)
	{
		UProperty* prop = UObject::TryCast<UProperty>(field);
		if (prop)
		{
			if (argindex == args.size() && AllFlags(prop->PropFlags, PropertyFlags::Parm | PropertyFlags::OptionalParm))
				args.push_back(ExpressionValue::NothingValue());

			if (AllFlags(prop->PropFlags, PropertyFlags::Parm))
				argindex++;
		}
	}

	ActiveCallScopeHook callScope(func, instance, args);
	ApplyMutableCallArgumentsHook(func, instance, args);

	ExpressionValue result;
	if (AllFlags(func->FuncFlags, FunctionFlags::Native))
	{
		result = CallNative(func, instance, args);
	}
	else
	{
		result = CallScript(func, instance, args);
	}
	NotifyCallResultObserver(func, instance, args, result);
	return result;
}

void Frame::SetCallScopeHook(CallScopeHook hook)
{
	CurrentCallScopeHook = std::move(hook);
}

void Frame::SetMutableCallArgumentsHook(MutableCallArgumentsHook hook)
{
	CurrentMutableCallArgumentsHook = std::move(hook);
}

void Frame::SetCallResultObserver(CallResultObserver observer)
{
	CurrentCallResultObserver = std::move(observer);
}

bool Frame::ApplyMutableCallArgumentsHook(UFunction* func, UObject* instance,
	Array<ExpressionValue>& args) noexcept
{
	MutableCallArgumentsHook hook = CurrentMutableCallArgumentsHook;
	if (!hook)
		return true;
	MutableCallArguments editor(args);
	try
	{
		hook(func, instance, editor);
		editor.Commit();
		return true;
	}
	catch (...)
	{
		return false;
	}
}

void Frame::NotifyCallResultObserver(UFunction* func, UObject* instance,
	const Array<ExpressionValue>& args, const ExpressionValue& result) noexcept
{
	CallResultObserver observer = CurrentCallResultObserver;
	if (!observer)
		return;
	try
	{
		observer(func, instance, args, result);
	}
	catch (...)
	{
	}
}

bool Frame::RunCallHookSelfTest()
{
	CallScopeHook savedScope = CurrentCallScopeHook;
	MutableCallArgumentsHook savedMutable = CurrentMutableCallArgumentsHook;
	CallResultObserver savedObserver = CurrentCallResultObserver;
	struct RestoreHooks
	{
		CallScopeHook Scope;
		MutableCallArgumentsHook Mutable;
		CallResultObserver Observer;
		~RestoreHooks()
		{
			Frame::CurrentCallScopeHook = std::move(Scope);
			Frame::CurrentMutableCallArgumentsHook = std::move(Mutable);
			Frame::CurrentCallResultObserver = std::move(Observer);
		}
	} restore{ std::move(savedScope), std::move(savedMutable), std::move(savedObserver) };

	Array<ExpressionValue> args = {
		ExpressionValue::VectorValue(vec3(1.0f, 2.0f, 3.0f)),
		ExpressionValue::VectorValue(vec3(4.0f, 5.0f, 6.0f)),
		ExpressionValue::VectorValue(vec3(7.0f, 8.0f, 9.0f)),
		ExpressionValue::VectorValue(vec3(10.0f, 11.0f, 12.0f))
	};
	const vec3* firstOutIdentity = &args[0].ToVector();
	const vec3* secondOutIdentity = &args[1].ToVector();
	CurrentMutableCallArgumentsHook = [](UFunction*, UObject*, MutableCallArguments& values)
	{
		values.Replace(2, ExpressionValue::VectorValue(vec3(13.0f, 14.0f, 15.0f)));
		values.Replace(3, ExpressionValue::VectorValue(vec3(16.0f, 17.0f, 18.0f)));
	};
	if (!ApplyMutableCallArgumentsHook(nullptr, nullptr, args) ||
		&args[0].ToVector() != firstOutIdentity ||
		&args[1].ToVector() != secondOutIdentity ||
		args[0].ToVector() != vec3(1.0f, 2.0f, 3.0f) ||
		args[1].ToVector() != vec3(4.0f, 5.0f, 6.0f) ||
		args[2].ToVector() != vec3(13.0f, 14.0f, 15.0f) ||
		args[3].ToVector() != vec3(16.0f, 17.0f, 18.0f))
		return false;

	CurrentMutableCallArgumentsHook = [](UFunction*, UObject*, MutableCallArguments& values)
	{
		values.Replace(2, ExpressionValue::VectorValue(vec3(19.0f, 20.0f, 21.0f)));
		throw std::runtime_error("intentional hook rollback test");
	};
	if (ApplyMutableCallArgumentsHook(nullptr, nullptr, args) ||
		args[0].ToVector() != vec3(1.0f, 2.0f, 3.0f) ||
		args[1].ToVector() != vec3(4.0f, 5.0f, 6.0f) ||
		args[2].ToVector() != vec3(13.0f, 14.0f, 15.0f) ||
		args[3].ToVector() != vec3(16.0f, 17.0f, 18.0f))
		return false;

	bool observed = false;
	CurrentCallResultObserver = [&observed](UFunction*, UObject*,
		const Array<ExpressionValue>& values, const ExpressionValue& result)
	{
		observed = values.size() == 4 && result.ToInt() == 42;
	};
	NotifyCallResultObserver(nullptr, nullptr, args, ExpressionValue::IntValue(42));
	if (!observed)
		return false;

	CurrentCallResultObserver = [](UFunction*, UObject*, const Array<ExpressionValue>&,
		const ExpressionValue&)
	{
		throw std::runtime_error("intentional observer isolation test");
	};
	NotifyCallResultObserver(nullptr, nullptr, args, ExpressionValue::IntValue(42));
	return true;
}

Frame::ActiveCallScopeHook::ActiveCallScopeHook(UFunction* func, UObject* instance, const Array<ExpressionValue>& args)
{
	// Copy first so replacing/clearing the global hook from inside the callback
	// cannot invalidate the callback currently being invoked.
	CallScopeHook hook = CurrentCallScopeHook;
	if (hook)
		Cleanup = hook(func, instance, args);
}

Frame::ActiveCallScopeHook::~ActiveCallScopeHook() noexcept
{
	if (!Cleanup)
		return;

	// A cleanup hook is an auxiliary observer/restorer and must never replace an
	// exception already unwinding from the VM call. The public contract requires
	// noexcept cleanup; this catch also protects the core VM from a bad hook.
	try
	{
		Cleanup();
	}
	catch (...)
	{
	}
}

ExpressionValue Frame::CallScript(UFunction* func, UObject* instance, Array<ExpressionValue>& args)
{
	Frame frame(instance, func);

	// Store args in function frame local variables
	int argindex = 0;
	for (UField* field = func->Children; field != nullptr; field = field->Next)
	{
		UProperty* prop = UObject::TryCast<UProperty>(field);
		if (prop)
		{
			ExpressionValue lvalue = ExpressionValue::Variable(frame.Variables->Data, prop);
			if (AllFlags(prop->PropFlags, PropertyFlags::Parm))
			{
				if (argindex < args.size())
				{
					lvalue.Store(args[argindex]);
				}

				argindex++;
			}
		}
	}

	// Run the function
	ExpressionValue result = frame.Run().Value;

	// Load the result from the frame local result variable
	result.Load();

	// Copy out params from frame local variables
	argindex = 0;
	for (UField* field = func->Children; field != nullptr; field = field->Next)
	{
		UProperty* prop = UObject::TryCast<UProperty>(field);
		if (prop)
		{
			ExpressionValue lvalue = ExpressionValue::Variable(frame.Variables->Data, prop);

			if (AllFlags(prop->PropFlags, PropertyFlags::Parm | PropertyFlags::OutParm) && argindex < args.size())
			{
				args[argindex].Store(lvalue);
			}

			if (AllFlags(prop->PropFlags, PropertyFlags::ReturnParm) && result.GetType() == ExpressionValueType::Nothing)
			{
				result = ExpressionValue::DefaultValue(prop);
			}

			if (AllFlags(prop->PropFlags, PropertyFlags::Parm))
				argindex++;
		}
	}

	return result;
}

ExpressionValue Frame::CallNative(UFunction* func, UObject* instance, Array<ExpressionValue>& args)
{
	// Native functions expect the last parameter to be the return value
	bool returnparmfound = false;
	int argindex = 0;
	for (UField* field = func->Children; field != nullptr; field = field->Next)
	{
		UProperty* prop = UObject::TryCast<UProperty>(field);
		if (prop)
		{
			if (AllFlags(prop->PropFlags, PropertyFlags::Parm | PropertyFlags::ReturnParm))
			{
				ExpressionValue retval = ExpressionValue::PropertyValue(prop);
				args.push_back(std::move(retval));
				returnparmfound = true;
			}
			if (AllFlags(prop->PropFlags, PropertyFlags::Parm))
				argindex++;
		}
	}

	if (func->NativeFuncIndex != 0)
	{
		auto& callback = NativeFunctions::NativeByIndex[func->NativeFuncIndex];
		if (callback)
		{
			Frame frame(instance, func);
			ActiveCallStackFrame activeFrame(&frame);
			try
			{
				callback(instance, args.data());
			}
			catch (const std::exception& e)
			{
				LogMessage(std::string("Script error: ") + e.what());
				return ExpressionValue::NothingValue();
			}
			catch (...)
			{
				LogMessage("Script error: Unknown error");
				return ExpressionValue::NothingValue();
			}
		}
		else
		{
			Exception::Throw("Unknown native function " + func->NativeStruct->Name.ToString() + "." + func->Name.ToString());
		}
	}
	else
	{
		auto& callback = NativeFunctions::NativeByName[{ func->Name, func->NativeStruct->Name }];
		if (callback)
		{
			Frame frame(instance, func);
			ActiveCallStackFrame activeFrame(&frame);
			try
			{
				callback(instance, args.data());
			}
			catch (const std::exception& e)
			{
				LogMessage(std::string("Script error: ") + e.what());
				return ExpressionValue::NothingValue();
			}
			catch (...)
			{
				LogMessage("Script error: Unknown error");
				return ExpressionValue::NothingValue();
			}
		}
		else
		{
			Exception::Throw("Unknown native function " + func->NativeStruct->Name.ToString() + "." + func->Name.ToString());
		}
	}

	if (!returnparmfound)
		return ExpressionValue::NothingValue();
	ExpressionValue result = std::move(args.back());
	args.pop_back();
	return result;
}

void Frame::TraceCall(UFunction* func, UObject* instance, const Array<ExpressionValue>& args)
{
#if 0 // To do: create a commandlet that lets us do this
	static NameString TraceActorClass = "CTFGame";
	static NameString TraceActorFunc = "PostBeginPlay";
	if (instance->Class->Name == TraceActorClass && func->Name == TraceActorFunc)
	{
		LogMessage("RemainingBots=" + std::to_string(instance->GetInt("RemainingBots")));
		LogMessage("InitialBots=" + std::to_string(instance->GetInt("InitialBots")));
		std::string traceMessage = "Called " + func->Name.ToString() + "(";
		bool first = true;
		for (auto& arg : args)
		{
			if (first)
				first = false;
			else
				traceMessage += ", ";
			if (arg.GetType() == ExpressionValueType::ValueString)
			{
				traceMessage += "\"";
				traceMessage += arg.ToString();
				traceMessage += "\"";
			}
			else if (arg.GetType() == ExpressionValueType::ValueName)
			{
				traceMessage += "'";
				traceMessage += arg.ToName().ToString();
				traceMessage += "'";
			}
			else if (arg.GetType() == ExpressionValueType::ValueBool)
			{
				traceMessage += "'";
				traceMessage += arg.ToBool() ? "true" : "false";
				traceMessage += "'";
			}
			else if (arg.GetType() == ExpressionValueType::ValueObject)
			{
				traceMessage += arg.ToObject() ? UObject::GetUClassName(arg.ToObject()).ToString() : "null";
			}
			else if (arg.GetType() == ExpressionValueType::Nothing)
			{
				traceMessage += "None";
			}
			else
			{
				traceMessage += "?";
			}
		}
		traceMessage += ")";
		LogMessage(traceMessage);
	}
#endif
}

void Frame::GotoLabel(const NameString& label)
{
	for (UClass* cls = Object->Class; cls != nullptr; cls = static_cast<UClass*>(cls->BaseStruct))
	{
		UState* state = cls->GetState(Func->Name);
		if (state)
		{
			int labelIndex = state->Code->FindLabelIndex(label.IsNone() ? NameString("Begin") : label);
			if (labelIndex != -1)
			{
				Func = state;
				StatementIndex = labelIndex;
				LatentState = LatentRunState::Continue;
				return;
			}
		}
	}
	LatentState = LatentRunState::Stop;
}

void Frame::Tick()
{
	if (LatentState == LatentRunState::Continue)
		Run();
}

ExpressionEvalResult Frame::Run()
{
	if (!Func)
		return {};

	ActiveCallStackFrame activeFrame(this);

	if (!Func->Code->Statements.empty())
		StepExpression = Func->Code->Statements[StatementIndex];

	if (RunState == FrameRunState::StepInto)
	{
		// We entered a new function. Break for step into.
		Break();
	}

	const int maxInstructions = 500'000;
	int instructionsRetired = 0;
	while (true)
	{
		if (StatementIndex >= Func->Code->Statements.size())
			ThrowException("Unexpected end of code statements");

		// Note: GotoState may change StatementIndex (jump to a different location) so we have to increment the index before executing the statement
		size_t curStatementIndex = StatementIndex;
		StatementIndex++;

		StepExpression = Func->Code->Statements[curStatementIndex];

		if (instructionsRetired >= maxInstructions)
		{
			LogMessage("Too many VM instructions executed in a single tick");
			Break();
		}
		else if ((RunState == FrameRunState::StepOver || RunState == FrameRunState::StepInto) && StepFrame == this)
		{
			// We are running a new expression. Break on step over, but also step into as there might not been a function to step into.
			Break();
		}
		else if (RunState == FrameRunState::StepOut && StepFrame == nullptr)
		{
			// We found the function exit point. Break the debugger.
			Break();
		}

		Expression* statement = Func->Code->Statements[curStatementIndex];
		ExpressionEvalResult result = ExpressionEvaluator::Eval(statement, Object, Object, Variables->Data);
		if (!Func)
			return result;

		switch (result.Result)
		{
		case StatementResult::Next:
			break;
		case StatementResult::Jump:
			StatementIndex = Func->Code->FindStatementIndex(result.JumpAddress);
			break;
		case StatementResult::Switch:
			ProcessSwitch(result.Value);
			break;
		case StatementResult::GotoLabel:
			{
				int index = Func->Code->FindLabelIndex(result.Label);
				if (index != -1)
				{
					StatementIndex = index;
				}
				else
				{
					// State gotos can jump to a parent state block!
					bool found = false;
					for (UClass* cls = Object->Class; cls != nullptr; cls = static_cast<UClass*>(cls->BaseStruct))
					{
						UState* state = cls->GetState(Func->Name);
						if (state)
						{
							int labelIndex = state->Code->FindLabelIndex(result.Label);
							if (labelIndex != -1)
							{
								Func = state;
								StatementIndex = labelIndex;
								found = true;
								break;
							}
						}
					}
					if (!found)
						ThrowException("Could not find label: " + result.Label.ToString());
				}
			}
			break;
		case StatementResult::Stop:
			LatentState = LatentRunState::Stop;
			return result;
		case StatementResult::Return:
			// Package 61 and earlier transfered the return value in an out parameter
			if (!static_cast<ReturnExpression*>(statement)->Value)
			{
				for (UField* field = Func->Children; field != nullptr; field = field->Next)
				{
					UProperty* prop = UObject::TryCast<UProperty>(field);
					if (prop && AllFlags(prop->PropFlags, PropertyFlags::Parm | PropertyFlags::ReturnParm))
					{
						result.Value = ExpressionValue::Variable(Variables->Data, prop);
						result.Value.Load();
						break;
					}
				}
			}

			if (RunState == FrameRunState::StepOut && StepFrame == this)
			{
				// We are exiting the function. Break on next instruction by requesting a break on next instruction.
				StepFrame = nullptr;
			}
			return result;
		case StatementResult::Iterator:
			if (!result.Iter)
				ThrowException("Iterator statement without an iterator in " + Object->Name.ToString() + "." + Func->Name.ToString());
			Iterators.push_back(std::move(result.Iter));
			Iterators.back()->StartStatementIndex = curStatementIndex + 1;
			Iterators.back()->EndStatementIndex = Func->Code->FindStatementIndex(result.JumpAddress);
			if (Iterators.back()->Next())
				StatementIndex = Iterators.back()->StartStatementIndex;
			else
				StatementIndex = Iterators.back()->EndStatementIndex;
			break;
		case StatementResult::IteratorNext:
			if (Iterators.empty())
				ThrowException("Iterator next statement without an iterator in " + Object->Name.ToString() + "." + Func->Name.ToString());
			if (Iterators.back()->Next())
				StatementIndex = Iterators.back()->StartStatementIndex;
			else
				StatementIndex = Iterators.back()->EndStatementIndex;
			break;
		case StatementResult::IteratorPop:
			if (Iterators.empty())
				ThrowException("Iterator pop statement without an iterator in " + Object->Name.ToString() + "." + Func->Name.ToString());
			Iterators.pop_back();
			break;
		case StatementResult::AccessedNone:
			LogMessage("Accessed None");
			LogMessage(Frame::GetCallstack());
			break;
		}

		if (Object->StateFrame.get() == this && LatentState != LatentRunState::Continue)
		{
			return result;
		}

		instructionsRetired++;
	}
}

void Frame::ProcessSwitch(const ExpressionValue& condition)
{
	SwitchExpression* switchexpr = static_cast<SwitchExpression*>(Func->Code->Statements[StatementIndex - 1]);
	while (true)
	{
		CaseExpression* caseexpr = static_cast<CaseExpression*>(Func->Code->Statements[StatementIndex++]);
		if (caseexpr->Value)
		{
			ExpressionValue casevalue = ExpressionEvaluator::Eval(caseexpr->Value, Object, Object, Variables->Data).Value;
			if (condition.IsEqual(casevalue))
				break;
			else
				StatementIndex = Func->Code->FindStatementIndex(caseexpr->NextOffset);
		}
		else
		{
			break;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////

LocalVariables::LocalVariables(UStruct* func) : Func(func)
{
	if (func)
	{
		Data = AlignedAlloc(func->StructAlignment, func->StructSize);
		// UnrealScript guarantees locals (and unset out-params) default to
		// zero/false/None. AlignedAlloc returns uninitialized memory and
		// ConstructElement is a no-op for POD types (bool/byte/int/float),
		// so without this the value is whatever garbage this memory
		// previously held - observed causing platform-dependent behavior.
		memset(Data, 0, func->StructSize);

		for (UProperty* prop : func->Properties)
		{
			prop->ConstructArray(static_cast<uint8_t*>(Data) + prop->DataOffset.DataOffset);
		}
	}
}

LocalVariables::~LocalVariables()
{
	if (Func && Data)
	{
		for (UProperty* prop : Func->Properties)
		{
			prop->DestructArray(static_cast<uint8_t*>(Data) + prop->DataOffset.DataOffset);
		}
	}

	AlignedFree(Data);
}
