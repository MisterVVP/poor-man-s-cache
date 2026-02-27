#include "server.hpp"
#include <algorithm>
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>

using namespace server;

ConnectionData::~ConnectionData() = default;

CacheServer::CacheServer(const ServerSettings settings, std::shared_ptr<metrics::MetricsCollector> metricsCollector)
    : numShards(settings.numShards), port(settings.port), metrics(std::move(metricsCollector)), drainTimeout(static_cast<int64_t>(settings.drainTimeoutMs)), drainMaxConnClosePerTick(settings.drainMaxConnClosePerTick)
{
    setRespInlineCapacity(settings.respInlineCapacity);

    const auto listenFd = socket(AF_INET, SOCK_STREAM, 0);
    server_fd.store(listenFd, std::memory_order_release);
    if (listenFd == -1) {
        throw std::system_error(errno, std::system_category(), "Socket creation failed");
    }

    int flag = 1;
    if (setsockopt(listenFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
        close(listenFd);
        throw std::system_error(errno, std::system_category(), "Failed to set TCP_NODELAY for server socket");
    }
    if (setsockopt(listenFd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &flag, sizeof(flag)) == -1) {
        close(listenFd);
        throw std::system_error(errno, std::system_category(), "Failed to set TCP_DEFER_ACCEPT for server socket");
    }
    if (setsockopt(listenFd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag)) == -1) {
        close(listenFd);
        throw std::system_error(errno, std::system_category(), "Failed to set TCP_QUICKACK for server socket");
    }
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
        close(listenFd);
        throw std::system_error(errno, std::system_category(), "Failed to set SO_REUSEADDR for server socket");
    }

    int qlen = 2048;
    if (setsockopt(listenFd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen)) == -1) {
        close(listenFd);
        throw std::system_error(errno, std::system_category(), "Failed to set TCP_FASTOPEN for server socket");
    }

    if (setSocketBuffers(listenFd, settings.sockBuffer, SOCK_BUF_OPTS::SOCK_BUF_ALL) == -1) {
        close(listenFd);
        throw std::runtime_error("Failed to set socket buffer options for server socket");
    }

    if (setNonBlocking(listenFd) == -1) {
        close(listenFd);
        throw std::runtime_error("Failed to set O_NONBLOCK for server socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listenFd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(listenFd);
        throw std::system_error(errno, std::system_category(), "Bind failed");
    }

    if (listen(listenFd, settings.connQueueLimit) < 0) {
        close(listenFd);
        throw std::system_error(errno, std::system_category(), "Listen failed");
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::system_error(errno, std::system_category(), "Failed to create epoll instance");
    }

    connManager = std::make_unique<ConnManager>(epoll_fd, metrics.get());

#ifndef NDEBUG
    std::cout << "Initializing " << numShards << " server shards…\n";
#endif
    serverShards.reserve(numShards);
    KeyValueStoreSettings kvsSettings { 2053, settings.enableCompression, true };
    for (int i = 0; i < numShards; ++i) {
        serverShards.emplace_back(i, kvsSettings);
    }
}

CacheServer::~CacheServer() {
    disableAccepting();
    if (epoll_fd >= 0) {
        close(epoll_fd);
    }
}

