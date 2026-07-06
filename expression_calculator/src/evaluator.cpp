#include "evaluator.h"

#include <limits>

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
        std::int64_t value = parseExpression();
        skipSpaces();
        if (m_current != m_end) {
            throw EvaluationError("unexpected character at position " + std::to_string(position()));
        }
        return value;
    }

private:
    std::int64_t parseExpression() {
        std::int64_t value = parseTerm();
        while (true) {
            skipSpaces();
            if (m_current != m_end && *m_current == '+') {
                ++m_current;
                value = checkedAdd(value, parseTerm());
            } else if (m_current != m_end && *m_current == '-') {
                ++m_current;
                value = checkedSub(value, parseTerm());
            } else {
                return value;
            }
        }
    }

    std::int64_t parseTerm() {
        std::int64_t value = parseFactor();
        while (true) {
            skipSpaces();
            if (m_current != m_end && *m_current == '*') {
                ++m_current;
                value = checkedMul(value, parseFactor());
            } else if (m_current != m_end && *m_current == '/') {
                ++m_current;
                std::int64_t rhs = parseFactor();
                if (rhs == 0) {
                    throw EvaluationError("division by zero");
                }
                if (value == std::numeric_limits<std::int64_t>::min() && rhs == -1) {
                    throw EvaluationError("integer overflow");
                }
                value /= rhs;
            } else {
                return value;
            }
        }
    }

    std::int64_t parseFactor() {
        skipSpaces();
        if (m_current != m_end && *m_current == '+') {
            ++m_current;
            return parseFactor();
        }
        if (m_current != m_end && *m_current == '-') {
            ++m_current;
            return checkedNegate(parseFactor());
        }
        if (m_current != m_end && *m_current == '(') {
            ++m_current;
            std::int64_t value = parseExpression();
            skipSpaces();
            if (m_current == m_end || *m_current != ')') {
                throw EvaluationError("missing closing parenthesis");
            }
            ++m_current;
            return value;
        }
        return parseNumber();
    }

    std::int64_t parseNumber() {
        skipSpaces();
        if (m_current == m_end || !isDigit(*m_current)) {
            throw EvaluationError("expected number at position " + std::to_string(position()));
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
