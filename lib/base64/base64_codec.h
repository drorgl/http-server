#ifndef BASE64_CODEC_H
#define BASE64_CODEC_H

#include <stddef.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculates the required output buffer size for Base64 encoding.
 *
 * @param input_len The length of the input binary data.
 * @return The required size of the output buffer (including null terminator).
 */
size_t base64_encoded_length(size_t input_len);

/**
 * @brief Encodes binary data into a Base64 string.
 *
 * @param input Pointer to the input binary data.
 * @param input_len The length of the input binary data.
 * @param output Pointer to the output buffer where the Base64 string will be written.
 * @param output_len The size of the output buffer.
 * @return The number of bytes written to the output buffer (excluding null terminator) on success,
 *         or 0 if the output buffer is too small or input is invalid.
 */
size_t base64_encode(const unsigned char *input, size_t input_len, char *output, size_t output_len);

/**
 * @brief Calculates the required output buffer size for Base64 decoding.
 *
 * @param input_len The length of the input Base64 string.
 * @return The required size of the output buffer (maximum possible decoded size).
 */
size_t base64_decoded_length(const char *input, size_t input_len);

/**
 * @brief Decodes a Base64 string into binary data.
 *
 * @param input Pointer to the input Base64 string.
 * @param input_len The length of the input Base64 string.
 * @param output Pointer to the output buffer where the decoded binary data will be written.
 * @param output_len The size of the output buffer.
 * @return The number of bytes written to the output buffer on success,
 *         or 0 if the output buffer is too small, input is invalid, or contains invalid characters.
 */
size_t base64_decode(const char *input, size_t input_len, unsigned char *output, size_t output_len);

#ifdef __cplusplus
}
#endif

#endif // BASE64_CODEC_H
