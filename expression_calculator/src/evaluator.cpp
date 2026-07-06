#include "evaluator.h"

#include <limits>
#include <vector>

namespace expression_calculator {
namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

class Parser {
public:
    explicit Parser(std::string_view expression)
        : m_begin(expression.data()), m_current(expression.data()), m_end(expression.data() + expression.size()) {}

    std::int64_t parse() {
        return parseExpression();
    }

private:
    enum class Operator {
        Add,
        Subtract,
        Multiply,
        Divide,
        UnaryPlus,
        UnaryMinus,
        OpenParen,
    };

    std::int64_t parseExpression() {
        std::vector<std::int64_t> values;
        std::vector<Operator> operators;
        bool expectingValue = true;

        while (m_current != m_end) {
            skipSpaces();
            if (m_current == m_end) {
                break;
            }

            char token = *m_current;
            if (isDigit(token)) {
                if (!expectingValue) {
                    throwUnexpectedCharacter();
                }
                values.push_back(parseNumber());
                applyPrefixOperators(values, operators);
                expectingValue = false;
            } else if (token == '(') {
                if (!expectingValue) {
                    throwUnexpectedCharacter();
                }
                ++m_current;
                operators.push_back(Operator::OpenParen);
                expectingValue = true;
            } else if (token == ')') {
                if (expectingValue) {
                    throwExpectedNumber();
                }
                closeParenthesizedExpression(values, operators);
                expectingValue = false;
            } else if (token == '+' || token == '-') {
                ++m_current;
                if (expectingValue) {
                    pushUnaryOperator(token == '+' ? Operator::UnaryPlus : Operator::UnaryMinus, operators);
                } else {
                    pushBinaryOperator(token == '+' ? Operator::Add : Operator::Subtract, values, operators);
                    expectingValue = true;
                }
            } else if (token == '*' || token == '/') {
                if (expectingValue) {
                    throwExpectedNumber();
                }
                ++m_current;
                pushBinaryOperator(token == '*' ? Operator::Multiply : Operator::Divide, values, operators);
                expectingValue = true;
            } else {
                throwUnexpectedCharacter();
            }
        }

        if (expectingValue) {
            throwExpectedNumber();
        }

        while (!operators.empty()) {
            if (operators.back() == Operator::OpenParen) {
                throw EvaluationError("missing closing parenthesis");
            }
            applyTopOperator(values, operators);
        }

        if (values.size() != 1) {
            throw EvaluationError("malformed expression");
        }
        return values.back();
    }

    void closeParenthesizedExpression(std::vector<std::int64_t>& values, std::vector<Operator>& operators) {
        size_t closePosition = position();
        ++m_current;

        while (!operators.empty() && operators.back() != Operator::OpenParen) {
            applyTopOperator(values, operators);
        }
        if (operators.empty()) {
            throw EvaluationError("unexpected character at position " + std::to_string(closePosition));
        }
        operators.pop_back();
        applyPrefixOperators(values, operators);
    }

    static void pushBinaryOperator(Operator op, std::vector<std::int64_t>& values, std::vector<Operator>& operators) {
        while (!operators.empty() && operators.back() != Operator::OpenParen &&
               precedence(operators.back()) >= precedence(op)) {
            applyTopOperator(values, operators);
        }
        operators.push_back(op);
    }

    static void pushUnaryOperator(Operator op, std::vector<Operator>& operators) {
        if (op == Operator::UnaryPlus) {
            return;
        }
        if (!operators.empty() && operators.back() == Operator::UnaryMinus) {
            operators.pop_back();
            return;
        }
        operators.push_back(Operator::UnaryMinus);
    }

    static void applyPrefixOperators(std::vector<std::int64_t>& values, std::vector<Operator>& operators) {
        while (!operators.empty() &&
               (operators.back() == Operator::UnaryPlus || operators.back() == Operator::UnaryMinus)) {
            applyTopOperator(values, operators);
        }
    }