ResponsePacket CacheServer::processRequestSync(const RequestView& request, ConnectionData& connData)
{
    auto recordRequest = [&](metrics::RequestOperation op) {
        if (metrics) {
            metrics->incrementRequest(op);
        }
    };

    auto recordResponse = [&](metrics::ResponseStatus status) {
        if (metrics) {
            metrics->incrementResponse(status);
        }
    };

    auto handleGet = [&](const char* keyPtr, RequestProtocol protocol) -> ResponsePacket {
        recordRequest(metrics::RequestOperation::Get);
        auto hash = hashFunc(keyPtr);
        if (metrics) {
            metrics->sampleKeyHash(hash);
        }
        auto& shard = serverShards[hash % numShards];
        Query query{QueryCode::GET, keyPtr, hash};
        auto result = shard.processQuery(query);
        if (result.value == NOTHING) {
            recordResponse(metrics::ResponseStatus::NotFound);
        } else if (result.value) {
            recordResponse(metrics::ResponseStatus::Ok);
        } else {
            recordResponse(metrics::ResponseStatus::Error);
        }

        if (protocol == RequestProtocol::RESP) {
            return makeRespBulkString(result.value);
        }

        if (result.ownedValue) {
            return makeCustomResponseCopy(result.value);
        }

        return makeCustomResponse(result.value);

    };

    auto handleSet = [&](const char* keyPtr, const char* valuePtr, RequestProtocol protocol) -> ResponsePacket {
        recordRequest(metrics::RequestOperation::Set);
        auto hash = hashFunc(keyPtr);
        if (metrics) {
            metrics->sampleKeyHash(hash);
        }
        auto& shard = serverShards[hash % numShards];
        Command cmd{CommandCode::SET, keyPtr, valuePtr, hash};
        auto result = shard.processCommand(cmd);
        if (result && std::strcmp(result, OK) == 0) {
            recordResponse(metrics::ResponseStatus::Ok);
        } else {
            recordResponse(metrics::ResponseStatus::Error);
        }
        if (protocol == RequestProtocol::RESP) {
            return (result && std::strcmp(result, OK) == 0) ? makeRespSimpleString(result) : makeRespError(result);
        }
        return makeCustomResponse(result);
    };

    auto handleDel = [&](const char* keyPtr, RequestProtocol protocol) -> ResponsePacket {
        recordRequest(metrics::RequestOperation::Del);
        auto hash = hashFunc(keyPtr);
        if (metrics) {
            metrics->sampleKeyHash(hash);
        }
        auto& shard = serverShards[hash % numShards];
        Command cmd{CommandCode::DEL, keyPtr, nullptr, hash};
        auto result = shard.processCommand(cmd);
        if (protocol == RequestProtocol::RESP) {
            if (result && std::strcmp(result, OK) == 0) {
                recordResponse(metrics::ResponseStatus::Ok);
                return makeRespInteger(1);
            }
            if (result && std::strcmp(result, KEY_NOT_EXISTS) == 0) {
                recordResponse(metrics::ResponseStatus::NotFound);
                return makeRespInteger(0);
            }
            recordResponse(metrics::ResponseStatus::Error);
            return makeRespError(result);
        }
        if (result && std::strcmp(result, OK) == 0) {
            recordResponse(metrics::ResponseStatus::Ok);
        } else if (result && std::strcmp(result, KEY_NOT_EXISTS) == 0) {
            recordResponse(metrics::ResponseStatus::NotFound);
        } else {
            recordResponse(metrics::ResponseStatus::Error);
        }
        return makeCustomResponse(result);
    };

    if (request.protocol == RequestProtocol::RESP) {
        auto markRespTransactionError = [&]() {
            if (connData.respTransaction && connData.respTransaction->active) {
                connData.respTransaction->aborted = true;
            }
        };

        auto ensureRespTransaction = [&]() -> RespTransactionState& {
            if (!connData.respTransaction) {
                connData.respTransaction = std::make_unique<RespTransactionState>();
            }
            return *connData.respTransaction;
        };

        auto queueRespCommand = [&](RespTransactionState::CommandType type, const char* key, const char* value) -> ResponsePacket {
            recordRequest(metrics::RequestOperation::Other);
            auto& tx = ensureRespTransaction();
            if (!tx.active) {
                recordResponse(metrics::ResponseStatus::Error);
                ++numErrors;
                return makeRespError(RESP_ERR_EXEC_NO_MULTI);
            }
            tx.queue.emplace_back();
            auto& queued = tx.queue.back();
            queued.type = type;
            queued.key = tx.persistString(key);
            queued.value = tx.persistString(value);
            recordResponse(metrics::ResponseStatus::Ok);
            return makeRespSimpleString(QUEUED_STR);
        };

        RespCommandParts parts{};
        if (!parseRespCommand(request.payload, parts)) {
            recordRequest(metrics::RequestOperation::Other);
            recordResponse(metrics::ResponseStatus::Error);
            ++numErrors;
            markRespTransactionError();
            return makeErrorResponse(RequestProtocol::RESP, UNABLE_TO_PARSE_REQUEST_ERROR);
        }

        if (std::strcmp(parts.command, MULTI_STR) == 0) {
            recordRequest(metrics::RequestOperation::Other);
            auto& tx = ensureRespTransaction();
            if (tx.active) {
                recordResponse(metrics::ResponseStatus::Error);
                ++numErrors;
                markRespTransactionError();
                return makeRespError(RESP_ERR_MULTI_NESTED);
            }
            tx.active = true;
            tx.aborted = false;
            tx.clearQueue();
            recordResponse(metrics::ResponseStatus::Ok);
            return makeRespSimpleString(OK);
        }

        if (std::strcmp(parts.command, DISCARD_STR) == 0) {
            recordRequest(metrics::RequestOperation::Other);
            if (!connData.respTransaction || !connData.respTransaction->active) {
                recordResponse(metrics::ResponseStatus::Error);
                ++numErrors;
                return makeRespError(RESP_ERR_DISCARD_NO_MULTI);
            }
            auto& tx = *connData.respTransaction;
            tx.clearQueue();
            tx.active = false;
            tx.aborted = false;
            recordResponse(metrics::ResponseStatus::Ok);
            return makeRespSimpleString(OK);
        }

        if (std::strcmp(parts.command, EXEC_STR) == 0) {
            recordRequest(metrics::RequestOperation::Other);
            if (!connData.respTransaction || !connData.respTransaction->active) {
                recordResponse(metrics::ResponseStatus::Error);
                ++numErrors;
                return makeRespError(RESP_ERR_EXEC_NO_MULTI);
            }
            auto& tx = *connData.respTransaction;
            if (tx.aborted) {
                tx.clearQueue();
                tx.active = false;
                tx.aborted = false;
                ++numErrors;
                recordResponse(metrics::ResponseStatus::Error);
                return makeRespError(RESP_ERR_EXEC_ABORTED);
            }
            std::vector<ResponsePacket> results;
            results.reserve(tx.queue.size());
            for (auto& queued : tx.queue) {
                switch (queued.type) {
                    case RespTransactionState::CommandType::Get:
                        results.emplace_back(handleGet(queued.key, RequestProtocol::RESP));
                        break;
                    case RespTransactionState::CommandType::Set:
                        results.emplace_back(handleSet(queued.key, queued.value, RequestProtocol::RESP));
                        break;
                    case RespTransactionState::CommandType::Del:
                        results.emplace_back(handleDel(queued.key, RequestProtocol::RESP));
                        break;
                }
            }
            tx.clearQueue();
            tx.active = false;
            tx.aborted = false;
            recordResponse(metrics::ResponseStatus::Ok);
            return makeRespArray(results);
        }

        if (std::strcmp(parts.command, GET_STR) == 0) {
            if (parts.argc != 2) {
                recordRequest(metrics::RequestOperation::Other);
                recordResponse(metrics::ResponseStatus::Error);
                ++numErrors;
                markRespTransactionError();
                return makeErrorResponse(RequestProtocol::RESP, INVALID_COMMAND_FORMAT);
            }
            if (connData.respTransaction && connData.respTransaction->active) {
                return queueRespCommand(RespTransactionState::CommandType::Get, parts.key, nullptr);
            }
            return handleGet(parts.key, RequestProtocol::RESP);
        }

        if (std::strcmp(parts.command, SET_STR) == 0) {
            if (parts.argc != 3 || parts.value == nullptr) {
                recordRequest(metrics::RequestOperation::Other);
                recordResponse(metrics::ResponseStatus::Error);
                ++numErrors;
                markRespTransactionError();
                return makeErrorResponse(RequestProtocol::RESP, INVALID_COMMAND_FORMAT);
            }
            if (connData.respTransaction && connData.respTransaction->active) {
                return queueRespCommand(RespTransactionState::CommandType::Set, parts.key, parts.value);
            }
            return handleSet(parts.key, parts.value, RequestProtocol::RESP);
        }

        if (std::strcmp(parts.command, DEL_STR) == 0) {
            if (parts.argc != 2) {
                recordRequest(metrics::RequestOperation::Other);
                recordResponse(metrics::ResponseStatus::Error);
                ++numErrors;
                markRespTransactionError();
                return makeErrorResponse(RequestProtocol::RESP, INVALID_COMMAND_FORMAT);
            }
            if (connData.respTransaction && connData.respTransaction->active) {
                return queueRespCommand(RespTransactionState::CommandType::Del, parts.key, nullptr);
            }
            return handleDel(parts.key, RequestProtocol::RESP);
        }

        ++numErrors;
        recordRequest(metrics::RequestOperation::Other);
        recordResponse(metrics::ResponseStatus::Error);
        markRespTransactionError();
        return makeErrorResponse(RequestProtocol::RESP, UNKNOWN_COMMAND);
    }

    const auto firstSpace = request.payload.find(' ');
    if (firstSpace == std::string_view::npos) {
        ++numErrors;
        recordRequest(metrics::RequestOperation::Other);
        recordResponse(metrics::ResponseStatus::Error);
        return makeErrorResponse(RequestProtocol::Custom, UNABLE_TO_PARSE_REQUEST_ERROR);
    }

    const auto command = request.payload.substr(0, firstSpace);
    const auto remainder = request.payload.substr(firstSpace + 1);
    if (remainder.empty()) {
        ++numErrors;
        recordRequest(metrics::RequestOperation::Other);
        recordResponse(metrics::ResponseStatus::Error);
        return makeErrorResponse(RequestProtocol::Custom, INVALID_COMMAND_FORMAT);
    }

    const auto secondSpace = remainder.find(' ');
    char* keyPtr = const_cast<char*>(remainder.data());
    const char* valuePtr = nullptr;

    if (secondSpace != std::string_view::npos) {
        keyPtr[secondSpace] = '\0';
        valuePtr = keyPtr + secondSpace + 1;
    }

    if (command == GET_STR) {
        recordRequest(metrics::RequestOperation::Get);
        return handleGet(keyPtr, RequestProtocol::Custom);
    }

    if (command == SET_STR) {
        recordRequest(metrics::RequestOperation::Set);
        if (!valuePtr) {
            ++numErrors;
            recordResponse(metrics::ResponseStatus::Error);
            return makeErrorResponse(RequestProtocol::Custom, INVALID_COMMAND_FORMAT);
        }
        return handleSet(keyPtr, valuePtr, RequestProtocol::Custom);
    }

    if (command == DEL_STR) {
        recordRequest(metrics::RequestOperation::Del);
        return handleDel(keyPtr, RequestProtocol::Custom);
    }

    recordRequest(metrics::RequestOperation::Other);
    recordResponse(metrics::ResponseStatus::Error);
    ++numErrors;
    return makeErrorResponse(RequestProtocol::Custom, UNKNOWN_COMMAND);
}

