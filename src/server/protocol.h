#pragma once
#include <cstdint>

namespace server {

    static const char MSG_SEPARATOR = 0x1F;

    // TODO: add error generator inline func ('ERROR: <reason>') + convert this to .hpp

    static constexpr const char* OK = "OK";
    static constexpr const char* NOTHING = "(nil)";
    static constexpr const char* INTERNAL_ERROR = "ERROR: Internal error";
    static constexpr const char* INVALID_COMMAND_CODE = "ERROR: Invalid command code";
    static constexpr const char* INVALID_QUERY_CODE = "ERROR: Invalid query code";
    static constexpr const char* UNKNOWN_COMMAND = "ERROR: Unknown command";
    static constexpr const char* UNABLE_TO_PARSE_REQUEST_ERROR = "ERROR: Unable to parse request";
    static constexpr const char* INVALID_COMMAND_FORMAT = "ERROR: Invalid command format";

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
}