    static void applyTopOperator(std::vector<std::int64_t>& values, std::vector<Operator>& operators) {
        Operator op = operators.back();
        operators.pop_back();

        if (op == Operator::UnaryPlus || op == Operator::UnaryMinus) {
            if (values.empty()) {
                throw EvaluationError("malformed expression");
            }
            if (op == Operator::UnaryMinus) {
                values.back() = checkedNegate(values.back());
            }
            return;
        }

        if (values.size() < 2) {
            throw EvaluationError("malformed expression");
        }
        std::int64_t rhs = values.back();
        values.pop_back();
        std::int64_t lhs = values.back();
        values.pop_back();

        switch (op) {
        case Operator::Add:
            values.push_back(checkedAdd(lhs, rhs));
            break;
        case Operator::Subtract:
            values.push_back(checkedSub(lhs, rhs));
            break;
        case Operator::Multiply:
            values.push_back(checkedMul(lhs, rhs));
            break;
        case Operator::Divide:
            if (rhs == 0) {
                throw EvaluationError("division by zero");
            }
            if (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) {
                throw EvaluationError("integer overflow");
            }
            values.push_back(lhs / rhs);
            break;
        default:
            throw EvaluationError("malformed expression");
        }
    }

    static int precedence(Operator op) {
        switch (op) {
        case Operator::UnaryPlus:
        case Operator::UnaryMinus:
            return 3;
        case Operator::Multiply:
        case Operator::Divide:
            return 2;
        case Operator::Add:
        case Operator::Subtract:
            return 1;
        case Operator::OpenParen:
            return 0;
        }
        return 0;
    }

    std::int64_t parseNumber() {
        if (m_current == m_end || !isDigit(*m_current)) {
            throwExpectedNumber();
        }

        std::int64_t value = 0;
        while (m_current != m_end && isDigit(*m_current)) {
            int digit = *m_current - '0';
            if (value > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
                throw EvaluationError("integer overflow");
            }
            value = value * 10 + digit;
            ++m_current;
        }
        return value;
    }

    void skipSpaces() {
        while (m_current != m_end && isSpace(*m_current)) {
            ++m_current;
        }
    }

    size_t position() const {
        return static_cast<size_t>(m_current - m_begin);
    }

    [[noreturn]] void throwExpectedNumber() const {
        throw EvaluationError("expected number at position " + std::to_string(position()));
    }

    [[noreturn]] void throwUnexpectedCharacter() const {
        throw EvaluationError("unexpected character at position " + std::to_string(position()));
    }

    static std::int64_t checkedAdd(std::int64_t lhs, std::int64_t rhs) {
        if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
            throw EvaluationError("integer overflow");
        }
        return lhs + rhs;
    }

    static std::int64_t checkedSub(std::int64_t lhs, std::int64_t rhs) {
        if ((rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs) ||
            (rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs)) {
            throw EvaluationError("integer overflow");
        }
        return lhs - rhs;
    }

    static std::int64_t checkedMul(std::int64_t lhs, std::int64_t rhs) {
        constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
        constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();

        if (lhs == 0 || rhs == 0) {
            return 0;
        }
        if (lhs == 1) {
            return rhs;
        }
        if (rhs == 1) {
            return lhs;
        }
        if (lhs == -1) {
            return checkedNegate(rhs);
        }
        if (rhs == -1) {
            return checkedNegate(lhs);
        }

        if (lhs > 0) {
            if ((rhs > 0 && lhs > max / rhs) || (rhs < 0 && rhs < min / lhs)) {
                throw EvaluationError("integer overflow");
            }
        } else if (lhs < 0) {
            if ((rhs > 0 && lhs < min / rhs) || (rhs < 0 && lhs < max / rhs)) {
                throw EvaluationError("integer overflow");
            }
        }
        return lhs * rhs;
    }

    static std::int64_t checkedNegate(std::int64_t value) {
        if (value == std::numeric_limits<std::int64_t>::min()) {
            throw EvaluationError("integer overflow");
        }
        return -value;
    }

    const char* m_begin;
    const char* m_current;
    const char* m_end;
};

} // namespace

EvaluationError::EvaluationError(const std::string& message) : std::runtime_error(message) {}

std::int64_t evaluateExpression(std::string_view expression) {
    return Parser(expression).parse();
}

} // namespace expression_calculator
