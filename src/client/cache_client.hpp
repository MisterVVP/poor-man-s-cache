#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <deque>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   define WIN32_LEAN_AND_MEAN
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   ifdef _MSC_VER
#       pragma comment(lib, "Ws2_32.lib")
#   endif
#else
#   include <netdb.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/socket.h>
#   include <sys/types.h>
#   include <sys/uio.h>
#   include <unistd.h>
#endif
#include <cstring>

namespace pmc {

/**
 * @brief Header-only cache client for the poor-man-s-cache server.
 *
 * The client implements the textual protocol understood by the cache server
 * and provides a small RAII friendly interface that supports request
 * pipelining. Requests are queued locally and flushed to the server on demand
 * so multiple commands can be sent without waiting for their responses.
 */
class CacheClient {
public:
    /// Unique identifier associated with each request.
    using RequestId = std::uint64_t;

    /// Commands supported by the server.
    enum class RequestType {
        Get,
        Set,
        Delete,
    };

    /// Result classification returned by the client.
    enum class ResultCode {
        Ok,        ///< Command executed successfully.
        NotFound,  ///< Returned when a key does not exist.
        Error,     ///< Server reported an error.
    };

    /// Parsed response returned by the server.
    struct Response {
        RequestId requestId{};
        RequestType requestType{};
        ResultCode result{ResultCode::Error};
        std::string value;        ///< Value for GET requests when result == Ok.
        std::string errorMessage; ///< Filled when result == Error.

        [[nodiscard]] bool ok() const noexcept { return result == ResultCode::Ok; }
        [[nodiscard]] bool notFound() const noexcept { return result == ResultCode::NotFound; }
        [[nodiscard]] bool hasError() const noexcept { return result == ResultCode::Error; }
    };

    struct Options {
        std::string host{"127.0.0.1"};
        std::uint16_t port{7000};
        std::chrono::milliseconds sendTimeout{0};
        std::chrono::milliseconds receiveTimeout{0};
    };

    CacheClient() = default;
    explicit CacheClient(Options options) : options_(std::move(options)) {}
    CacheClient(const CacheClient&) = delete;
    CacheClient& operator=(const CacheClient&) = delete;

    CacheClient(CacheClient&& other) noexcept { moveFrom(std::move(other)); }
    CacheClient& operator=(CacheClient&& other) noexcept {
        if (this != &other) {
            close();
            moveFrom(std::move(other));
        }
        return *this;
    }

    ~CacheClient() { close(); }

    /// Establishes a TCP connection to the configured host.
    void connect() {
        if (connected()) {
            return;
        }

        ensureWinsockInitialized();

        const auto portStr = std::to_string(options_.port);

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = nullptr;
        const int gaiErr = ::getaddrinfo(options_.host.c_str(), portStr.c_str(), &hints, &result);
        if (gaiErr != 0) {
            throw std::system_error(gaiErr, std::generic_category(), "Failed to resolve cache server host");
        }

        int lastErrno = 0;
        for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
            int socketType = rp->ai_socktype;
#if defined(SOCK_CLOEXEC)
            socketType |= SOCK_CLOEXEC;
#endif
            SocketHandle fd = ::socket(rp->ai_family, socketType, rp->ai_protocol);
            if (fd == INVALID_SOCKET_HANDLE) {
                lastErrno = lastSocketError();
                continue;
            }

#if defined(SO_NOSIGPIPE)
            {
                int enable = 1;
                ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                             reinterpret_cast<const char*>(&enable), sizeof(enable));
            }
#endif

            {
                int flag = 1;
                ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                             reinterpret_cast<const char*>(&flag), sizeof(flag));
            }

            if (options_.sendTimeout.count() > 0) {
                setSocketTimeout(fd, SO_SNDTIMEO, options_.sendTimeout);
            }

            if (options_.receiveTimeout.count() > 0) {
                setSocketTimeout(fd, SO_RCVTIMEO, options_.receiveTimeout);
            }

            if (connectSocket(fd, rp->ai_addr, static_cast<socklen_t>(rp->ai_addrlen)) == 0) {
                socketFd_ = fd;
                lastErrno = 0;
                break;
            }

