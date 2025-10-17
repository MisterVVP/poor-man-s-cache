#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>
#include "protocol.hpp"

using namespace server;

TEST(RespProtocolTest, ParseRespMessageLengthComplete)
{
    const char request[] = "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    std::vector<char> buffer(request, request + sizeof(request) - 1);
    auto result = parseRespMessageLength(buffer, 0);
    ASSERT_EQ(result.status, RespParseStatus::Complete);
    ASSERT_EQ(result.length, sizeof(request) - 1);
}

TEST(RespProtocolTest, ParseRespMessageLengthIncomplete)
{
    const char partial[] = "*2\r\n$3\r\nGET\r\n$3\r\nfoo";
    std::vector<char> buffer(partial, partial + sizeof(partial) - 1);
    auto result = parseRespMessageLength(buffer, 0);
    ASSERT_EQ(result.status, RespParseStatus::Incomplete);
}

TEST(RespProtocolTest, ParseRespCommandGet)
{
    std::string payload = "*2\r\n$3\r\nGET\r\n$3\r\nbar\r\n";
    RespCommandParts parts{};
    ASSERT_TRUE(parseRespCommand(payload, parts));
    ASSERT_EQ(parts.argc, 2u);
    ASSERT_STREQ(parts.command, GET_STR);
    ASSERT_STREQ(parts.key, "bar");
    ASSERT_EQ(parts.value, nullptr);
}

TEST(RespProtocolTest, ParseRespCommandSet)
{
    std::string payload = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    RespCommandParts parts{};
    ASSERT_TRUE(parseRespCommand(payload, parts));
    ASSERT_EQ(parts.argc, 3u);
    ASSERT_STREQ(parts.command, SET_STR);
    ASSERT_STREQ(parts.key, "key");
    ASSERT_STREQ(parts.value, "value");
}

TEST(RespProtocolTest, MakeRespSimpleString)
{
    auto response = makeRespSimpleString(OK);
    ASSERT_EQ(response.protocol, RequestProtocol::RESP);
    ASSERT_EQ(response.size, 5u);
    ASSERT_TRUE(response.owned || response.usesInlineStorage());
    std::string serialized(response.data, response.size);
    ASSERT_EQ(serialized, "+OK\r\n");
}

TEST(RespProtocolTest, MakeRespBulkString)
{
    auto response = makeRespBulkString("hello");
    ASSERT_EQ(response.protocol, RequestProtocol::RESP);
    ASSERT_EQ(response.size, 11u);
    ASSERT_TRUE(response.owned || response.usesInlineStorage());
    std::string serialized(response.data, response.size);
    ASSERT_EQ(serialized, "$5\r\nhello\r\n");
}

TEST(RespProtocolTest, MakeRespInteger)
{
    auto positive = makeRespInteger(1);
    ASSERT_EQ(positive.protocol, RequestProtocol::RESP);
    ASSERT_TRUE(positive.owned || positive.usesInlineStorage());
    std::string serializedPos(positive.data, positive.size);
    ASSERT_EQ(serializedPos, ":1\r\n");

    auto negative = makeRespInteger(-1);
    ASSERT_EQ(negative.protocol, RequestProtocol::RESP);
    ASSERT_TRUE(negative.owned || negative.usesInlineStorage());
    std::string serializedNeg(negative.data, negative.size);
    ASSERT_EQ(serializedNeg, ":-1\r\n");
}

TEST(RespProtocolTest, MakeRespArray)
{
    std::vector<ResponsePacket> elements;
    elements.emplace_back(makeRespSimpleString(OK));
    elements.emplace_back(makeRespInteger(1));
    elements.emplace_back(makeRespBulkString("value"));

    auto response = makeRespArray(elements);
    ASSERT_EQ(response.protocol, RequestProtocol::RESP);
    ASSERT_TRUE(response.owned || response.usesInlineStorage());
    std::string serialized(response.data, response.size);
    ASSERT_EQ(serialized, "*3\r\n+OK\r\n:1\r\n$5\r\nvalue\r\n");
}

TEST(RespProtocolTest, MakeRespNullBulkString)
{
    auto response = makeRespBulkString(NOTHING);
    ASSERT_EQ(response.protocol, RequestProtocol::RESP);
    ASSERT_EQ(response.size, 5u);
    ASSERT_EQ(response.owned, nullptr);
    std::string serialized(response.data, response.size);
    ASSERT_EQ(serialized, "$-1\r\n");
}

TEST(RespProtocolTest, ErrorResponseMatchesProtocol)
{
    auto customError = makeErrorResponse(RequestProtocol::Custom, UNKNOWN_COMMAND);
    ASSERT_EQ(customError.protocol, RequestProtocol::Custom);
    ASSERT_EQ(customError.size, std::strlen(UNKNOWN_COMMAND));
    ASSERT_EQ(customError.owned, nullptr);

    auto respError = makeErrorResponse(RequestProtocol::RESP, UNKNOWN_COMMAND);
    ASSERT_EQ(respError.protocol, RequestProtocol::RESP);
    ASSERT_TRUE(respError.owned || respError.usesInlineStorage());
    std::string serialized(respError.data, respError.size);
    ASSERT_EQ(serialized, "-ERR ERROR: Unknown command\r\n");
}

TEST(CustomProtocolTest, MakeCustomResponse)
{
    auto response = makeCustomResponse(OK);
    ASSERT_EQ(response.protocol, RequestProtocol::Custom);
    ASSERT_EQ(response.data, OK);
    ASSERT_EQ(response.size, std::strlen(OK));
    ASSERT_EQ(response.owned, nullptr);
}
