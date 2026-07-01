#include "expression_calculator/tcp_server.h"

#include "expression_calculator/evaluator.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <string_view>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace expression_calculator {
namespace {

constexpr int kMaxEvents = 1024;
constexpr size_t kReadBufferSize = 1024 * 1024;
constexpr size_t kExpectedConcurrentClients = 16 * 1024;

void closeFd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void addEpollFd(int epollFd, int fd, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event) < 0) {
        throw std::runtime_error("epoll_ctl ADD failed: " + std::string(std::strerror(errno)));
    }
}

std::string makeResponse(std::string_view expression) {
    try {
        return std::to_string(evaluateExpression(expression)) + "\n";
    } catch (const std::exception& ex) {
        return "ERROR: " + std::string(ex.what()) + "\n";
    }
}

} // namespace

TcpServer::TcpServer(std::uint16_t port, size_t workerCount, size_t maxExpressionBytes)
    : listenFd_(createListenSocket(port)),
      epollFd_(::epoll_create1(EPOLL_CLOEXEC)),
      completionEventFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      maxExpressionBytes_(maxExpressionBytes),
      workers_(workerCount) {
    if (epollFd_ < 0) {
        closeFd(listenFd_);
        throw std::runtime_error("epoll_create1 failed: " + std::string(std::strerror(errno)));
    }
    if (completionEventFd_ < 0) {
        closeFd(listenFd_);
        closeFd(epollFd_);
        throw std::runtime_error("eventfd failed: " + std::string(std::strerror(errno)));
    }

    addEpollFd(epollFd_, listenFd_, EPOLLIN);
    addEpollFd(epollFd_, completionEventFd_, EPOLLIN);
    clients_.reserve(kExpectedConcurrentClients);
}

TcpServer::~TcpServer() {
    for (auto& [fd, client] : clients_) {
        (void)client;
        ::close(fd);
    }
    closeFd(completionEventFd_);
    closeFd(epollFd_);
    closeFd(listenFd_);
}

void TcpServer::run() {
    std::cerr << "server is listening; worker threads=" << workers_.workerCount() << "\n";

    std::vector<epoll_event> events(kMaxEvents);
    while (true) {
        int ready = ::epoll_wait(epollFd_, events.data(), static_cast<int>(events.size()), -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("epoll_wait failed: " + std::string(std::strerror(errno)));
        }

        for (int i = 0; i < ready; ++i) {
            int fd = events[i].data.fd;
            uint32_t mask = events[i].events;

            if (fd == listenFd_) {
                acceptClients();
            } else if (fd == completionEventFd_) {
                handleCompletions();
            } else {
                if (mask & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    closeClient(fd);
                    continue;
                }
                if (mask & EPOLLIN) {
                    handleClientReadable(fd);
                }
                if (clients_.find(fd) != clients_.end() && (mask & EPOLLOUT)) {
                    handleClientWritable(fd);
                }
            }
        }
    }
}

int TcpServer::createListenSocket(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
    }

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        closeFd(fd);
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        closeFd(fd);
        throw std::runtime_error("bind failed: " + std::string(std::strerror(errno)));
    }
    if (::listen(fd, SOMAXCONN) < 0) {
        closeFd(fd);
        throw std::runtime_error("listen failed: " + std::string(std::strerror(errno)));
    }
    return fd;
}

void TcpServer::acceptClients() {
    while (true) {
        sockaddr_in address{};
        socklen_t addressLength = sizeof(address);
        int fd = ::accept4(listenFd_, reinterpret_cast<sockaddr*>(&address), &addressLength,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("accept4 failed: " + std::string(std::strerror(errno)));
        }

        int nodelay = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        ClientState client;
        client.fd = fd;
        client.id = nextClientId_++;
        addEpollFd(epollFd_, fd, EPOLLIN | EPOLLRDHUP);
        clients_.emplace(fd, std::move(client));
    }
}