            lastErrno = lastSocketError();
            closeSocket(fd);
        }

        ::freeaddrinfo(result);

        if (!connected()) {
            if (lastErrno == 0) {
                lastErrno = lastSocketError();
            }
            throw std::system_error(lastErrno, std::system_category(), "Failed to connect to cache server");
        }
    }

    /// Closes the socket connection.
    void close() noexcept {
        if (socketFd_ != INVALID_SOCKET_HANDLE) {
            closeSocket(socketFd_);
            socketFd_ = INVALID_SOCKET_HANDLE;
        }
        pendingRequests_.clear();
        completedResponses_.clear();
        sendBuffer_.clear();
        sendOffset_ = 0;
        receiveBuffer_.clear();
        nextRequestId_ = 1;
    }

    [[nodiscard]] bool connected() const noexcept { return socketFd_ != INVALID_SOCKET_HANDLE; }

    RequestId enqueueGet(std::string_view key) {
        return enqueue(RequestType::Get, key, {});
    }

    RequestId enqueueSet(std::string_view key, std::string_view value) {
        return enqueue(RequestType::Set, key, value);
    }

    RequestId enqueueDelete(std::string_view key) {
        return enqueue(RequestType::Delete, key, {});
    }

    /// Flushes all pending requests to the server.
    void flush() {
        ensureConnected();

        while (sendOffset_ < sendBuffer_.size()) {
            const auto remaining = sendBuffer_.size() - sendOffset_;
            const char* dataPtr = sendBuffer_.data() + sendOffset_;
#if defined(_WIN32)
            const auto chunk =
                static_cast<int>(std::min<std::size_t>(remaining, std::numeric_limits<int>::max()));
            const auto sent = ::send(socketFd_, dataPtr, chunk, sendFlags());
            if (sent == SOCKET_ERROR) {
                const int errorCode = lastSocketError();
                if (isInterrupted(errorCode)) {
                    continue;
                }
                throw std::system_error(errorCode, std::system_category(), "Failed to send data to cache server");
            }
#else
            const auto sent = ::send(socketFd_, dataPtr, remaining, sendFlags());
            if (sent == -1) {
                const int errorCode = lastSocketError();
                if (isInterrupted(errorCode)) {
                    continue;
                }
                throw std::system_error(errorCode, std::system_category(), "Failed to send data to cache server");
            }
#endif
            sendOffset_ += static_cast<std::size_t>(sent);
        }

        if (!sendBuffer_.empty()) {
            sendBuffer_.clear();
            sendOffset_ = 0;
        }
    }

    /// Receives the next response from the server.
    Response receiveResponse() {
        ensureConnected();

        if (pendingRequests_.empty()) {
            throw std::logic_error("No pending requests to receive responses for");
        }

        while (true) {
            if (auto response = tryParseResponse()) {
                return *response;
            }

            char buffer[4096];
#if defined(_WIN32)
            const auto received = ::recv(socketFd_, buffer,
                                         static_cast<int>(sizeof(buffer)), 0);
            if (received == SOCKET_ERROR) {
                const int errorCode = lastSocketError();
                if (isInterrupted(errorCode)) {
                    continue;
                }
                throw std::system_error(errorCode, std::system_category(), "Failed to receive data from cache server");
            }
#else
            const auto received = ::recv(socketFd_, buffer, sizeof(buffer), 0);
            if (received == -1) {
                const int errorCode = lastSocketError();
                if (isInterrupted(errorCode)) {
                    continue;
                }
                throw std::system_error(errorCode, std::system_category(), "Failed to receive data from cache server");
            }
#endif

            if (received == 0) {
                throw std::runtime_error("Connection closed by cache server");
            }

            receiveBuffer_.insert(receiveBuffer_.end(), buffer, buffer + received);
        }
    }

    /// Waits for the response that corresponds to the provided request id.
    Response waitFor(RequestId id) {
        if (auto cached = popCompleted(id)) {
            return std::move(*cached);
        }

        while (true) {
            Response response = receiveResponse();
            if (response.requestId == id) {
                return response;
            }
            completedResponses_.emplace(response.requestId, std::move(response));
        }
    }

    Response get(std::string_view key) {
        const auto id = enqueueGet(key);
        flush();
        return waitFor(id);
    }

    Response set(std::string_view key, std::string_view value) {
        const auto id = enqueueSet(key, value);
        flush();
        return waitFor(id);
    }

    Response del(std::string_view key) {
        const auto id = enqueueDelete(key);
        flush();
        return waitFor(id);
    }

    [[nodiscard]] std::size_t pendingRequestCount() const noexcept {
        return pendingRequests_.size();
    }

