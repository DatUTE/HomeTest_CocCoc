#include "expression_calculator/evaluator.h"

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
        : begin_(expression.data()), current_(expression.data()), end_(expression.data() + expression.size()) {}

    std::int64_t parse() {
        std::int64_t value = parseExpression();
        skipSpaces();
        if (current_ != end_) {
            throw EvaluationError("unexpected character at position " + std::to_string(position()));
        }
        return value;
    }

private:
    std::int64_t parseExpression() {
        std::int64_t value = parseTerm();
        while (true) {
            skipSpaces();
            if (current_ != end_ && *current_ == '+') {
                ++current_;
                value = checkedAdd(value, parseTerm());
            } else if (current_ != end_ && *current_ == '-') {
                ++current_;
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
            if (current_ != end_ && *current_ == '*') {
                ++current_;
                value = checkedMul(value, parseFactor());
            } else if (current_ != end_ && *current_ == '/') {
                ++current_;
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
        if (current_ != end_ && *current_ == '+') {
            ++current_;
            return parseFactor();
        }
        if (current_ != end_ && *current_ == '-') {
            ++current_;
            return checkedNegate(parseFactor());
        }
        if (current_ != end_ && *current_ == '(') {
            ++current_;
            std::int64_t value = parseExpression();
            skipSpaces();
            if (current_ == end_ || *current_ != ')') {
                throw EvaluationError("missing closing parenthesis");
            }
            ++current_;
            return value;
        }
        return parseNumber();
    }

    std::int64_t parseNumber() {
        skipSpaces();
        if (current_ == end_ || !isDigit(*current_)) {
            throw EvaluationError("expected number at position " + std::to_string(position()));
        }

        std::int64_t value = 0;
        while (current_ != end_ && isDigit(*current_)) {
            int digit = *current_ - '0';
            if (value > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
                throw EvaluationError("integer overflow");
            }
            value = value * 10 + digit;
            ++current_;
        }
        return value;
    }

    void skipSpaces() {
        while (current_ != end_ && isSpace(*current_)) {
            ++current_;
        }
    }

    size_t position() const {
        return static_cast<size_t>(current_ - begin_);
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

    const char* begin_;
    const char* current_;
    const char* end_;
};

} // namespace

EvaluationError::EvaluationError(const std::string& message) : std::runtime_error(message) {}

std::int64_t evaluateExpression(std::string_view expression) {
    return Parser(expression).parse();
}

} // namespace expression_calculator
