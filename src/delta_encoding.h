#pragma once
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>
#include <format>

class DeltaEncoding {
    private:
        static constexpr const char* CHARSET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_!@#$%^&*()-=+[]{};:'\",.<>?/|\\~";
        static constexpr size_t CHARSET_SIZE = 77; // Updated length of CHARSET

        // Helper function to convert a character to its numeric value
        inline static uint8_t charToValue(char c) {
            const char* pos = std::strchr(CHARSET, c);
            if (pos == nullptr) {
                throw std::invalid_argument("Invalid character for Delta Encoding");
            }
            return static_cast<uint8_t>(pos - CHARSET);
        }

        inline static char valueToChar(uint8_t value) {
            if (value >= CHARSET_SIZE) {
                throw std::invalid_argument("Invalid value for Delta Encoding");
            }
            return CHARSET[value];
        }
    public:
        static const char* encode(const char* input) {
            size_t length = std::strlen(input);
            if (length == 0) return nullptr;

            char* compressed = new char[length + 1];
            compressed[0] = input[0]; // Store the first character as-is

            for (size_t i = 1; i < length; ++i) {
                int delta = charToValue(input[i]) - charToValue(input[i - 1]);
                if (delta < 0) delta += CHARSET_SIZE; // Wrap around if delta is negative
                compressed[i] = valueToChar(delta);
            }

            compressed[length] = '\0';
            return compressed;
        }

        static const char* decode(const char* compressed) {
            size_t length = std::strlen(compressed);
            if (length == 0) return nullptr;

            char* decompressed = new char[length + 1];
            decompressed[0] = compressed[0]; // Store the first character as-is

            for (size_t i = 1; i < length; ++i) {
                int delta = charToValue(compressed[i]);
                int prevValue = charToValue(decompressed[i - 1]);
                int originalValue = (prevValue + delta) % CHARSET_SIZE; // Wrap around
                decompressed[i] = valueToChar(originalValue);
            }

            decompressed[length] = '\0';
            return decompressed;
        }
};