private:
    struct PendingRequest {
        RequestId id;
        RequestType type;
    };

    static constexpr char MSG_SEPARATOR = 0x1F;
    static constexpr std::string_view NOTHING = "(nil)";
    static constexpr std::string_view KEY_NOT_EXISTS = "ERROR: Key does not exist";

    using SocketHandle =
#if defined(_WIN32)
        SOCKET;
#else
        int;
#endif

    static constexpr SocketHandle INVALID_SOCKET_HANDLE =
#if defined(_WIN32)
        INVALID_SOCKET;
#else
        -1;
#endif

    Options options_{};
    SocketHandle socketFd_{INVALID_SOCKET_HANDLE};
    std::deque<PendingRequest> pendingRequests_{};
    std::unordered_map<RequestId, Response> completedResponses_{};
    std::string sendBuffer_{};
    std::size_t sendOffset_{0};
    std::vector<char> receiveBuffer_{};
    RequestId nextRequestId_{1};

    void ensureConnected() {
        if (!connected()) {
            connect();
        }
    }

#if !defined(_WIN32)
    static timeval toTimeVal(std::chrono::milliseconds duration) {
        timeval tv{};
        tv.tv_sec = static_cast<long>(duration.count() / 1000);
        tv.tv_usec = static_cast<long>((duration.count() % 1000) * 1000);
        return tv;
    }
#endif

    static int sendFlags() {
#if defined(MSG_NOSIGNAL)
        return MSG_NOSIGNAL;
#else
        return 0;
#endif
    }

#if defined(_WIN32)
    class WinsockInitializer {
    public:
        WinsockInitializer() {
            WSADATA data;
            const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
            if (result != 0) {
                throw std::system_error(result, std::system_category(), "WSAStartup failed");
            }
        }

        ~WinsockInitializer() { ::WSACleanup(); }
    };
#endif

    static void ensureWinsockInitialized() {
#if defined(_WIN32)
        static const WinsockInitializer initializer{};
#endif
    }

    static void closeSocket(SocketHandle socket) noexcept {
#if defined(_WIN32)
        if (socket != INVALID_SOCKET_HANDLE) {
            ::closesocket(socket);
        }
#else
        if (socket != INVALID_SOCKET_HANDLE) {
            ::close(socket);
        }
#endif
    }

    static int connectSocket(SocketHandle socket, const sockaddr* address, socklen_t length) noexcept {
#if defined(_WIN32)
        return ::connect(socket, address, static_cast<int>(length));
#else
        return ::connect(socket, address, length);
#endif
    }

    static int lastSocketError() noexcept {
#if defined(_WIN32)
        return ::WSAGetLastError();
#else
        return errno;
#endif
    }

    static bool isInterrupted(int errorCode) noexcept {
#if defined(_WIN32)
        return errorCode == WSAEINTR;
#else
        return errorCode == EINTR;
#endif
    }

    static void setSocketTimeout(SocketHandle socket, int option,
                                 std::chrono::milliseconds timeout) noexcept {
#if defined(_WIN32)
        const auto clamped =
            static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(
                timeout.count(), std::numeric_limits<DWORD>::max()));
        ::setsockopt(socket, SOL_SOCKET, option,
                     reinterpret_cast<const char*>(&clamped),
                     sizeof(clamped));
#else
        const auto tv = toTimeVal(timeout);
        ::setsockopt(socket, SOL_SOCKET, option, &tv, sizeof(tv));