HandleReqTask CacheServer::handleRequests()
{
    while (isRunning.load(std::memory_order_acquire) || !connManager->connections.empty()) {
#ifndef NDEBUG
        auto start = std::chrono::high_resolution_clock::now();
#endif

        int event_count = epoll_wait(epoll_fd, epoll_events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT_MSEC);
        if (metrics && event_count >= 0) {
            metrics->incrementEpollWait(static_cast<std::size_t>(event_count));
        }
        if (event_count == -1) {
            if (errno == EINTR) {
#ifndef NDEBUG
                std::cout << "epoll_wait interrupted by signal (EINTR), retrying...\n";
#endif
                co_yield 0;
            } else {
                perror("epoll_wait failed");
                co_yield -1;
            }
        } else if (event_count == 0) {
#ifndef NDEBUG
            //TODO: this log is 'Trace' level, not even 'Debug' std::cout << "handleRequests finished without events to handle!\n";
#endif
            if (!connManager->connections.empty()) {
                std::vector<int> pendingFds;
                pendingFds.reserve(connManager->connections.size());
                for (const auto& [fd, _] : connManager->connections) {
                    pendingFds.push_back(fd);
                }

                for (const int fd : pendingFds) {
                    auto it = connManager->connections.find(fd);
                    if (it == connManager->connections.end()) {
                        continue;
                    }

                    if (!it->second.flushWriteBatch(fd)) {
                        connManager->closeConnection(fd, metrics::CloseReason::Error);
                    }
                }
            }
            co_yield 0;
        } else {
            std::vector<AsyncReadTask> readers;
            readers.reserve(MAX_EVENTS);
            numRequests += event_count;
            std::size_t processedRequests = 0;
            for (int i = 0; i < event_count; ++i) {
                auto client_fd = epoll_events[i].data.fd;
                if ((epoll_events[i].events & (EPOLLERR | EPOLLHUP))) {
                    connManager->closeConnection(client_fd, metrics::CloseReason::Error);
                    continue;
                }

                if (epoll_events[i].events & EPOLLOUT) {
                    auto it = connManager->connections.find(client_fd);
                    if (it != connManager->connections.end()) {
                        if (!it->second.flushWriteBatch(client_fd)) {
                            connManager->closeConnection(client_fd, metrics::CloseReason::Error);
                            continue;
                        }
                    }
                }

                if (epoll_events[i].events & EPOLLIN) {
                    auto asyncRead = readRequestAsync(client_fd);
                    connManager->updateActivity(client_fd);
                    asyncRead.client_fd = client_fd;
                    readers.emplace_back(std::move(asyncRead));
                }
            }
            std::unordered_map<int, std::vector<ResponsePacket>> responsesPerConn;
            if (metrics) {
                metrics->setInFlightRequests(readers.size());
            }
            for (int i = 0; i < readers.size(); ++i) {
                const std::lock_guard<std::mutex> lock(req_handle_mutex);
                auto fd = readers[i].client_fd;
#ifndef NDEBUG
                std::cout << "reading request from client_fd = " << fd  << ", epoll_fd = " << epoll_fd << std::endl;
#endif
                auto readResult = co_await readers[i];
                if (metrics) {
                    metrics->setInFlightRequests(readers.size() - static_cast<std::size_t>(i + 1));
                }

                if (readResult.operationResult == ReqReadOperationResult::Failure || readResult.operationResult == ReqReadOperationResult::AwaitingData) {
                    continue;
                }

                auto& connData = connManager->connections[fd];
                auto& responses = responsesPerConn[fd];
                while (!connData.pendingRequests.empty()) {
                    auto req = connData.pendingRequests.front();
                    connData.pendingRequests.pop_front();
                    ++processedRequests;
                    responses.emplace_back(processRequestSync(req, connData));
                }
                if (connData.bytesToErase > 0) {
                    bufferedReadBytes -= std::min<std::size_t>(bufferedReadBytes, connData.bytesToErase);
                    connData.readBuffer.erase(connData.readBuffer.begin(), connData.readBuffer.begin() + connData.bytesToErase);
                    connData.bytesToErase = 0;
                    if (metrics) {
                        metrics->setReadBufferUsageBytes(bufferedReadBytes);
                    }
                }
            }

            std::size_t queuedBytes = 0;
            for (auto& [fd, responses] : responsesPerConn) {
                if (responses.empty())
                    continue;

                auto it = connManager->connections.find(fd);
                if (it == connManager->connections.end())
                    continue;

                ConnectionData& conn = it->second;

                for (const auto& resp : responses) {
                    queuedBytes += resp.size;
                    conn.queueResponseChunk(resp.data, resp.size, resp.protocol);
                    if (resp.protocol == RequestProtocol::Custom) {
                        queuedBytes += 1;
                    }
                }

                if(!conn.flushWriteBatch(fd)) {
                    ++numErrors;
                    connManager->closeConnection(fd, metrics::CloseReason::Error);
                };
            }

            if (metrics) {
                metrics->setWriteQueueDepth(queuedBytes);
                metrics->recordBatch(processedRequests);
                if (processedRequests > 0) {
                    updateKvsMetrics();
                }
            }

#ifndef NDEBUG
            auto stop = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
            std::cout << "handleRequests interation finished in " << duration.count() << " ns ! event_count = " << event_count << std::endl;
#endif
            co_yield event_count;
        }
    }
    co_return 0;
}

