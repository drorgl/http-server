#include <unity.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "sha1.h"

void setUp(){}
void tearDown(){}

// Helper function to convert hex string to byte array
static void hex_to_bytes(const char *hex_str, uint8_t *byte_arr, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex_str + 2 * i, "%2hhx", &byte_arr[i]);
    }
}

// Test case 1: "abc"
void test_sha1_abc() {
    sha1_context_t context;
    uint8_t hash[SHA1_HASH_SIZE];
    const char *input = "abc";
    const char *expected_hex = "a9993e364706816aba3e25717850c26c9cd0d89d";
    uint8_t expected_hash[SHA1_HASH_SIZE];

    hex_to_bytes(expected_hex, expected_hash, SHA1_HASH_SIZE);

    sha1_init(&context);
    sha1_update(&context, (const uint8_t *)input, strlen(input));
    sha1_final(&context, hash);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, hash, SHA1_HASH_SIZE);
}

// Test case 2: "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
void test_sha1_long_string() {
    sha1_context_t context;
    uint8_t hash[SHA1_HASH_SIZE];
    const char *input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const char *expected_hex = "84983e441c3bd26ebaae4aa1f95129e5e54670f1";
    uint8_t expected_hash[SHA1_HASH_SIZE];

    hex_to_bytes(expected_hex, expected_hash, SHA1_HASH_SIZE);

    sha1_init(&context);
    sha1_update(&context, (const uint8_t *)input, strlen(input));
    sha1_final(&context, hash);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, hash, SHA1_HASH_SIZE);
}

// Test case 3: One million 'a's
void test_sha1_one_million_as() {
    sha1_context_t context;
    uint8_t hash[SHA1_HASH_SIZE];
    const char *expected_hex = "34aa973cd4c4daa4f61eeb2bdbad27316534016f";
    uint8_t expected_hash[SHA1_HASH_SIZE];
    char *input_buffer = (char *)malloc(1000000);
    TEST_ASSERT_NOT_NULL(input_buffer);
    memset(input_buffer, 'a', 1000000);

    hex_to_bytes(expected_hex, expected_hash, SHA1_HASH_SIZE);

    sha1_init(&context);
    sha1_update(&context, (const uint8_t *)input_buffer, 1000000);
    sha1_final(&context, hash);

    free(input_buffer);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_hash, hash, SHA1_HASH_SIZE);
}

int test_hashing(){
    UNITY_BEGIN();

    RUN_TEST(test_sha1_abc);
    RUN_TEST(test_sha1_long_string);
    RUN_TEST(test_sha1_one_million_as);

    return UNITY_END();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering for stdout
    setvbuf(stderr, NULL, _IONBF, 0); // Disable buffering for stderr
    return test_hashing();
    
}

void app_main(void)
{
    main();
}
