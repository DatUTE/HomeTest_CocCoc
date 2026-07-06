#include "evaluator.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {

struct Generator {
    std::mt19937 rng;
    int maxDepth = 4;

    std::string number() {
        std::uniform_int_distribution<int> dist(0, 1000);
        return std::to_string(dist(rng));
    }

    std::string expression(int depth) {
        if (depth <= 0) {
            return number();
        }

        std::uniform_int_distribution<int> kind(0, 3);
        if (kind(rng) == 0) {
            return number();
        }

        static constexpr char ops[] = {'+', '-', '*', '/'};
        std::uniform_int_distribution<int> opDist(0, 3);
        char op = ops[opDist(rng)];

        std::string lhs = expression(depth - 1);
        std::string rhs = expression(depth - 1);
        if (op == '/') {
            rhs = "(" + rhs + "+1)";
        }
        return "(" + lhs + op + rhs + ")";
    }
};

size_t parseSize(const char* value, const char* name) {
    try {
        size_t pos = 0;
        unsigned long long parsed = std::stoull(value, &pos);
        if (pos != std::string(value).size()) {
            throw std::invalid_argument("invalid");
        }
        return static_cast<size_t>(parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid ") + name + ": " + value);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        size_t count = argc >= 2 ? parseSize(argv[1], "count") : 10;
        int depth = argc >= 3 ? static_cast<int>(parseSize(argv[2], "depth")) : 4;
        unsigned seed = argc >= 4 ? static_cast<unsigned>(parseSize(argv[3], "seed")) : std::random_device{}();

        Generator generator{std::mt19937(seed), depth};
        for (size_t i = 0; i < count; ++i) {
            std::string expression = generator.expression(generator.maxDepth);
            try {
                std::cout << expression << " = "
                          << expression_calculator::evaluateExpression(expression) << "\n";
            } catch (const std::exception& ex) {
                std::cout << expression << " = ERROR: " << ex.what() << "\n";
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n"
                  << "Usage: generate_expressions [count] [depth] [seed]\n";
        return 1;
    }
    return 0;
}
