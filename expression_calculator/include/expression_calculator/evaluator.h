#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace expression_calculator {

/**
 * @brief Error raised when an expression cannot be evaluated.
 *
 * Typical causes are malformed input, division by zero, missing parentheses,
 * unexpected characters, or integer overflow.
 */
class EvaluationError : public std::runtime_error {
public:
    /**
     * @brief Creates an evaluation error with a human-readable message.
     * @param message Description of the evaluation failure.
     */
    explicit EvaluationError(const std::string& message);
};

/**
 * @brief Evaluates an integer arithmetic expression.
 *
 * Supports decimal integers, parentheses, unary plus/minus, and the binary
 * operators +, -, *, and /. Multiplication/division have higher precedence
 * than addition/subtraction. Division uses C++ integer division semantics.
 *
 * @param expression Expression text without the trailing newline.
 * @return The evaluated int64 result.
 * @throws EvaluationError If the input is invalid or evaluation overflows.
 */
std::int64_t evaluateExpression(std::string_view expression);

} // namespace expression_calculator