AsyncReadTask server::CacheServer::readRequestAsync(int client_fd)
{
    char buffer[READ_BUFFER_SIZE];
    buffer[0] = 0;
    uint16_t readErrorsCounter = 0;
    uint_fast32_t read_attempts = 0;
    bool parsed = false;
    auto& connData = connManager->connections[client_fd];
    while (read_attempts < READ_MAX_ATTEMPTS) {
        if (metrics) {
            metrics->incrementSyscallRecv();
        }
        ssize_t bytes_read = co_await AsyncReadAwaiter(client_fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR && readErrorsCounter < READ_NUM_RETRY_ON_INT) {
                perror("Failed to read client request buffer: interruption signal received. Retrying…");
                ++readErrorsCounter;
                continue;
            }
            perror("Failed to read client request buffer");
            co_return ReadRequestResult{ ReqReadOperationResult::Failure };
        }

        if (bytes_read == 0) {
            bufferedReadBytes -= std::min<std::size_t>(bufferedReadBytes, connData.readBuffer.size());
            if (metrics) {
                metrics->setReadBufferUsageBytes(bufferedReadBytes);
            }
            connManager->closeConnection(client_fd, metrics::CloseReason::Client);
            co_return ReadRequestResult{ ReqReadOperationResult::Failure };
        }

        connData.readBuffer.insert(connData.readBuffer.end(), buffer, buffer + bytes_read);
        bufferedReadBytes += static_cast<std::size_t>(bytes_read);
        if (metrics) {
            metrics->addBytesRx(static_cast<std::size_t>(bytes_read));
            metrics->setReadBufferUsageBytes(bufferedReadBytes);
        }
        ++read_attempts;
    }

    size_t start = 0;
    while (start < connData.readBuffer.size()) {
        char current = connData.readBuffer[start];

        if (current == MSG_SEPARATOR) {
            ++start;
            continue;
        }

        if (current == RESP_ARRAY_PREFIX) {
            auto parseResult = parseRespMessageLength(connData.readBuffer, start);
            if (parseResult.status == RespParseStatus::Incomplete) {
                break;
            }

            if (parseResult.status == RespParseStatus::Error) {
                ++numErrors;
                connData.pendingRequests.clear();
                connData.readBuffer.clear();
                connData.bytesToErase = 0;
                bufferedReadBytes = 0;
                if (metrics) {
                    metrics->setReadBufferUsageBytes(bufferedReadBytes);
                }
                connManager->closeConnection(client_fd, metrics::CloseReason::Error);
                co_return ReadRequestResult{ ReqReadOperationResult::Failure };
            }

            std::string_view req{connData.readBuffer.data() + start, parseResult.length};
            connData.pendingRequests.emplace_back(RequestView{req, RequestProtocol::RESP});
            parsed = true;
            start += parseResult.length;
            continue;
        }

        size_t pos = start;
        while (pos < connData.readBuffer.size() && connData.readBuffer[pos] != MSG_SEPARATOR) {
            ++pos;
        }

        if (pos >= connData.readBuffer.size()) {
            break;
        }

        size_t len = pos - start;
        connData.readBuffer[pos] = '\0';
        std::string_view req{connData.readBuffer.data() + start, len};
        connData.pendingRequests.emplace_back(RequestView{req, RequestProtocol::Custom});
        parsed = true;
        start = pos + 1;
    }

    if (start > 0) {
        connData.bytesToErase += start;
    }

    if (metrics) {
        metrics->setReadBufferUsageBytes(bufferedReadBytes);
    }

    if (parsed) {
        co_return ReadRequestResult{ ReqReadOperationResult::Success };
    }

    co_return ReadRequestResult{ ReqReadOperationResult::AwaitingData };
}

