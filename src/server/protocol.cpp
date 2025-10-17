#include "protocol.hpp"
#include <array>
#include <limits>
#include <atomic>
#include <memory>

namespace {
    constexpr uint16_t RESP_INLINE_INVALID = std::numeric_limits<uint16_t>::max();
    constexpr size_t RESP_INLINE_SLOTS = 256;

    std::atomic<std::size_t> g_respInlineCapacity{255};

    inline std::size_t sanitizeCapacity(std::size_t value) noexcept {
        return value == 0 ? 1 : value;
    }

    inline std::size_t currentCapacity() noexcept {
        return sanitizeCapacity(g_respInlineCapacity.load(std::memory_order_relaxed));
    }

    struct RespInlineArena {
        std::array<uint16_t, RESP_INLINE_SLOTS> freelist{};
        std::unique_ptr<char[]> storage;
        std::size_t slotSize = 0;
        uint16_t freeCount = RESP_INLINE_SLOTS;

        RespInlineArena() noexcept {
            resetStorage(currentCapacity());
        }

        void resetStorage(std::size_t capacity) noexcept {
            slotSize = sanitizeCapacity(capacity);
            storage = std::make_unique<char[]>(slotSize * RESP_INLINE_SLOTS);
            freeCount = RESP_INLINE_SLOTS;
            for (uint16_t i = 0; i < RESP_INLINE_SLOTS; ++i) {
                // Fill the freelist in reverse order so we pop the lowest indices first.
                freelist[i] = static_cast<uint16_t>(RESP_INLINE_SLOTS - 1 - i);
            }
        }

        void ensureCapacity() noexcept {
            const auto desired = currentCapacity();
            if (!storage) {
                resetStorage(desired);
            } else if (desired != slotSize && freeCount == RESP_INLINE_SLOTS) {
                resetStorage(desired);
            }
        }

        char* acquire(uint16_t& index, size_t required) noexcept {
            ensureCapacity();
            if (required > slotSize || freeCount == 0) {
                index = RESP_INLINE_INVALID;
                return nullptr;
            }

            index = freelist[--freeCount];
            return storage.get() + static_cast<std::size_t>(index) * slotSize;
        }

        void release(uint16_t index) noexcept {
            if (index < RESP_INLINE_SLOTS && freeCount < RESP_INLINE_SLOTS) {
                freelist[freeCount++] = index;
                if (freeCount == RESP_INLINE_SLOTS) {
                    const auto desired = currentCapacity();
                    if (desired != slotSize) {
                        resetStorage(desired);
                    }
                }
            }
        }

        char* pointer(uint16_t index) noexcept {
            ensureCapacity();
            if (index < RESP_INLINE_SLOTS && storage) {
                return storage.get() + static_cast<std::size_t>(index) * slotSize;
            }
            return nullptr;
        }
    };

    thread_local RespInlineArena g_respInlineArena;
}

namespace server {

void setRespInlineCapacity(std::size_t capacity) noexcept {
    g_respInlineCapacity.store(sanitizeCapacity(capacity), std::memory_order_relaxed);
}

std::size_t respInlineCapacity() noexcept {
    return currentCapacity();
}

char* ResponsePacket::tryUseInline(size_t required) noexcept {
    if (inlineIndex != RESP_INLINE_INVALID) {
        g_respInlineArena.release(inlineIndex);
        inlineIndex = RESP_INLINE_INVALID;
    }
    owned.reset();
    data = nullptr;
    size = 0;

    uint16_t index = RESP_INLINE_INVALID;
    char* slot = g_respInlineArena.acquire(index, required);
    if (!slot) {
        return nullptr;
    }

    inlineIndex = index;
    data = slot;
    size = required;
    return slot;
}

void ResponsePacket::setOwnedBuffer(std::unique_ptr<char[]> buffer, size_t length) noexcept {
    if (inlineIndex != RESP_INLINE_INVALID) {
        g_respInlineArena.release(inlineIndex);
        inlineIndex = RESP_INLINE_INVALID;
    }
    owned = std::move(buffer);
    data = owned ? owned.get() : nullptr;
    size = length;
}

void ResponsePacket::setStaticData(const char* ptr, size_t length) noexcept {
    if (inlineIndex != RESP_INLINE_INVALID) {
        g_respInlineArena.release(inlineIndex);
        inlineIndex = RESP_INLINE_INVALID;
    }
    owned.reset();
    data = ptr;
    size = length;
}

ResponsePacket& ResponsePacket::operator=(ResponsePacket&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    if (inlineIndex != RESP_INLINE_INVALID) {
        g_respInlineArena.release(inlineIndex);
        inlineIndex = RESP_INLINE_INVALID;
    }

    protocol = other.protocol;
    size = other.size;
    if (other.inlineIndex != RESP_INLINE_INVALID) {
        inlineIndex = other.inlineIndex;
        data = g_respInlineArena.pointer(inlineIndex);
        owned.reset();
    } else {
        inlineIndex = RESP_INLINE_INVALID;
        owned = std::move(other.owned);
        if (owned) {
            data = owned.get();
        } else {
            data = other.data;
        }
    }

    other.data = nullptr;
    other.size = 0;
    other.inlineIndex = RESP_INLINE_INVALID;
    other.owned.reset();

    return *this;
}

ResponsePacket::~ResponsePacket() noexcept {
    if (inlineIndex != RESP_INLINE_INVALID) {
        g_respInlineArena.release(inlineIndex);
        inlineIndex = RESP_INLINE_INVALID;
    }
    data = nullptr;
    size = 0;
}

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
    if (!read_num(elements) || elements < 1 || elements > 3) return false;

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
    if (!parts.command) return false;
    if (elements >= 2 && !parts.key) return false;
    if (elements == 3 && !parts.value) return false;
    return true;
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
    const size_t total = len + 3;
    char* out = response.tryUseInline(total);
    if (!out) {
        auto buffer = std::unique_ptr<char[]>(new char[total]);
        out = buffer.get();
        response.setOwnedBuffer(std::move(buffer), total);
    }

