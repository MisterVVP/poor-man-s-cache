#pragma once
#include <cstdint>
#include <cstddef>
#include <cctype>
#include <charconv>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>
#include <utility>
#include <limits>
#include <array>
#include <atomic>

namespace server {

    inline constexpr char MSG_SEPARATOR = 0x1F;
    inline constexpr char RESP_ARRAY_PREFIX = '*';
    inline constexpr char RESP_SIMPLE_PREFIX = '+';
    inline constexpr char RESP_BULK_PREFIX = '$';
    inline constexpr char RESP_INTEGER_PREFIX = ':';
    inline constexpr char RESP_CR = '\r';
    inline constexpr char RESP_LF = '\n';
    inline constexpr char RESP_ERROR_PREFIX[] = "-ERR ";
    inline constexpr char RESP_NULL_BULK[] = "$-1\r\n";

    extern const char MULTI_STR[];
    extern const char EXEC_STR[];
    extern const char DISCARD_STR[];
    extern const char QUEUED_STR[];
    extern const char RESP_ERR_MULTI_NESTED[];
    extern const char RESP_ERR_EXEC_NO_MULTI[];
    extern const char RESP_ERR_DISCARD_NO_MULTI[];
    extern const char RESP_ERR_EXEC_ABORTED[];
    extern const char OK[];
    extern const char NOTHING[];
    extern const char KEY_NOT_EXISTS[];
    extern const char INTERNAL_ERROR[];
    extern const char INVALID_COMMAND_CODE[];
    extern const char INVALID_QUERY_CODE[];
    extern const char UNKNOWN_COMMAND[];
    extern const char UNABLE_TO_PARSE_REQUEST_ERROR[];
    extern const char INVALID_COMMAND_FORMAT[];

    extern const char GET_STR[];
    extern const char SET_STR[];
    extern const char DEL_STR[];
    /// @brief Query codes, 0: Reserved, 1: GET
    enum QueryCode : uint_fast8_t {
        UnknownQuery = 0,
        GET = 1,
    };

    /// @brief Command codes, 0: Reserved, 1: SET
    enum CommandCode : uint_fast8_t {
        UnknownCommand = 0,
        SET = 1,
        DEL = 2,
    };

    enum class RequestProtocol : uint_fast8_t {
        Custom = 0,
        RESP = 1,
    };

    struct ResponsePacket {
        RequestProtocol protocol = RequestProtocol::Custom;
        const char* data = nullptr;
        size_t size = 0;
        std::unique_ptr<char[]> owned;
        uint16_t inlineIndex = std::numeric_limits<uint16_t>::max();

        ResponsePacket() = default;
        ResponsePacket(ResponsePacket&& other) noexcept { *this = std::move(other); }
        ResponsePacket& operator=(ResponsePacket&& other) noexcept;
        ResponsePacket(const ResponsePacket&) = delete;
        ResponsePacket& operator=(const ResponsePacket&) = delete;
        ~ResponsePacket() noexcept;

        char* tryUseInline(size_t required) noexcept;
        void setOwnedBuffer(std::unique_ptr<char[]> buffer, size_t length) noexcept;
        void setStaticData(const char* ptr, size_t length) noexcept;

        bool usesInlineStorage() const noexcept { return inlineIndex != std::numeric_limits<uint16_t>::max(); }
    };

    struct RequestView {
        std::string_view payload{};
        RequestProtocol protocol = RequestProtocol::Custom;
    };

    struct RespCommandParts {
        char* command = nullptr;
        char* key = nullptr;
        char* value = nullptr;
        size_t argc = 0;
    };

    enum class RespParseStatus : uint8_t {
        Incomplete = 0,
        Complete,
        Error,
    };

    struct RespParseResult {
        RespParseStatus status;
        size_t length;
    };

    RespParseResult parseRespMessageLength(const std::vector<char>& buffer, size_t start);
    bool parseRespCommand(std::string_view payload, RespCommandParts& parts);

    ResponsePacket makeCustomResponse(const char* message);
    ResponsePacket makeRespSimpleString(const char* message);
    ResponsePacket makeRespInteger(int64_t value);
    ResponsePacket makeRespBulkString(const char* value);
    ResponsePacket makeRespArray(const std::vector<ResponsePacket>& elements);
    ResponsePacket makeRespError(const char* message);
    ResponsePacket makeErrorResponse(RequestProtocol protocol, const char* message);

    void setRespInlineCapacity(std::size_t capacity) noexcept;
    std::size_t respInlineCapacity() noexcept;
}
