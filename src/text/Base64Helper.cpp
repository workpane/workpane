#include "text/Base64Helper.h"

#include <cstddef>

namespace workpane::text {

// Every four characters carry three bytes, padding only closes the text, and any other character refuses it whole.
std::optional<std::vector<unsigned char>> Base64Helper::decode(std::string_view text) {
    std::size_t length = text.size();

    while (length > 0 && text.size() - length < 2 && text[length - 1] == '=') {
        --length;
    }

    if (text.size() % 4 != 0 || length % 4 == 1) {
        return std::nullopt;
    }

    std::vector<unsigned char> bytes;
    bytes.reserve(length / 4 * 3 + 2);
    unsigned int buffer = 0;
    int bits = 0;

    for (std::size_t index = 0; index < length; ++index) {
        const int digit = value(text[index]);

        if (digit < 0) {
            return std::nullopt;
        }

        buffer = (buffer << 6U) | static_cast<unsigned int>(digit);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<unsigned char>((buffer >> static_cast<unsigned int>(bits)) & 0xFFU));
        }
    }

    return bytes;
}

int Base64Helper::value(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }

    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }

    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }

    if (character == '+') {
        return 62;
    }

    return character == '/' ? 63 : -1;
}

} // namespace workpane::text
