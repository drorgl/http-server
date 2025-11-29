#include "unity.h"
#include "base64_codec.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h> // For malloc, free

void setUp(){}
void tearDown(){}

// Helper to print arrays for debugging
void print_array(const char *name, const unsigned char *arr, size_t len) {
    printf("%s [%zu]: ", name, len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", arr[i]);
    }
    printf("\n");
}

#ifdef __cplusplus
extern "C" {
#endif

// Test functions for base64_encoded_length
void test_base64_encoded_length_zero_input(void) {
    TEST_ASSERT_EQUAL(0, base64_encoded_length(0));
}

void test_base64_encoded_length_one_input(void) {
    TEST_ASSERT_EQUAL(4, base64_encoded_length(1));
}

void test_base64_encoded_length_two_input(void) {
    TEST_ASSERT_EQUAL(4, base64_encoded_length(2));
}

void test_base64_encoded_length_three_input(void) {
    TEST_ASSERT_EQUAL(4, base64_encoded_length(3));
}

void test_base64_encoded_length_multiple_input(void) {
    TEST_ASSERT_EQUAL(8, base64_encoded_length(5));
    TEST_ASSERT_EQUAL(12, base64_encoded_length(9));
}

// Test functions for base64_encode
void test_base64_encode_empty_string(void) {
    const unsigned char input[] = "";
    char output[10];
    size_t encoded_len = base64_encode(input, 0, output, sizeof(output));
    TEST_ASSERT_EQUAL(0, encoded_len);
    TEST_ASSERT_EQUAL_STRING("", output);
}

void test_base64_encode_single_char(void) {
    const unsigned char input[] = "A";
    char output[10];
    size_t encoded_len = base64_encode(input, 1, output, sizeof(output));
    TEST_ASSERT_EQUAL(4, encoded_len);
    TEST_ASSERT_EQUAL_STRING("QQ==", output);
}

void test_base64_encode_two_chars(void) {
    const unsigned char input[] = "AB";
    char output[10];
    size_t encoded_len = base64_encode(input, 2, output, sizeof(output));
    TEST_ASSERT_EQUAL(4, encoded_len);
    TEST_ASSERT_EQUAL_STRING("QUI=", output);
}

void test_base64_encode_three_chars(void) {
    const unsigned char input[] = "ABC";
    char output[10];
    size_t encoded_len = base64_encode(input, 3, output, sizeof(output));
    TEST_ASSERT_EQUAL(4, encoded_len);
    TEST_ASSERT_EQUAL_STRING("QUJD", output);
}

void test_base64_encode_long_string(void) {
    const unsigned char input[] = "Man is distinguished, not only by his reason, but by this singular passion from other animals, which is a desire of knowledge, that by a perseverance of delight in the continued and indefatigable generation of knowledge, exceeds the short vehemence of any carnal pleasure.";
    size_t input_len = strlen((char*)input);
    size_t required_output_len = base64_encoded_length(input_len) + 1; // +1 for null terminator
    char *output = (char*)malloc(required_output_len);
    TEST_ASSERT_NOT_NULL(output);
    size_t encoded_len = base64_encode(input, input_len, output, required_output_len);
    TEST_ASSERT_EQUAL(364, encoded_len);
    TEST_ASSERT_EQUAL_STRING("TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlzIHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGRlc2lyZSBvZiBrbm93bGVkZ2UsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGludWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRoZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4=", output);
    free(output);
}

void test_base64_encode_buffer_too_small(void) {
    const unsigned char input[] = "ABC";
    char output[4]; // Too small for "QUJD\0"
    size_t encoded_len = base64_encode(input, 3, output, sizeof(output));
    TEST_ASSERT_EQUAL(0, encoded_len);
}

// Test functions for base64_decoded_length
void test_base64_decoded_length_empty_string(void) {
    TEST_ASSERT_EQUAL(0, base64_decoded_length("", 0));
}

void test_base64_decoded_length_no_padding(void) {
    TEST_ASSERT_EQUAL(3, base64_decoded_length("QUJD", 4)); // "QUJD" -> 3 bytes
    TEST_ASSERT_EQUAL(6, base64_decoded_length("QUJDQUJD", 8)); // "QUJDQUJD" -> 6 bytes
}

void test_base64_decoded_length_one_padding(void) {
    TEST_ASSERT_EQUAL(2, base64_decoded_length("QUI=", 4)); // "QUI=" -> 2 bytes
}

void test_base64_decoded_length_two_padding(void) {
    TEST_ASSERT_EQUAL(1, base64_decoded_length("QQ==", 4)); // "QQ==" -> 1 byte
}

// Test functions for base64_decode
void test_base64_decode_empty_string(void) {
    const char input[] = "";
    unsigned char output[10] = {0};
    size_t decoded_len = base64_decode(input, 0, output, sizeof(output));
    TEST_ASSERT_EQUAL(0, decoded_len);
}

void test_base64_decode_single_char_padding(void) {
    const char input[] = "QQ==";
    unsigned char output[10] = {0};
    size_t decoded_len = base64_decode(input, strlen(input), output, sizeof(output));
    TEST_ASSERT_EQUAL(1, decoded_len);
    TEST_ASSERT_EQUAL_STRING("A", (char*)output);
}

void test_base64_decode_two_chars_padding(void) {
    const char input[] = "QUI=";
    unsigned char output[10] = {0};
    size_t decoded_len = base64_decode(input, strlen(input), output, sizeof(output));
    TEST_ASSERT_EQUAL(2, decoded_len);
    TEST_ASSERT_EQUAL_STRING("AB", (char*)output);
}

void test_base64_decode_three_chars_no_padding(void) {
    const char input[] = "QUJD";
    unsigned char output[10] = {0};
    size_t decoded_len = base64_decode(input, strlen(input), output, sizeof(output));
    TEST_ASSERT_EQUAL(3, decoded_len);
    TEST_ASSERT_EQUAL_STRING("ABC", (char*)output);
}

void test_base64_decode_long_string(void) {
    const char input[] = "TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlzIHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGRlc2lyZSBvZiBrbm93bGVkZ2UsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGludWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRoZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4=";
    const unsigned char expected_output[] = "Man is distinguished, not only by his reason, but by this singular passion from other animals, which is a desire of knowledge, that by a perseverance of delight in the continued and indefatigable generation of knowledge, exceeds the short vehemence of any carnal pleasure.";
    size_t expected_len = strlen((char*)expected_output);
    unsigned char output[300]; // Ensure buffer is large enough
    size_t decoded_len = base64_decode(input, strlen(input), output, sizeof(output));
    TEST_ASSERT_EQUAL(expected_len, decoded_len);
    output[decoded_len] = '\0'; // Null-terminate for string comparison
    TEST_ASSERT_EQUAL_STRING((char*)expected_output, (char*)output);
}

void test_base64_decode_invalid_length(void) {
    const char input[] = "ABC"; // Length 3, not multiple of 4
    unsigned char output[10] = {0};
    size_t decoded_len = base64_decode(input, strlen(input), output, sizeof(output));
    TEST_ASSERT_EQUAL(0, decoded_len);
}

void test_base64_decode_invalid_char(void) {
    const char input[] = "A!@#"; // Invalid characters
    unsigned char output[10] = {0};
    size_t decoded_len = base64_decode(input, strlen(input), output, sizeof(output));
    TEST_ASSERT_EQUAL(0, decoded_len);
}

void test_base64_decode_buffer_too_small(void) {
    const char input[] = "QUJD"; // Decodes to "ABC" (3 bytes)
    unsigned char output[2]; // Too small
    size_t decoded_len = base64_decode(input, strlen(input), output, sizeof(output));
    TEST_ASSERT_EQUAL(0, decoded_len);
}

void test_base64_encode_decode_roundtrip(void) {
    const unsigned char original_data[] = "Hello, Base64! This is a test string with some special characters: !@#$%^&*()_+-=[]{}|;':\",./<>?`~ and also some longer data to ensure proper padding handling.";
    size_t original_len = strlen((char*)original_data);

    size_t encoded_buf_len = base64_encoded_length(original_len) + 1; // +1 for null terminator
    char *encoded_data = (char*)malloc(encoded_buf_len);
    TEST_ASSERT_NOT_NULL(encoded_data);

    size_t actual_encoded_len = base64_encode(original_data, original_len, encoded_data, encoded_buf_len);
    TEST_ASSERT_TRUE(actual_encoded_len > 0);

    size_t decoded_buf_len = base64_decoded_length(encoded_data, actual_encoded_len);
    unsigned char *decoded_data = (unsigned char*)malloc(decoded_buf_len + 1); // +1 for null terminator for string comparison
    TEST_ASSERT_NOT_NULL(decoded_data);

    size_t actual_decoded_len = base64_decode(encoded_data, actual_encoded_len, decoded_data, decoded_buf_len);
    TEST_ASSERT_EQUAL(original_len, actual_decoded_len);
    decoded_data[actual_decoded_len] = '\0'; // Null-terminate for string comparison

    TEST_ASSERT_EQUAL_STRING((char*)original_data, (char*)decoded_data);

    free(encoded_data);
    free(decoded_data);
}

void test_base64_encode_decode_roundtrip_binary_data(void) {
    unsigned char original_data[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                     0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                                     0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
                                     0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
                                     0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
                                     0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
                                     0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
                                     0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
                                     0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
                                     0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
                                     0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
                                     0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
                                     0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
                                     0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
                                     0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
                                     0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};
    size_t original_len = sizeof(original_data);

    size_t encoded_buf_len = base64_encoded_length(original_len) + 1; // +1 for null terminator
    char *encoded_data = (char*)malloc(encoded_buf_len);
    TEST_ASSERT_NOT_NULL(encoded_data);

    size_t actual_encoded_len = base64_encode(original_data, original_len, encoded_data, encoded_buf_len);
    TEST_ASSERT_TRUE(actual_encoded_len > 0);

    size_t decoded_buf_len = base64_decoded_length(encoded_data, actual_encoded_len);
    unsigned char *decoded_data = (unsigned char*)malloc(decoded_buf_len);
    TEST_ASSERT_NOT_NULL(decoded_data);

    size_t actual_decoded_len = base64_decode(encoded_data, actual_encoded_len, decoded_data, decoded_buf_len);
    TEST_ASSERT_EQUAL(original_len, actual_decoded_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(original_data, decoded_data, original_len);

    free(encoded_data);
    free(decoded_data);
}

int main(void) {
    UNITY_BEGIN();

    // Run tests for base64_encoded_length
    RUN_TEST(test_base64_encoded_length_zero_input);
    RUN_TEST(test_base64_encoded_length_one_input);
    RUN_TEST(test_base64_encoded_length_two_input);
    RUN_TEST(test_base64_encoded_length_three_input);
    RUN_TEST(test_base64_encoded_length_multiple_input);

    // Run tests for base64_encode
    RUN_TEST(test_base64_encode_empty_string);
    RUN_TEST(test_base64_encode_single_char);
    RUN_TEST(test_base64_encode_two_chars);
    RUN_TEST(test_base64_encode_three_chars);
    RUN_TEST(test_base64_encode_long_string);
    RUN_TEST(test_base64_encode_buffer_too_small);

    // Run tests for base64_decoded_length
    RUN_TEST(test_base64_decoded_length_empty_string);
    RUN_TEST(test_base64_decoded_length_no_padding);
    RUN_TEST(test_base64_decoded_length_one_padding);
    RUN_TEST(test_base64_decoded_length_two_padding);

    // Run tests for base64_decode
    RUN_TEST(test_base64_decode_empty_string);
    RUN_TEST(test_base64_decode_single_char_padding);
    RUN_TEST(test_base64_decode_two_chars_padding);
    RUN_TEST(test_base64_decode_three_chars_no_padding);
    RUN_TEST(test_base64_decode_long_string);
    RUN_TEST(test_base64_decode_invalid_length);
    RUN_TEST(test_base64_decode_invalid_char);
    RUN_TEST(test_base64_decode_buffer_too_small);

    // Run roundtrip tests
    RUN_TEST(test_base64_encode_decode_roundtrip);
    RUN_TEST(test_base64_encode_decode_roundtrip_binary_data);

    return UNITY_END();
}

void app_main(void)
{
    main();
}


#ifdef __cplusplus
}
#endif