void CacheServer::sendResponses(int client_fd, const std::vector<ResponsePacket>& responses) {
    std::vector<iovec> iov;
    iov.reserve(responses.size() * 2);
    std::vector<char> separators;
    separators.reserve(responses.size());

    size_t totalRequired = 0;
    for (const auto& response : responses) {
        iovec dataVec{};
        dataVec.iov_base = const_cast<char*>(response.data);
        dataVec.iov_len = response.size;
        iov.push_back(dataVec);
        totalRequired += response.size;

        if (response.protocol == RequestProtocol::Custom) {
            separators.push_back(MSG_SEPARATOR);
            iovec sepVec{};
            sepVec.iov_base = static_cast<void*>(&separators.back());
            sepVec.iov_len = 1;
            iov.push_back(sepVec);
            totalRequired += 1;
        }
    }

    size_t totalSent = 0;
    size_t iov_idx = 0;

    msghdr msg{};
    msg.msg_iov = iov.data();
    msg.msg_iovlen = iov.size();

    while (totalSent < totalRequired) {
        if (metrics) {
            metrics->incrementSyscallSend();
        }
        auto bytesSent = sendmsg(client_fd, &msg, 0);

        if (bytesSent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                perror("Error when sending data back to client");
                ++numErrors;
                break;
            }
        }

        totalSent += bytesSent;
        if (metrics) {
            metrics->addBytesTx(static_cast<std::size_t>(bytesSent));
        }

        while (bytesSent > 0 && iov_idx < iov.size()) {
            if (static_cast<size_t>(bytesSent) >= iov[iov_idx].iov_len) {
                bytesSent -= iov[iov_idx].iov_len;
                ++iov_idx;
            } else {
                iov[iov_idx].iov_base = static_cast<char*>(iov[iov_idx].iov_base) + bytesSent;
                iov[iov_idx].iov_len -= bytesSent;
                bytesSent = 0;
            }
        }

        msg.msg_iov = &iov[iov_idx];
        msg.msg_iovlen = iov.size() - iov_idx;
    }
}

