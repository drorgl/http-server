#include "base64_codec.h"
#include <string.h> // For strlen
#include <stdlib.h> // For malloc, free
#include <stdint.h> // For uint8_t

// Base64 character set
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 padding character
static const char base64_pad = '=';

// Helper function to get the value of a base64 character
static int base64_char_to_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == base64_pad) return 0; // Padding character, treated as 0 for decoding
    return -1; // Invalid character
}

size_t base64_encoded_length(size_t input_len) {
    if (input_len == 0) {
        return 0;
    }
    return ((input_len + 2) / 3) * 4;
}

size_t base64_encode(const unsigned char *input, size_t input_len, char *output, size_t output_len) {
    if ((!input && input_len > 0) || !output) {
        return 0;
    }

    if (input_len == 0) {
        if (output_len < 1) {
            return 0;
        }
        output[0] = '\0';
        return 0;
    }

    size_t required_len = base64_encoded_length(input_len);
    if (output_len < required_len + 1) { // +1 for null terminator
        return 0; // Output buffer too small
    }

    size_t i = 0, j = 0;
    while (i < input_len) {
        uint32_t octet_a = input[i++];
        uint32_t octet_b = (i < input_len) ? input[i++] : 0;
        uint32_t octet_c = (i < input_len) ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = base64_chars[(triple >> 18) & 0x3F];
        output[j++] = base64_chars[(triple >> 12) & 0x3F];
        output[j++] = base64_chars[(triple >> 6) & 0x3F];
        output[j++] = base64_chars[triple & 0x3F];
    }

    // Add padding
    int remainder = input_len % 3;
    if (remainder == 1) {
        output[j - 2] = base64_pad;
        output[j - 1] = base64_pad;
    } else if (remainder == 2) {
        output[j - 1] = base64_pad;
    }

    output[j] = '\0';
    return j;
}

size_t base64_decoded_length(const char *input, size_t input_len) {
    if (input_len == 0) {
        return 0;
    }
    // Each 4 bytes of base64 output become up to 3 bytes of binary data.
    // We need to account for padding.
    size_t num_padding_chars = 0;
    if (input_len >= 2 && input_len % 4 == 0) {
        if (base64_pad == input[input_len - 1]) {
            num_padding_chars++;
        }
        if (base64_pad == input[input_len - 2]) {
            num_padding_chars++;
        }
    }
    return (input_len / 4) * 3 - num_padding_chars;
}

size_t base64_decode(const char *input, size_t input_len, unsigned char *output, size_t output_len) {
    if ((!input && input_len > 0) || !output) {
        return 0;
    }

    if (input_len == 0) {
        return 0;
    }

    if (input_len % 4 != 0) {
        return 0; // Invalid length
    }

    size_t required_len = base64_decoded_length(input, input_len);
    if (output_len < required_len) {
        return 0; // Output buffer too small
    }

    size_t i = 0, j = 0;
    while (i < input_len) {
        int val[4];
        val[0] = base64_char_to_value(input[i++]);
        val[1] = base64_char_to_value(input[i++]);
        val[2] = base64_char_to_value(input[i++]);
        val[3] = base64_char_to_value(input[i++]);

        if (val[0] == -1 || val[1] == -1 || val[2] == -1 || val[3] == -1) {
            return 0; // Invalid character
        }

        uint32_t quadruple = (val[0] << 18) | (val[1] << 12) | (val[2] << 6) | val[3];

        output[j++] = (quadruple >> 16) & 0xFF;
        if (input[i - 2] != base64_pad) {
            output[j++] = (quadruple >> 8) & 0xFF;
        }
        if (input[i - 1] != base64_pad) {
            output[j++] = quadruple & 0xFF;
        }
    }

    return j;
}