void TcpServer::handleClientReadable(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }
    ClientState& client = it->second;

    char buffer[kReadBufferSize];
    while (true) {
        ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            client.input.append(buffer, static_cast<size_t>(n));

            while (true) {
                size_t newline = client.input.find('\n', client.scanPos);
                if (newline == std::string::npos) {
                    client.scanPos = client.input.size();
                    break;
                }

                if (newline > maxExpressionBytes_) {
                    client.output += "ERROR: expression is too large\n";
                    client.closeAfterWrite = true;
                    client.input.clear();
                    updateClientEvents(client);
                    return;
                }

                std::string expression;
                if (newline + 1 == client.input.size()) {
                    expression = std::move(client.input);
                    expression.resize(newline);
                    client.input.clear();
                } else {
                    expression = client.input.substr(0, newline);
                    client.input.erase(0, newline + 1);
                }
                if (!expression.empty() && expression.back() == '\r') {
                    expression.pop_back();
                }
                client.scanPos = 0;
                client.pendingExpressions.push_back(std::move(expression));
                submitNextExpression(client);
            }

            if (client.input.size() > maxExpressionBytes_) {
                client.output += "ERROR: expression is too large\n";
                client.closeAfterWrite = true;
                client.input.clear();
                updateClientEvents(client);
                return;
            }
        } else if (n == 0) {
            closeClient(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            closeClient(fd);
            return;
        }
    }
}

void TcpServer::handleClientWritable(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }
    ClientState& client = it->second;

    while (!client.output.empty()) {
        ssize_t n = ::write(fd, client.output.data(), client.output.size());
        if (n > 0) {
            client.output.erase(0, static_cast<size_t>(n));
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            updateClientEvents(client);
            return;
        } else {
            closeClient(fd);
            return;
        }
    }

    if (client.closeAfterWrite) {
        closeClient(fd);
        return;
    }
    updateClientEvents(client);
}

void TcpServer::handleCompletions() {
    uint64_t counter = 0;
    while (::read(completionEventFd_, &counter, sizeof(counter)) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno != EINTR) {
            throw std::runtime_error("eventfd read failed");
        }
    }

    std::deque<Completion> completions;
    {
        std::lock_guard<std::mutex> lock(completionsMutex_);
        completions.swap(completions_);
    }

    for (Completion& completion : completions) {
        auto it = clients_.find(completion.fd);
        if (it == clients_.end() || it->second.id != completion.clientId) {
            continue;
        }

        ClientState& client = it->second;
        client.processing = false;
        client.output += std::move(completion.response);
        submitNextExpression(client);
        updateClientEvents(client);
    }
}

void TcpServer::submitNextExpression(ClientState& client) {
    if (client.processing || client.pendingExpressions.empty() || client.closeAfterWrite) {
        return;
    }

    client.processing = true;
    int fd = client.fd;
    std::uint64_t clientId = client.id;
    std::string expression = std::move(client.pendingExpressions.front());
    client.pendingExpressions.pop_front();

    workers_.submit([this, fd, clientId, expression = std::move(expression)]() mutable {
        queueCompletion({fd, clientId, makeResponse(expression)});
    });
}

void TcpServer::queueCompletion(Completion completion) {
    bool shouldWake = false;
    {
        std::lock_guard<std::mutex> lock(completionsMutex_);
        shouldWake = completions_.empty();
        completions_.push_back(std::move(completion));
    }

    if (!shouldWake) {
        return;
    }

    uint64_t one = 1;
    while (::write(completionEventFd_, &one, sizeof(one)) < 0) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        throw std::runtime_error("eventfd write failed");
    }
}

void TcpServer::updateClientEvents(const ClientState& client) {
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP;
    if (!client.output.empty()) {
        event.events |= EPOLLOUT;
    }
    event.data.fd = client.fd;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, client.fd, &event) < 0 && errno != EBADF) {
        throw std::runtime_error("epoll_ctl MOD failed: " + std::string(std::strerror(errno)));
    }
}

void TcpServer::closeClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }
    ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    clients_.erase(it);
}

} // namespace expression_calculator
