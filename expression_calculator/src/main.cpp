#include "tcp_server.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

constexpr std::uint16_t kDefaultPort = 5555;
constexpr size_t kDefaultMaxExpressionBytes = 1024ULL * 1024ULL * 1024ULL;

struct Config {
    std::uint16_t port = kDefaultPort;
    size_t workers = std::max(1u, std::thread::hardware_concurrency());
    size_t maxExpressionBytes = kDefaultMaxExpressionBytes;
};

[[noreturn]] void usage(const char* program) {
    std::cerr << "Usage: " << program << " [port] [workers] [max_expression_bytes]\n"
              << "Defaults: port=" << kDefaultPort
              << " workers=hardware_concurrency"
              << " max_expression_bytes=" << kDefaultMaxExpressionBytes << "\n";
    std::exit(1);
}

size_t parseSize(const char* value, const char* name) {
    try {
        size_t pos = 0;
        unsigned long long parsed = std::stoull(value, &pos);
        if (pos != std::string(value).size() || parsed == 0) {
            throw std::invalid_argument("invalid");
        }
        return static_cast<size_t>(parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid ") + name + ": " + value);
    }
}

Config parseConfig(int argc, char** argv) {
    if (argc > 4) {
        usage(argv[0]);
    }

    Config config;
    if (argc >= 2) {
        size_t port = parseSize(argv[1], "port");
        if (port > 65535) {
            throw std::invalid_argument("port is out of range");
        }
        config.port = static_cast<std::uint16_t>(port);
    }
    if (argc >= 3) {
        config.workers = parseSize(argv[2], "workers");
    }
    if (argc >= 4) {
        config.maxExpressionBytes = parseSize(argv[3], "max_expression_bytes");
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Config config = parseConfig(argc, argv);
        expression_calculator::TcpServer server(config.port, config.workers, config.maxExpressionBytes);
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