    out[0] = RESP_SIMPLE_PREFIX;
    if (len) std::memcpy(out + 1, message, len);
    out[len + 1] = RESP_CR;
    out[len + 2] = RESP_LF;

    return response;
}

ResponsePacket makeRespInteger(int64_t value)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;

    uint64_t magnitude = value < 0 ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
    char digitsBuf[20];
    const unsigned digits = u64_to_ascii(magnitude, digitsBuf);

    const bool negative = value < 0;
    const size_t total = 1 + (negative ? 1 : 0) + digits + 2;
    char* out = response.tryUseInline(total);
    if (!out) {
        auto buffer = std::unique_ptr<char[]>(new char[total]);
        out = buffer.get();
        response.setOwnedBuffer(std::move(buffer), total);
    }

    out[0] = RESP_INTEGER_PREFIX;
    size_t offset = 1;
    if (negative) {
        out[offset++] = '-';
    }
    if (digits) std::memcpy(out + offset, digitsBuf, digits);
    offset += digits;
    out[offset++] = RESP_CR;
    out[offset++] = RESP_LF;

    return response;
}

ResponsePacket makeRespBulkString(const char* value)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;

    if (value == NOTHING) {
        response.setStaticData(RESP_NULL_BULK, std::strlen(RESP_NULL_BULK));
        return response;
    }

    const size_t len = std::strlen(value);
    char lenBuf[20];
    const unsigned digits = u64_to_ascii(len, lenBuf);

    const size_t total = 1 + digits + 2 + len + 2;
    char* out = response.tryUseInline(total);
    if (!out) {
        auto buffer = std::unique_ptr<char[]>(new char[total]);
        out = buffer.get();
        response.setOwnedBuffer(std::move(buffer), total);
    }

    out[0] = RESP_BULK_PREFIX;
    if (digits) std::memcpy(out + 1, lenBuf, digits);
    out[1 + digits] = RESP_CR;
    out[2 + digits] = RESP_LF;
    if (len) std::memcpy(out + 3 + digits, value, len);
    out[3 + digits + len] = RESP_CR;
    out[4 + digits + len] = RESP_LF;

    return response;
}

ResponsePacket makeRespArray(const std::vector<ResponsePacket>& elements)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;

    char lenBuf[20];
    const unsigned digits = u64_to_ascii(elements.size(), lenBuf);

    size_t total = 1 + digits + 2;
    for (const auto& element : elements) {
        total += element.size;
    }

    char* out = response.tryUseInline(total);
    if (!out) {
        auto buffer = std::unique_ptr<char[]>(new char[total]);
        out = buffer.get();
        response.setOwnedBuffer(std::move(buffer), total);
    }

    out[0] = RESP_ARRAY_PREFIX;
    if (digits) std::memcpy(out + 1, lenBuf, digits);
    size_t offset = 1 + digits;
    out[offset++] = RESP_CR;
    out[offset++] = RESP_LF;

    for (const auto& element : elements) {
        if (element.size) {
            std::memcpy(out + offset, element.data, element.size);
        }
        offset += element.size;
    }

    return response;
}

ResponsePacket makeRespError(const char* message)
{
    ResponsePacket response{};
    response.protocol = RequestProtocol::RESP;

    const size_t prefixLen = std::strlen(RESP_ERROR_PREFIX);
    const size_t msgLen    = std::strlen(message);
    const size_t total     = prefixLen + msgLen + 2;
    char* out = response.tryUseInline(total);
    if (!out) {
        auto buffer = std::unique_ptr<char[]>(new char[total]);
        out = buffer.get();
        response.setOwnedBuffer(std::move(buffer), total);
    }

    if (prefixLen) std::memcpy(out, RESP_ERROR_PREFIX, prefixLen);
    if (msgLen)    std::memcpy(out + prefixLen, message, msgLen);
    out[prefixLen + msgLen]     = RESP_CR;
    out[prefixLen + msgLen + 1] = RESP_LF;

    return response;
}

ResponsePacket makeErrorResponse(RequestProtocol protocol, const char* message)
{
    if (protocol == RequestProtocol::RESP) return makeRespError(message);
    return makeCustomResponse(message);
}

}
