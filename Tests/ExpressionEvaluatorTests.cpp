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
	void CheckAccessedNoneArray(bool noneFromIndex, const char* message)
	{
		NoObjectExpression noObject;
		InstanceVariableExpression unreachableProperty;
		ContextExpression missingContext;
		missingContext.ObjectExpr = &noObject;
		missingContext.ContextExpr = &unreachableProperty;
		IntZeroExpression zero;
		ArrayExpression element;
		element.Index = noneFromIndex ? static_cast<Expression*>(&missingContext) : &zero;
		element.Array = noneFromIndex ? static_cast<Expression*>(&noObject) : &missingContext;

		ExpressionEvalResult result = ExpressionEvaluator::Eval(
			&element, nullptr, nullptr, nullptr);
		Check(result.Result == StatementResult::AccessedNone &&
			result.Value.GetType() == ExpressionValueType::Nothing, message);
	}
}

int main()
{
	CheckAccessedNoneArray<ArrayElementExpression>(false,
		"fixed array operand did not preserve AccessedNone");
	CheckAccessedNoneArray<ArrayElementExpression>(true,
		"fixed array index did not preserve AccessedNone");
	CheckAccessedNoneArray<DynArrayElementExpression>(false,
		"dynamic array operand did not preserve AccessedNone");
	CheckAccessedNoneArray<DynArrayElementExpression>(true,
		"dynamic array index did not preserve AccessedNone");
	std::cout << "Expression evaluator tests passed\n";
	return 0;
}