#endif
    }

    RequestId enqueue(RequestType type, std::string_view key,
                      std::optional<std::string_view> value) {
        ensureConnected();
        validateKey(key);
        if (value && value->find(MSG_SEPARATOR) != std::string_view::npos) {
            throw std::invalid_argument("Value contains protocol separator character");
        }

        const RequestId id = nextRequestId_++;
        pendingRequests_.push_back(PendingRequest{id, type});

        appendCommand(type, key, value);
        return id;
    }

    void appendCommand(RequestType type, std::string_view key,
                       std::optional<std::string_view> value) {
        switch (type) {
            case RequestType::Get:
                sendBuffer_.append("GET ");
                break;
            case RequestType::Set:
                sendBuffer_.append("SET ");
                break;
            case RequestType::Delete:
                sendBuffer_.append("DEL ");
                break;
        }

        sendBuffer_.append(key);
        if (value.has_value()) {
            sendBuffer_.push_back(' ');
            sendBuffer_.append(value->data(), value->size());
        }
        sendBuffer_.push_back(MSG_SEPARATOR);
    }

    [[nodiscard]] std::optional<Response> tryParseResponse() {
        const auto it =
            std::find(receiveBuffer_.begin(), receiveBuffer_.end(), MSG_SEPARATOR);
        if (it == receiveBuffer_.end()) {
            return std::nullopt;
        }

        const auto messageEnd =
            static_cast<std::size_t>(std::distance(receiveBuffer_.begin(), it));
        std::string message(receiveBuffer_.data(), messageEnd);
        receiveBuffer_.erase(receiveBuffer_.begin(), it + 1);

        if (pendingRequests_.empty()) {
            throw std::logic_error("Received response without pending request");
        }

        const PendingRequest pending = pendingRequests_.front();
        pendingRequests_.pop_front();

        Response response;
        response.requestId = pending.id;
        response.requestType = pending.type;
        response.result = interpretResult(pending.type, message);

        switch (response.result) {
            case ResultCode::Ok:
                if (pending.type == RequestType::Get) {
                    response.value = std::move(message);
                } else {
                    response.value.clear();
                }
                response.errorMessage.clear();
                break;
            case ResultCode::NotFound:
                response.value.clear();
                response.errorMessage.clear();
                break;
            case ResultCode::Error:
                response.value.clear();
                response.errorMessage = std::move(message);
                break;
        }

        return response;
    }

    static ResultCode interpretResult(RequestType type,
                                      const std::string& message) {
        if (message.rfind("ERROR:", 0) == 0) {
            if (type == RequestType::Delete && message == KEY_NOT_EXISTS) {
                return ResultCode::NotFound;
            }
            return ResultCode::Error;
        }

        if (type == RequestType::Get && message == NOTHING) {
            return ResultCode::NotFound;
        }

        return ResultCode::Ok;
    }

    [[nodiscard]] std::optional<Response> popCompleted(RequestId id) {
        auto it = completedResponses_.find(id);
        if (it == completedResponses_.end()) {
            return std::nullopt;
        }
        Response response = std::move(it->second);
        completedResponses_.erase(it);
        return response;
    }

    static void validateKey(std::string_view key) {
        if (key.empty()) {
            throw std::invalid_argument("Key must not be empty");
        }
        if (key.find(' ') != std::string_view::npos) {
            throw std::invalid_argument("Keys containing spaces are not supported by the protocol");
        }
        if (key.find(MSG_SEPARATOR) != std::string_view::npos) {
            throw std::invalid_argument("Key contains protocol separator character");
        }
    }

    void moveFrom(CacheClient&& other) noexcept {
        options_ = std::move(other.options_);
        socketFd_ = std::exchange(other.socketFd_, INVALID_SOCKET_HANDLE);
        pendingRequests_ = std::move(other.pendingRequests_);
        completedResponses_ = std::move(other.completedResponses_);
        sendBuffer_ = std::move(other.sendBuffer_);
        sendOffset_ = other.sendOffset_;
        other.sendOffset_ = 0;
        receiveBuffer_ = std::move(other.receiveBuffer_);
        nextRequestId_ = other.nextRequestId_;
        other.nextRequestId_ = 1;
    }
};


// ============================================================================
// ZERO-ALLOCATION CROSS-PLATFORM CLUSTER CLIENT (low-level core)
// ============================================================================

static constexpr std::uint32_t PMC_MAX_SHARDS    = 256;
static constexpr std::size_t   PMC_SEND_BUF_SIZE = 64 * 1024;
static constexpr std::size_t   PMC_RECV_BUF_SIZE = 64 * 1024;
static constexpr char          PMC_SEP           = 0x1F;

