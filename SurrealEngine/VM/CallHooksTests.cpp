#include "Precomp.h"
#include "VM/CallHooks.h"
#include <iostream>
#include <stdexcept>

namespace
{
	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void TestOrderingRollbackAndResult()
	{
		VMCallHookRegistry registry;
		Array<ExpressionValue> arguments = {
			ExpressionValue::IntValue(1),
			ExpressionValue::IntValue(10),
			ExpressionValue::VectorValue(vec3(100.0f, 101.0f, 102.0f))
		};
		const vec3* untouchedIdentity = &arguments[2].ToVector();
		std::vector<std::string> events;

		VMCallHook first;
		first.Order = 10;
		first.Enter = [&events](UFunction*, UObject*, VMCallArguments& args)
		{
			events.push_back("enter-first");
			Require(args.Replace(0, ExpressionValue::IntValue(2)), "first replacement failed");
			return [&events]() { events.push_back("cleanup-first"); };
		};
		first.ObserveResult = [&events, untouchedIdentity](UFunction*, UObject*, const Array<ExpressionValue>& args,
			const ExpressionValue& result)
		{
			Require(args[0].ToInt() == 2 && args[1].ToInt() == 20, "observer saw wrong arguments");
			Require(&args[2].ToVector() == untouchedIdentity, "untouched argument lost its identity");
			Require(args[2].ToVector() == vec3(200.0f, 201.0f, 202.0f), "observer missed post-dispatch argument state");
			Require(result.ToInt() == 42, "observer saw wrong result");
			events.push_back("result-first");
		};

		VMCallHook failing;
		failing.Order = 20;
		failing.Enter = [&events](UFunction*, UObject*, VMCallArguments& args) -> VMCallHookCleanup
		{
			events.push_back("enter-failing");
			Require(args.Replace(0, ExpressionValue::IntValue(3)), "failing replacement failed");
			Require(args.Replace(0, ExpressionValue::IntValue(4)), "repeat replacement failed");
			throw std::runtime_error("intentional rollback");
		};
		failing.ObserveResult = [&events](UFunction*, UObject*, const Array<ExpressionValue>&,
			const ExpressionValue&) { events.push_back("result-failing"); };

		VMCallHook last;
		last.Order = 30;
		last.Enter = [&events](UFunction*, UObject*, VMCallArguments& args)
		{
			events.push_back("enter-last");
			Require(args.Values()[0].ToInt() == 2, "failed hook did not roll back its replacement");
			Require(args.Replace(1, ExpressionValue::IntValue(20)), "last replacement failed");
			Require(!args.Replace(10, ExpressionValue::IntValue(0)), "out-of-range replacement succeeded");
			return [&events]() { events.push_back("cleanup-last"); };
		};
		last.ObserveResult = [&events](UFunction*, UObject*, const Array<ExpressionValue>&,
			const ExpressionValue&) { events.push_back("result-last"); };

		registry.Register(std::move(last));
		registry.Register(std::move(failing));
		VMCallHookHandle firstHandle = registry.Register(std::move(first));
		{
			auto scope = registry.BeginCall(nullptr, nullptr, arguments);
			Require(arguments[0].ToInt() == 2 && arguments[1].ToInt() == 20 &&
				arguments[2].ToVector() == vec3(100.0f, 101.0f, 102.0f),
				"committed argument replacements were wrong");
			arguments[2].ToType<vec3&>() = vec3(200.0f, 201.0f, 202.0f); // Simulate an out param write.
			scope.ObserveResult(ExpressionValue::IntValue(42));
		}

		const std::vector<std::string> expected = {
			"enter-first", "enter-failing", "enter-last",
			"result-last", "result-first", "cleanup-last", "cleanup-first"
		};
		Require(events == expected, "hook callback order was wrong");
		Require(registry.Unregister(firstHandle), "registered hook could not be removed");
		Require(!registry.Unregister(firstHandle), "removed hook was removed twice");
	}

	void TestNestedCleanupAndExceptionIsolation()
	{
		VMCallHookRegistry registry;
		Array<ExpressionValue> outerArguments;
		Array<ExpressionValue> innerArguments;
		std::vector<std::string> events;

		for (const char* name : { "a", "b" })
		{
			VMCallHook hook;
			hook.Enter = [&events, name](UFunction*, UObject*, VMCallArguments&)
			{
				events.push_back(std::string("enter-") + name);
				return [&events, name]()
				{
					events.push_back(std::string("cleanup-") + name);
					if (name[0] == 'b')
						throw std::runtime_error("intentional cleanup failure");
				};
			};
			hook.ObserveResult = [&events, name](UFunction*, UObject*, const Array<ExpressionValue>&,
				const ExpressionValue&)
			{
				events.push_back(std::string("result-") + name);
				if (name[0] == 'b')
					throw std::runtime_error("intentional observer failure");
			};
			registry.Register(std::move(hook));
		}

		{
			auto outer = registry.BeginCall(nullptr, nullptr, outerArguments);
			{
				auto inner = registry.BeginCall(nullptr, nullptr, innerArguments);
				inner.ObserveResult(ExpressionValue::NothingValue());
			}
			outer.ObserveResult(ExpressionValue::NothingValue());
		}

		const std::vector<std::string> expected = {
			"enter-a", "enter-b", "enter-a", "enter-b",
			"result-b", "result-a", "cleanup-b", "cleanup-a",
			"result-b", "result-a", "cleanup-b", "cleanup-a"
		};
		Require(events == expected, "nested result/cleanup order or exception isolation was wrong");
	}

	void TestSnapshotAndDispatchExceptionCleanup()
	{
		VMCallHookRegistry registry;
		Array<ExpressionValue> arguments;
		int entries = 0;
		int cleanups = 0;
		VMCallHookHandle handle = 0;

		VMCallHook hook;
		hook.Enter = [&](UFunction*, UObject*, VMCallArguments&)
		{
			entries++;
			registry.Unregister(handle);
			return [&]() { cleanups++; };
		};
		handle = registry.Register(std::move(hook));

		try
		{
			auto scope = registry.BeginCall(nullptr, nullptr, arguments);
			Require(entries == 1 && registry.Size() == 0, "registry mutation during entry failed");
			throw std::runtime_error("simulated dispatch failure");
		}
		catch (const std::runtime_error&)
		{
		}

		Require(cleanups == 1, "dispatch exception did not unwind call cleanup");
		{
			auto scope = registry.BeginCall(nullptr, nullptr, arguments);
		}
		Require(entries == 1, "unregistered hook affected a later call");
	}
}

int main()
{
	try
	{
		TestOrderingRollbackAndResult();
		TestNestedCleanupAndExceptionIsolation();
		TestSnapshotAndDispatchExceptionCleanup();
		std::cout << "VM call hook tests passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "VM call hook test failed: " << e.what() << '\n';
		return 1;
	}
}
