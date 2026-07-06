#pragma once

#include "thread_pool.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace expression_calculator {

/**
 * @brief Non-blocking TCP expression calculator server.
 *
 * The server uses one epoll-based event loop for socket I/O and a fixed-size
 * ThreadPool for expression evaluation. Client sockets stay open after each
 * response, so a client may send multiple newline-terminated expressions over
 * the same connection.
 */
class TcpServer {
public:
    /**
     * @brief Creates a TCP server bound to the given port.
     * @param port TCP port to listen on.
     * @param workerCount Number of worker threads used for expression evaluation.
     * @param maxExpressionBytes Maximum accepted expression size per connection.
     * @throws std::runtime_error If socket, epoll, eventfd, bind, or listen setup fails.
     */
    TcpServer(std::uint16_t port, size_t workerCount, size_t maxExpressionBytes);

    /**
     * @brief Closes all sockets and releases epoll/eventfd resources.
     */
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    /**
     * @brief Starts the event loop and serves clients until the process exits.
     * @throws std::runtime_error If an unrecoverable epoll or accept error occurs.
     */
    void run();

private:
    /**
     * @brief Runtime state for one connected client socket.
     */
    struct ClientState {
        int fd = -1;
        std::uint64_t id = 0;
        std::string input;
        size_t scanPos = 0;
        std::string output;
        std::deque<std::string> pendingExpressions;
        bool processing = false;
        bool closeAfterWrite = false;
    };

    /**
     * @brief Result produced by a worker thread and consumed by the event loop.
     */
    struct Completion {
        int fd = -1;
        std::uint64_t clientId = 0;
        std::string response;
    };

    /**
     * @brief Creates, binds, and listens on a non-blocking TCP socket.
     * @param port TCP port to bind.
     * @return Listening socket file descriptor.
     */
    int createListenSocket(std::uint16_t port);

    /**
     * @brief Accepts all pending client connections from the listening socket.
     */
    void acceptClients();

    /**
     * @brief Reads available bytes from a client and extracts complete expressions.
     * @param fd Client socket file descriptor.
     */
    void handleClientReadable(int fd);

    /**
     * @brief Writes queued response bytes to a client socket.
     * @param fd Client socket file descriptor.
     */
    void handleClientWritable(int fd);

    /**
     * @brief Drains worker completion notifications and appends responses to clients.
     */
    void handleCompletions();

    /**
     * @brief Submits the next pending expression for a client if no expression is running.
     * @param client Client state to schedule.
     */
    void submitNextExpression(ClientState& client);

    /**
     * @brief Queues a worker completion and wakes the epoll event loop through eventfd.
     * @param completion Completed evaluation result.
     */
    void queueCompletion(Completion completion);

    /**
     * @brief Updates epoll interest flags for a client based on pending output.
     * @param client Client state whose socket should be updated.
     */
    void updateClientEvents(const ClientState& client);

    /**
     * @brief Removes a client from epoll, closes the socket, and drops its state.
     * @param fd Client socket file descriptor.
     */
    void closeClient(int fd);

    int m_listenFd = -1;
    int m_epollFd = -1;
    int m_completionEventFd = -1;
    size_t m_maxExpressionBytes = 0;
    ThreadPool m_workers;
    std::uint64_t m_nextClientId = 1;
    std::unordered_map<int, ClientState> m_clients;

    std::mutex m_completionsMutex;
    std::deque<Completion> m_completions;
};

} // namespace expression_calculator
