#pragma once
#include <cstdint>
#include <cstddef>
#include <cctype>
#include <charconv>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

namespace server {

    static const char MSG_SEPARATOR = 0x1F;
    inline constexpr char RESP_ARRAY_PREFIX = '*';
    inline constexpr char RESP_SIMPLE_PREFIX = '+';
    inline constexpr char RESP_BULK_PREFIX = '$';
    inline constexpr char RESP_CR = '\r';
    inline constexpr char RESP_LF = '\n';
    inline constexpr char RESP_ERROR_PREFIX[] = "-ERR ";
    inline constexpr char RESP_NULL_BULK[] = "$-1\r\n";
    static constexpr const char* OK = "OK";
    static constexpr const char* NOTHING = "(nil)";
    static constexpr const char* KEY_NOT_EXISTS = "ERROR: Key does not exist";
    static constexpr const char* INTERNAL_ERROR = "ERROR: Internal error";
    static constexpr const char* INVALID_COMMAND_CODE = "ERROR: Invalid command code";
    static constexpr const char* INVALID_QUERY_CODE = "ERROR: Invalid query code";
    static constexpr const char* UNKNOWN_COMMAND = "ERROR: Unknown command";
    static constexpr const char* UNABLE_TO_PARSE_REQUEST_ERROR = "ERROR: Unable to parse request";
    static constexpr const char* INVALID_COMMAND_FORMAT = "ERROR: Invalid command format";

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

        ResponsePacket() = default;
        ResponsePacket(ResponsePacket&&) noexcept = default;
        ResponsePacket& operator=(ResponsePacket&&) noexcept = default;
        ResponsePacket(const ResponsePacket&) = delete;
        ResponsePacket& operator=(const ResponsePacket&) = delete;
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
    ResponsePacket makeRespBulkString(const char* value);
    ResponsePacket makeRespError(const char* message);
    ResponsePacket makeErrorResponse(RequestProtocol protocol, const char* message);
}
