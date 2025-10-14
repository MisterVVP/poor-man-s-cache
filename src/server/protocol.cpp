#include "protocol.hpp"

namespace server {

static inline bool is_digit(char c) noexcept {
    return (c >= '0') && (c <= '9');
}

static inline bool read_crlf_decimal(const char* data, size_t end, size_t& idx, size_t& out) noexcept {
    size_t val = 0;
    bool any = false;

    while (idx < end) {
        const char c = data[idx];
        if (c == RESP_CR) {
            if (idx + 1 >= end || data[idx + 1] != RESP_LF || !any) return false;
            idx += 2;
            out = val;
            return true;
        }
        if (!is_digit(c)) return false;
        any = true;
        val = val * 10 + static_cast<size_t>(c - '0');
        ++idx;
    }
    return false;
}

static inline unsigned u64_to_ascii(size_t v, char* out) noexcept {
    char buf[20];
    unsigned n = 0;
    do {
        const size_t q = v / 10;
        const unsigned r = static_cast<unsigned>(v - q * 10);
        buf[n++] = static_cast<char>('0' + r);
        v = q;
    } while (v);

    for (unsigned i = 0, j = n - 1; i < j; ++i, --j) {
        const char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
    }
    std::memcpy(out, buf, n);
    return n;
}

RespParseResult parseRespMessageLength(const std::vector<char>& buffer, size_t start)
{
    size_t idx = start;
    const size_t end = buffer.size();

    if (idx >= end || buffer[idx] != RESP_ARRAY_PREFIX) {
        return {RespParseStatus::Error, 0};
    }
    ++idx;

    size_t arrayLen = 0;
    {
        bool any = false;
        while (idx < end) {
            const char c = buffer[idx];
            if (c == RESP_CR) {
                if (idx + 1 >= end || buffer[idx + 1] != RESP_LF || !any) {
                    return {RespParseStatus::Error, 0};
                }
                idx += 2;
                break;
            }
            if (!is_digit(c)) return {RespParseStatus::Error, 0};
            any = true;
            arrayLen = arrayLen * 10 + static_cast<size_t>(c - '0');
            ++idx;
        }
        if (!any) return {RespParseStatus::Error, 0};
    }

    for (size_t arg = 0; arg < arrayLen; ++arg) {
        if (idx >= end) return {RespParseStatus::Incomplete, 0};
        if (buffer[idx] != RESP_BULK_PREFIX) return {RespParseStatus::Error, 0};
        ++idx;

        size_t bulkLen = 0;
        {
            bool any = false;
            while (idx < end) {
                const char c = buffer[idx];
                if (c == RESP_CR) {
                    if (idx + 1 >= end || buffer[idx + 1] != RESP_LF || !any) {
                        return {RespParseStatus::Error, 0};
                    }
                    idx += 2;
                    break;
                }
                if (!is_digit(c)) return {RespParseStatus::Error, 0};
                any = true;
                bulkLen = bulkLen * 10 + static_cast<size_t>(c - '0');
                ++idx;
            }
            if (!any) return {RespParseStatus::Error, 0};
        }

        if (idx + bulkLen + 2 > end) return {RespParseStatus::Incomplete, 0};
        idx += bulkLen;
        if (buffer[idx] != RESP_CR || buffer[idx + 1] != RESP_LF) return {RespParseStatus::Error, 0};
        idx += 2;
    }

    return {RespParseStatus::Complete, idx - start};
}

bool parseRespCommand(std::string_view payload, RespCommandParts& parts)
{
    if (payload.empty() || payload.front() != RESP_ARRAY_PREFIX) return false;

    char* data = const_cast<char*>(payload.data());
    size_t idx = 1;
    const size_t end = payload.size();

    auto read_num = [&](size_t& out) -> bool {
        return read_crlf_decimal(data, end, idx, out);
    };

    size_t elements = 0;
    if (!read_num(elements) || elements < 2 || elements > 3) return false;

    for (size_t i = 0; i < elements; ++i) {
        if (idx >= end || data[idx] != RESP_BULK_PREFIX) return false;
        ++idx;

        size_t len = 0;
        if (!read_num(len) || idx + len + 2 > end) return false;

        char* startPtr = data + idx;
        idx += len;

        if (idx + 1 >= end || data[idx] != RESP_CR || data[idx + 1] != RESP_LF) return false;
        data[idx] = '\0';
        idx += 2;

        if (i == 0)       parts.command = startPtr;
        else if (i == 1)  parts.key     = startPtr;
        else              parts.value   = startPtr;
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
    char* out = buffer.get();

    out[0] = RESP_SIMPLE_PREFIX;
    if (len) std::memcpy(out + 1, message, len);
    out[len + 1] = RESP_CR;
    out[len + 2] = RESP_LF;

    response.size  = len + 3;
    response.data  = out;
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
    char lenBuf[20];
    const unsigned digits = u64_to_ascii(len, lenBuf);

    const size_t total = 1 + digits + 2 + len + 2;
    auto buffer = std::unique_ptr<char[]>(new char[total]);
    char* out = buffer.get();

    out[0] = RESP_BULK_PREFIX;
    if (digits) std::memcpy(out + 1, lenBuf, digits);
    out[1 + digits] = RESP_CR;
    out[2 + digits] = RESP_LF;
    if (len) std::memcpy(out + 3 + digits, value, len);
    out[3 + digits + len] = RESP_CR;
    out[4 + digits + len] = RESP_LF;

    response.size  = total;
    response.data  = out;
    response.owned = std::move(buffer);
    return response;
}

ResponsePacket makeRespError(const char* message)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;

    const size_t prefixLen = sizeof(RESP_ERROR_PREFIX) - 1;
    const size_t msgLen    = std::strlen(message);
    auto buffer = std::unique_ptr<char[]>(new char[prefixLen + msgLen + 2]);
    char* out = buffer.get();

    if (prefixLen) std::memcpy(out, RESP_ERROR_PREFIX, prefixLen);
    if (msgLen)    std::memcpy(out + prefixLen, message, msgLen);
    out[prefixLen + msgLen]     = RESP_CR;
    out[prefixLen + msgLen + 1] = RESP_LF;

    response.size  = prefixLen + msgLen + 2;
    response.data  = out;
    response.owned = std::move(buffer);
    return response;
}

ResponsePacket makeErrorResponse(RequestProtocol protocol, const char* message)
{
    if (protocol == RequestProtocol::RESP) return makeRespError(message);
    return makeCustomResponse(message);
}

}
