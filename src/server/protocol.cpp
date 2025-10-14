#include "protocol.hpp"

namespace server {
namespace {
    constexpr char RESP_SIMPLE_PREFIX = '+';
    constexpr char RESP_BULK_PREFIX = '$';
    constexpr char RESP_ERROR_PREFIX[] = "-ERR ";
    constexpr char RESP_NULL_BULK[] = "$-1\r\n";
}

RespParseResult parseRespMessageLength(const std::vector<char>& buffer, size_t start)
{
    size_t idx = start;
    const size_t end = buffer.size();

    if (idx >= end || buffer[idx] != '*') {
        return {RespParseStatus::Error, 0};
    }

    ++idx;
    size_t arrayLen = 0;
    bool hasDigits = false;

    while (idx < end) {
        const char c = buffer[idx];
        if (c == '\r') {
            if (idx + 1 >= end) {
                return {RespParseStatus::Incomplete, 0};
            }

            if (buffer[idx + 1] != '\n' || !hasDigits) {
                return {RespParseStatus::Error, 0};
            }

            idx += 2;
            break;
        }

        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return {RespParseStatus::Error, 0};
        }

        hasDigits = true;
        arrayLen = arrayLen * 10 + static_cast<size_t>(c - '0');
        ++idx;
    }

    if (!hasDigits) {
        return {RespParseStatus::Error, 0};
    }

    for (size_t arg = 0; arg < arrayLen; ++arg) {
        if (idx >= end) {
            return {RespParseStatus::Incomplete, 0};
        }

        if (buffer[idx] != RESP_BULK_PREFIX) {
            return {RespParseStatus::Error, 0};
        }

        ++idx;
        size_t bulkLen = 0;
        bool lenDigits = false;

        while (idx < end) {
            const char c = buffer[idx];
            if (c == '\r') {
                if (idx + 1 >= end) {
                    return {RespParseStatus::Incomplete, 0};
                }

                if (buffer[idx + 1] != '\n' || !lenDigits) {
                    return {RespParseStatus::Error, 0};
                }

                idx += 2;
                break;
            }

            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return {RespParseStatus::Error, 0};
            }

            lenDigits = true;
            bulkLen = bulkLen * 10 + static_cast<size_t>(c - '0');
            ++idx;
        }

        if (!lenDigits) {
            return {RespParseStatus::Error, 0};
        }

        if (idx + bulkLen + 2 > end) {
            return {RespParseStatus::Incomplete, 0};
        }

        idx += bulkLen;

        if (idx + 1 >= end) {
            return {RespParseStatus::Incomplete, 0};
        }

        if (buffer[idx] != '\r' || buffer[idx + 1] != '\n') {
            return {RespParseStatus::Error, 0};
        }

        idx += 2;
    }

    return {RespParseStatus::Complete, idx - start};
}

bool parseRespCommand(std::string_view payload, RespCommandParts& parts)
{
    if (payload.empty() || payload.front() != '*') {
        return false;
    }

    auto* data = const_cast<char*>(payload.data());
    size_t idx = 1;
    const size_t end = payload.size();

    auto readNumber = [&](size_t& out) -> bool {
        if (idx >= end) {
            return false;
        }

        size_t value = 0;
        bool hasDigits = false;

        while (idx < end) {
            const char c = data[idx];
            if (c == '\r') {
                if (idx + 1 >= end || data[idx + 1] != '\n' || !hasDigits) {
                    return false;
                }

                idx += 2;
                out = value;
                return true;
            }

            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return false;
            }

            hasDigits = true;
            value = value * 10 + static_cast<size_t>(c - '0');
            ++idx;
        }

        return false;
    };

    size_t elements = 0;
    if (!readNumber(elements) || elements < 2 || elements > 3) {
        return false;
    }

    for (size_t i = 0; i < elements; ++i) {
        if (idx >= end || data[idx] != RESP_BULK_PREFIX) {
            return false;
        }

        ++idx;

        size_t len = 0;
        if (!readNumber(len) || idx + len + 2 > end) {
            return false;
        }

        char* startPtr = data + idx;
        idx += len;

        if (idx + 1 >= end || data[idx] != '\r' || data[idx + 1] != '\n') {
            return false;
        }

        data[idx] = '\0';
        idx += 2;

        if (i == 0) {
            parts.command = startPtr;
        } else if (i == 1) {
            parts.key = startPtr;
        } else if (i == 2) {
            parts.value = startPtr;
        }
    }

    parts.argc = elements;
    return parts.command && parts.key;
}

ResponsePacket makeCustomResponse(const char* message)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::Custom;
    response.data = message;
    response.size = std::strlen(message);
    return response;
}

ResponsePacket makeRespSimpleString(const char* message)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;
    const size_t len = std::strlen(message);
    auto buffer = std::unique_ptr<char[]>(new char[len + 3]);

    buffer[0] = RESP_SIMPLE_PREFIX;
    if (len > 0) {
        std::memcpy(buffer.get() + 1, message, len);
    }
    buffer[len + 1] = '\r';
    buffer[len + 2] = '\n';

    response.size = len + 3;
    response.data = buffer.get();
    response.owned = std::move(buffer);
    return response;
}

ResponsePacket makeRespBulkString(const char* value)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;

    if (value == NOTHING) {
        response.data = RESP_NULL_BULK;
        response.size = sizeof(RESP_NULL_BULK) - 1;
        return response;
    }

    const size_t len = std::strlen(value);
    char lenBuf[32];
    auto res = std::to_chars(lenBuf, lenBuf + sizeof(lenBuf), len);
    const size_t digits = static_cast<size_t>(res.ptr - lenBuf);

    const size_t total = digits + len + 5;
    auto buffer = std::unique_ptr<char[]>(new char[total]);
    char* out = buffer.get();

    out[0] = RESP_BULK_PREFIX;
    if (digits > 0) {
        std::memcpy(out + 1, lenBuf, digits);
    }
    out[1 + digits] = '\r';
    out[2 + digits] = '\n';
    if (len > 0) {
        std::memcpy(out + 3 + digits, value, len);
    }
    out[3 + digits + len] = '\r';
    out[4 + digits + len] = '\n';

    response.size = total;
    response.data = buffer.get();
    response.owned = std::move(buffer);
    return response;
}

ResponsePacket makeRespError(const char* message)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;

    const size_t msgLen = std::strlen(message);
    const size_t prefixLen = sizeof(RESP_ERROR_PREFIX) - 1;
    auto buffer = std::unique_ptr<char[]>(new char[prefixLen + msgLen + 2]);
    char* out = buffer.get();

    if (prefixLen > 0) {
        std::memcpy(out, RESP_ERROR_PREFIX, prefixLen);
    }
    if (msgLen > 0) {
        std::memcpy(out + prefixLen, message, msgLen);
    }
    out[prefixLen + msgLen] = '\r';
    out[prefixLen + msgLen + 1] = '\n';

    response.size = prefixLen + msgLen + 2;
    response.data = buffer.get();
    response.owned = std::move(buffer);
    return response;
}

ResponsePacket makeErrorResponse(RequestProtocol protocol, const char* message)
{
    if (protocol == RequestProtocol::RESP) {
        return makeRespError(message);
    }
    return makeCustomResponse(message);
}

} // namespace server