void CacheServer::updateKvsMetrics()
{
    if (!metrics) {
        return;
    }

    uint64_t totalItems = 0;
    uint64_t totalBytesUsed = 0;
    for (const auto& shard : serverShards) {
        totalItems += shard.keyValueStore->getNumEntries();
        totalBytesUsed += shard.keyValueStore->getDataBytesUsed();
    }

    metrics->setKvsState(totalItems, totalBytesUsed);
}

int CacheServer::Start()
{
    isRunning.store(true, std::memory_order_release);
    setWorkerState(WorkerReadinessState::STARTING);

    std::cout << "Server started on port " << port << ", " << numShards << " shards are ready\n";

    int resultCode = 0;

    auto acceptTask = connManager->acceptConnections(server_fd.load(std::memory_order_acquire), isRunning);
    auto hrt = handleRequests();

    if (isRunning.load(std::memory_order_acquire)) {
        setWorkerState(WorkerReadinessState::READY);
    }

    std::cout << "Cache server is ready to accept connections on port " << port << std::endl;

    try {
        bool drainingStarted = false;
        bool drainTimeoutRecorded = false;
        auto drainStartedAt = std::chrono::steady_clock::now();

        while (true) {
            auto accepted_count = acceptTask.next_value();
            auto events_processed = hrt.next_value();
            if (accepted_count < 0 || events_processed < 0) {
                resultCode = -1;
                break;
            }

            if (isRunning.load(std::memory_order_acquire)) {
                if (accepted_count == 0 && events_processed <= 0) {
                    std::this_thread::sleep_for(PROCESS_REQ_DELAY);
                }
                continue;
            }

            if (!drainingStarted) {
                drainingStarted = true;
                drainStartedAt = std::chrono::steady_clock::now();
                disableAccepting();
                setWorkerState(WorkerReadinessState::DRAINING);
            }

            if (connManager->connections.empty()) {
                break;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - drainStartedAt);
            if (elapsed >= drainTimeout) {
                if (!drainTimeoutRecorded && metrics) {
                    metrics->incrementDrainTimeout();
                    drainTimeoutRecorded = true;
                }
                std::vector<int> fdsToClose;
                const auto maxClose = std::max<uint_fast32_t>(1, drainMaxConnClosePerTick);
                fdsToClose.reserve(std::min<std::size_t>(connManager->connections.size(), maxClose));

                for (const auto& [fd, _] : connManager->connections) {
                    if (fdsToClose.size() >= maxClose) {
                        break;
                    }
                    fdsToClose.push_back(fd);
                }

                for (int fd : fdsToClose) {
                    connManager->closeConnection(fd, metrics::CloseReason::Server);
                }

                if (connManager->connections.empty()) {
                    break;
                }
            }

            std::this_thread::sleep_for(PROCESS_REQ_DELAY);
        }

        setWorkerState(WorkerReadinessState::STOPPED);
        std::cout << "Server stopped.\n";
    }
    catch (const std::exception& ex) {
        std::cerr << "Unrecoverable exception during requests handling: " << ex.what() << '\n';
        Stop();
        setWorkerState(WorkerReadinessState::STOPPED);
        resultCode = -1;
    }

    return resultCode;
}