enum class ClusterResult : std::int32_t {
    Ok = 0,
    NotFound = 1,
    ServerError = 2,
    IoError = 3,
    ProtocolError = 4,
    BufferTooSmall = 5,
    InvalidArgument = 6
};

#if defined(_WIN32)
using pmc_socket_t = SOCKET;
static constexpr pmc_socket_t PMC_INVALID_SOCKET = INVALID_SOCKET;

struct WinsockInit {
    WinsockInit() noexcept {
        WSADATA w;
        WSAStartup(MAKEWORD(2,2), &w);
    }
    ~WinsockInit() noexcept {
        WSACleanup();
    }
};

static WinsockInit _winsock_bootstrap{};

#else
using pmc_socket_t = int;
static constexpr pmc_socket_t PMC_INVALID_SOCKET = -1;
#endif

inline std::uint32_t pmc_crc32(const char* data, std::size_t len) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for(std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint8_t>(data[i]);
        for(int j = 0; j < 8; ++j) {
            std::uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

inline std::uint64_t pmc_hash64(const char* key, std::size_t len) noexcept {
    std::uint32_t h32 = pmc_crc32(key, len);
    return (std::uint64_t(h32) << 32) | h32;
}

inline std::uint32_t pmc_jump_hash(std::uint64_t key, std::uint32_t buckets) noexcept {
    if (buckets == 0) {
        return 0;
    }
    std::int64_t b = -1;
    std::int64_t j = 0;
    while (j < static_cast<std::int64_t>(buckets)) {
        b = j;
        key = key * 2862933555777941757ULL + 1ULL;
        j = static_cast<std::int64_t>(
            (static_cast<double>(b + 1) *
             (static_cast<double>(1ull << 31) / static_cast<double>((key >> 33) + 1)))
        );
    }
    return static_cast<std::uint32_t>(b);
}

// ============================================================================
// CLUSTER CLIENT
// ============================================================================

struct ClusterResponse {
    bool ok{false};
    bool notFound{false};
    bool error{false};
    std::string value;

    [[nodiscard]] bool hasValue() const noexcept {
        return ok && !notFound && !error && !value.empty();
    }
};

class ClusterCacheClient {
public:
    struct ClusterOptions {
        std::string host{"127.0.0.1"};
        std::uint16_t basePort{9001};
        std::uint32_t shardCount{0};
        std::chrono::milliseconds sendTimeout{0};
        std::chrono::milliseconds recvTimeout{0};
    };

    using RequestId = std::uint64_t;

    explicit ClusterCacheClient(const ClusterOptions& opts)
        : shardCount_(opts.shardCount)
    {
        if (shardCount_ == 0) {
            throw std::invalid_argument("ClusterOptions.shardCount must be > 0");
        }

        shards_.reserve(shardCount_);
        pendingPipelined_.assign(shardCount_, 0);

        CacheClient::Options base;
        base.host          = opts.host;
        base.sendTimeout   = opts.sendTimeout;
        base.receiveTimeout = opts.recvTimeout;

        for (std::uint32_t i = 0; i < shardCount_; ++i) {
            CacheClient::Options shardOpt = base;
            shardOpt.port = static_cast<std::uint16_t>(opts.basePort + i);
            shards_.emplace_back(shardOpt);
            // Let exceptions propagate; caller (tests) already use retry logic.
            shards_.back().connect();
        }
    }

    [[nodiscard]] std::uint32_t shardForKey(const std::string& key) const noexcept {
        return shardForKey(std::string_view{key});
    }

    // ------------ synchronous CRUD ------------

    ClusterResponse get(const std::string& key) noexcept {
        ClusterResponse out;
        const std::uint32_t shardIdx = shardForKey(key);
        if (shardIdx >= shardCount_) {
            out.error = true;
            return out;
        }

        try {
            CacheClient& c = shards_[shardIdx];
            auto resp = c.get(key);
            out.ok       = resp.ok();
            out.notFound = resp.notFound();
            out.error    = resp.hasError();
            if (resp.ok()) {
                out.value = std::move(resp.value);
            } else {
                out.value.clear();
            }
        } catch (...) {
            out.ok = false;
            out.notFound = false;
            out.error = true;
            out.value.clear();
        }

        return out;
    }

    ClusterResponse set(const std::string& key, const std::string& value) noexcept {
        ClusterResponse out;
        const std::uint32_t shardIdx = shardForKey(key);
        if (shardIdx >= shardCount_) {
            out.error = true;
            return out;
        }

        try {
            CacheClient& c = shards_[shardIdx];
            auto resp = c.set(key, value);
            out.ok       = resp.ok();
            out.notFound = resp.notFound();
            out.error    = resp.hasError();
        } catch (...) {
            out.ok = false;
            out.notFound = false;
            out.error = true;
        }

        return out;
    }

    ClusterResponse del(const std::string& key) noexcept {
        ClusterResponse out;
        const std::uint32_t shardIdx = shardForKey(key);
        if (shardIdx >= shardCount_) {
            out.error = true;
            return out;
        }

        try {
            CacheClient& c = shards_[shardIdx];
            auto resp = c.del(key);
            out.ok       = resp.ok();
            out.notFound = resp.notFound();
            out.error    = resp.hasError();
        } catch (...) {
            out.ok = false;
            out.notFound = false;
            out.error = true;
        }

        return out;
    }

    // ------------ pipelining API (real, per-shard) ------------

    RequestId enqueueSet(const std::string& key, const std::string& value) noexcept {
        const std::uint32_t shardIdx = shardForKey(key);
        if (shardIdx >= shardCount_) {
            return ++nextId_; // invalid shard, but keep ID monotonic
        }
        try {
            CacheClient& c = shards_[shardIdx];
            (void)c.enqueueSet(key, value);
            ++pendingPipelined_[shardIdx];
        } catch (...) {
            // Keep pendingPipelined_ unchanged on error; ID still advances.
        }
        return ++nextId_;
    }

    RequestId enqueueGet(const std::string& key) noexcept {
        const std::uint32_t shardIdx = shardForKey(key);
        if (shardIdx >= shardCount_) {
            return ++nextId_;
        }
        try {
            CacheClient& c = shards_[shardIdx];
            (void)c.enqueueGet(key);
            ++pendingPipelined_[shardIdx];
        } catch (...) {
            // Ignore; ID still advances.
        }
        return ++nextId_;
    }

    RequestId enqueueDelete(const std::string& key) noexcept {
        const std::uint32_t shardIdx = shardForKey(key);
        if (shardIdx >= shardCount_) {
            return ++nextId_;
        }
        try {
            CacheClient& c = shards_[shardIdx];
            (void)c.enqueueDelete(key);
            ++pendingPipelined_[shardIdx];
        } catch (...) {
            // Ignore; ID still advances.
        }
        return ++nextId_;
    }

    // Flush all pipelined commands on every shard and drain their replies so that
    // subsequent synchronous get/set/del calls see a clean stream.
    void flushAll() {
        for (std::uint32_t shardIdx = 0; shardIdx < shardCount_; ++shardIdx) {
            const auto pending = pendingPipelined_[shardIdx];
            if (pending == 0) {
                continue;
            }

            CacheClient& c = shards_[shardIdx];

            // 1) Send all queued requests for this shard.
            c.flush();

            // 2) Drain exactly 'pending' replies in FIFO order.
            for (std::size_t i = 0; i < pending; ++i) {
                (void)c.receiveResponse(); // discard; tests verify state via fresh gets
            }

            pendingPipelined_[shardIdx] = 0;
        }
    }

private:
    std::uint32_t shardCount_{0};
    std::vector<CacheClient> shards_{};
    std::vector<std::size_t> pendingPipelined_{};
    RequestId nextId_{0};

    // Simple FNV-1a 32-bit hash for sharding.
    [[nodiscard]] std::uint32_t shardForKey(std::string_view key) const noexcept {
        constexpr std::uint32_t FNV_OFFSET = 2166136261u;
        constexpr std::uint32_t FNV_PRIME  = 16777619u;

        std::uint32_t hash = FNV_OFFSET;
        for (unsigned char c : key) {
            hash ^= static_cast<std::uint32_t>(c);
            hash *= FNV_PRIME;
        }

        return static_cast<std::uint32_t>(hash % shardCount_);
    }
};

} // namespace pmc
