#include "evaluator.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using expression_calculator::EvaluationError;
using expression_calculator::evaluateExpression;

namespace {

void expectValue(std::string_view expression, std::int64_t expected) {
    std::int64_t actual = evaluateExpression(expression);
    if (actual != expected) {
        std::cerr << "FAILED: " << expression << " expected " << expected << " got " << actual << "\n";
        std::abort();
    }
}

void expectError(std::string_view expression) {
    try {
        (void)evaluateExpression(expression);
    } catch (const EvaluationError&) {
        return;
    }
    std::cerr << "FAILED: expected error for " << expression << "\n";
    std::abort();
}

} // namespace

int main() {
    expectValue("0", 0);
    expectValue("42", 42);
    expectValue("3+2*4", 11);
    expectValue("(3+2*4)*7", 77);
    expectValue("20/3", 6);
    expectValue("20-3-4", 13);
    expectValue("2*(3+4*(5-2))", 30);
    expectValue("-5+2", -3);
    expectValue("--5", 5);
    expectValue(" 1 + 2 * 3 \r", 7);

    expectError("");
    expectError("1+");
    expectError("(1+2");
    expectError("1/0");
    expectError("9223372036854775808");
    expectError("9223372036854775807+1");
    expectError("abc");

    std::string unaryRun(10000, '-');
    unaryRun += "5";
    expectValue(unaryRun, 5);

    std::string oddUnaryRun(10001, '-');
    oddUnaryRun += "5";
    expectValue(oddUnaryRun, -5);

    std::string nestedParens(5000, '(');
    nestedParens += "5";
    nestedParens.append(5000, ')');
    expectValue(nestedParens, 5);

    std::cout << "all evaluator tests passed\n";
    return 0;
}