bool CacheServer::runStartupSelfChecks(std::string* error) const noexcept
{
    if (server_fd.load(std::memory_order_acquire) < 0) {
        if (error) {
            *error = "startup self-check failed: data port is not bound";
        }
        return false;
    }

    if (epoll_fd < 0) {
        if (error) {
            *error = "startup self-check failed: epoll is not initialized";
        }
        return false;
    }

    if (!connManager) {
        if (error) {
            *error = "startup self-check failed: connection manager is not initialized";
        }
        return false;
    }

    if (numShards == 0) {
        if (error) {
            *error = "startup self-check failed: number of shards must be greater than zero";
        }
        return false;
    }

    if (serverShards.size() != numShards) {
        if (error) {
            *error = "startup self-check failed: shard count mismatch";
        }
        return false;
    }

    for (std::size_t i = 0; i < serverShards.size(); ++i) {
        const auto& shard = serverShards[i];
        if (!shard.keyValueStore) {
            if (error) {
                *error = "startup self-check failed: shard KVS is missing";
            }
            return false;
        }
        if (shard.keyValueStore->getPoolCapacity() == 0) {
            if (error) {
                *error = "startup self-check failed: shard memory arena is not allocated";
            }
            return false;
        }
    }

    return true;
}

void CacheServer::Stop(metrics::ShutdownReason reason) noexcept
{
    const auto wasRunning = isRunning.exchange(false, std::memory_order_acq_rel);
    if (!wasRunning) {
        return;
    }

    std::cout << "Stopping server…\n";
    if (metrics) {
        metrics->incrementShutdown(reason);
    }
    setWorkerState(WorkerReadinessState::DRAINING);
    disableAccepting();
}

void CacheServer::disableAccepting() noexcept
{
    const int fd = server_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        close(fd);
    }
}

void CacheServer::setWorkerState(WorkerReadinessState next) noexcept
{
    workerState.store(static_cast<uint8_t>(next), std::memory_order_release);
    if (!metrics) {
        return;
    }

    switch (next) {
        case WorkerReadinessState::STARTING:
            metrics->setWorkerState(metrics::WorkerState::Starting);
            break;
        case WorkerReadinessState::READY:
            metrics->setWorkerState(metrics::WorkerState::Ready);
            break;
        case WorkerReadinessState::DRAINING:
        case WorkerReadinessState::STOPPED:
        default:
            metrics->setWorkerState(metrics::WorkerState::Draining);
            break;
    }
}

WorkerReadinessState CacheServer::workerReadinessState() const noexcept
{
    return static_cast<WorkerReadinessState>(workerState.load(std::memory_order_acquire));
}
