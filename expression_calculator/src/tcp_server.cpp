#include "tcp_server.h"

#include "evaluator.h"

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

// Pre-size client map for high-concurrency tests.
constexpr size_t kExpectedConcurrentClients = 16 * 1024;
constexpr size_t kMaxPendingExpressionsPerClient = 1024;

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
    : m_listenFd(createListenSocket(port)),
      m_epollFd(::epoll_create1(EPOLL_CLOEXEC)),
      m_completionEventFd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      m_maxExpressionBytes(maxExpressionBytes),
      m_workers(workerCount) {
    if (m_epollFd < 0) {
        closeFd(m_listenFd);
        throw std::runtime_error("epoll_create1 failed: " + std::string(std::strerror(errno)));
    }
    if (m_completionEventFd < 0) {
        closeFd(m_listenFd);
        closeFd(m_epollFd);
        throw std::runtime_error("eventfd failed: " + std::string(std::strerror(errno)));
    }

    addEpollFd(m_epollFd, m_listenFd, EPOLLIN);
    addEpollFd(m_epollFd, m_completionEventFd, EPOLLIN);
    m_clients.reserve(kExpectedConcurrentClients);
}

TcpServer::~TcpServer() {
    for (auto& [fd, client] : m_clients) {
        (void)client;
        ::close(fd);
    }
    closeFd(m_completionEventFd);
    closeFd(m_epollFd);
    closeFd(m_listenFd);
}

void TcpServer::run() {
    std::cerr << "server is listening; worker threads=" << m_workers.workerCount() << "\n";

    std::vector<epoll_event> events(kMaxEvents);
    while (true) {
        int ready = ::epoll_wait(m_epollFd, events.data(), static_cast<int>(events.size()), -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("epoll_wait failed: " + std::string(std::strerror(errno)));
        }

        for (int i = 0; i < ready; ++i) {
            int fd = events[i].data.fd;
            uint32_t mask = events[i].events;

            if (fd == m_listenFd) {
                acceptClients();
            } else if (fd == m_completionEventFd) {
                handleCompletions();
            } else {
                if (mask & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    closeClient(fd);
                    continue;
                }
                if (mask & EPOLLIN) {
                    handleClientReadable(fd);
                }
                if (m_clients.find(fd) != m_clients.end() && (mask & EPOLLOUT)) {
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
        int fd = ::accept4(m_listenFd, reinterpret_cast<sockaddr*>(&address), &addressLength,
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
        client.id = m_nextClientId++;
        addEpollFd(m_epollFd, fd, EPOLLIN | EPOLLRDHUP);
        m_clients.emplace(fd, std::move(client));
    }
}

void TcpServer::handleClientReadable(int fd) {
    auto it = m_clients.find(fd);
    if (it == m_clients.end()) {
        return;
    }
    ClientState& client = it->second;

    char buffer[kReadBufferSize];
    while (shouldReadFromClient(client)) {
        ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            client.input.append(buffer, static_cast<size_t>(n));
            processBufferedInput(client);

            if (client.closeAfterWrite || isClientBackpressured(client)) {
                updateClientEvents(client);
                return;
            }

            if (client.input.size() > m_maxExpressionBytes) {
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
    updateClientEvents(client);
}

void TcpServer::processBufferedInput(ClientState& client) {
    while (!isClientBackpressured(client)) {
        size_t newline = client.input.find('\n', client.scanPos);
        if (newline == std::string::npos) {
            client.scanPos = client.input.size();
            break;
        }

        if (newline > m_maxExpressionBytes) {
            client.output += "ERROR: expression is too large\n";
            client.closeAfterWrite = true;
            client.input.clear();
            client.scanPos = 0;
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
        client.pendingExpressionBytes += expression.size();
        client.pendingExpressions.push_back(std::move(expression));
        submitNextExpression(client);
    }
}

bool TcpServer::isClientBackpressured(const ClientState& client) const {
    return client.pendingExpressions.size() >= kMaxPendingExpressionsPerClient ||
           client.pendingExpressionBytes >= m_maxExpressionBytes;
}

bool TcpServer::shouldReadFromClient(const ClientState& client) const {
    return !client.closeAfterWrite && !isClientBackpressured(client);
}

void TcpServer::handleClientWritable(int fd) {
    auto it = m_clients.find(fd);
    if (it == m_clients.end()) {
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
    while (::read(m_completionEventFd, &counter, sizeof(counter)) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno != EINTR) {
            throw std::runtime_error("eventfd read failed");
        }
    }

    std::deque<Completion> completions;
    {
        std::lock_guard<std::mutex> lock(m_completionsMutex);
        completions.swap(m_completions);
    }

    for (Completion& completion : completions) {
        auto it = m_clients.find(completion.fd);
        if (it == m_clients.end() || it->second.id != completion.clientId) {
            continue;
        }

        ClientState& client = it->second;
        client.processing = false;
        client.output += std::move(completion.response);
        submitNextExpression(client);
        processBufferedInput(client);
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
    size_t expressionBytes = client.pendingExpressions.front().size();
    std::string expression = std::move(client.pendingExpressions.front());
    client.pendingExpressions.pop_front();
    client.pendingExpressionBytes -= expressionBytes;

    m_workers.submit([this, fd, clientId, expression = std::move(expression)]() mutable {
        queueCompletion({fd, clientId, makeResponse(expression)});
    });
}

void TcpServer::queueCompletion(Completion completion) {
    bool shouldWake = false;
    {
        std::lock_guard<std::mutex> lock(m_completionsMutex);
        shouldWake = m_completions.empty();
        m_completions.push_back(std::move(completion));
    }

    if (!shouldWake) {
        return;
    }

    uint64_t one = 1;
    while (::write(m_completionEventFd, &one, sizeof(one)) < 0) {
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
    event.events = EPOLLRDHUP;
    if (shouldReadFromClient(client)) {
        event.events |= EPOLLIN;
    }
    if (!client.output.empty()) {
        event.events |= EPOLLOUT;
    }
    event.data.fd = client.fd;
    if (::epoll_ctl(m_epollFd, EPOLL_CTL_MOD, client.fd, &event) < 0 && errno != EBADF) {
        throw std::runtime_error("epoll_ctl MOD failed: " + std::string(std::strerror(errno)));
    }
}

void TcpServer::closeClient(int fd) {
    auto it = m_clients.find(fd);
    if (it == m_clients.end()) {
        return;
    }
    ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    m_clients.erase(it);
}

} // namespace expression_calculator
