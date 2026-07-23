#include "Precomp.h"
#include "VM/ExpressionEvaluator.h"
#include "VM/Expression.h"

#include <cstdlib>
#include <iostream>

namespace
{
	void Check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << message << '\n';
			std::exit(1);
		}
	}

	template<typename ArrayExpression>
	void CheckAccessedNoneArray(const char* message)
	{
		NoObjectExpression noObject;
		InstanceVariableExpression unreachableProperty;
		ContextExpression missingContext;
		missingContext.ObjectExpr = &noObject;
		missingContext.ContextExpr = &unreachableProperty;
		IntZeroExpression zero;
		ArrayExpression element;
		element.Index = &zero;
		element.Array = &missingContext;

		ExpressionEvalResult result = ExpressionEvaluator::Eval(
			&element, nullptr, nullptr, nullptr);
		Check(result.Result == StatementResult::AccessedNone &&
			result.Value.GetType() == ExpressionValueType::Nothing, message);
	}
}

int main()
{
	CheckAccessedNoneArray<ArrayElementExpression>(
		"fixed array access through None did not preserve AccessedNone");
	CheckAccessedNoneArray<DynArrayElementExpression>(
		"dynamic array access through None did not preserve AccessedNone");
	std::cout << "Expression evaluator tests passed\n";
	return 0;
}
