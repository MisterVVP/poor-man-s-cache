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
    inline constexpr char MULTI_STR[] = "MULTI";
    inline constexpr char EXEC_STR[] = "EXEC";
    inline constexpr char DISCARD_STR[] = "DISCARD";
    inline constexpr char QUEUED_STR[] = "QUEUED";
    inline constexpr char RESP_ERR_MULTI_NESTED[] = "ERR MULTI calls can not be nested";
    inline constexpr char RESP_ERR_EXEC_NO_MULTI[] = "ERR EXEC without MULTI";
    inline constexpr char RESP_ERR_DISCARD_NO_MULTI[] = "ERR DISCARD without MULTI";
    inline constexpr char RESP_ERR_EXEC_ABORTED[] = "EXECABORT Transaction discarded because of previous errors.";
    inline constexpr char OK[] = "OK";
    inline constexpr char NOTHING[] = "(nil)";
    inline constexpr char KEY_NOT_EXISTS[] = "ERROR: Key does not exist";
    inline constexpr char INTERNAL_ERROR[] = "ERROR: Internal error";
    inline constexpr char INVALID_COMMAND_CODE[] = "ERROR: Invalid command code";
    inline constexpr char INVALID_QUERY_CODE[] = "ERROR: Invalid query code";
    inline constexpr char UNKNOWN_COMMAND[] = "ERROR: Unknown command";
    inline constexpr char UNABLE_TO_PARSE_REQUEST_ERROR[] = "ERROR: Unable to parse request";
    inline constexpr char INVALID_COMMAND_FORMAT[] = "ERROR: Invalid command format";

    static constexpr const char* GET_STR = "GET";
    static constexpr const char* SET_STR = "SET";
    static constexpr const char* DEL_STR = "DEL";
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
