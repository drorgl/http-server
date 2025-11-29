#include "sha1.h"
#include <string.h> // For memcpy, memset
// #include <arpa/inet.h> // For htonl (if available, otherwise manual byte swap) - Removed as it's not available on all platforms and not strictly needed for this implementation.

// Define SHA1 constants
#define SHA1_K0 0x5A827999
#define SHA1_K1 0x6ED9EBA1
#define SHA1_K2 0x8F1BBCDC
#define SHA1_K3 0xCA62C1D6

// Rotate left macro
#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

// SHA1 compression function
static void sha1_transform(sha1_context_t *context, const uint8_t block[64]) {
    uint32_t a, b, c, d, e;
    uint32_t w[80];

    // Copy block to w and expand
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[i * 4] << 24 |
               (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 |
               (uint32_t)block[i * 4 + 3];
    }

    for (int i = 16; i < 80; i++) {
        w[i] = ROTL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;

        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = SHA1_K0;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = SHA1_K1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = SHA1_K2;
        } else {
            f = b ^ c ^ d;
            k = SHA1_K3;
        }

        uint32_t temp = ROTL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROTL(b, 30);
        b = a;
        a = temp;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
}

void sha1_init(sha1_context_t *context) {
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count[0] = 0;
    context->count[1] = 0;
}

void sha1_update(sha1_context_t *context, const uint8_t *data, size_t len) {
    uint32_t i, j;

    j = context->count[0];
    if ((context->count[0] += (uint32_t)len << 3) < j) {
        context->count[1]++;
    }
    context->count[1] += (uint32_t)(len >> 29);

    j = (j >> 3) & 63;

    if ((j + len) >= 64) {
        memcpy(&context->buffer[j], data, 64 - j);
        sha1_transform(context, context->buffer);
        data += (64 - j);
        len -= (64 - j);
        j = 0;
    }

    while (len >= 64) {
        sha1_transform(context, data);
        data += 64;
        len -= 64;
    }

    memcpy(&context->buffer[j], data, len);
}

void sha1_final(sha1_context_t *context, uint8_t hash[SHA1_HASH_SIZE]) {
    uint32_t i;
    uint8_t final_count[8];
    uint8_t padding[64];

    // Store bit length
    for (i = 0; i < 8; i++) {
        final_count[i] = (uint8_t)((context->count[(i < 4 ? 1 : 0)] >> ((3 - (i & 3)) * 8)) & 255);
    }

    // Pad out to 56 mod 64
    i = (context->count[0] >> 3) & 63;
    padding[0] = 0x80;
    memset(&padding[1], 0, 63);

    if (i < 56) {
        sha1_update(context, padding, 56 - i);
    } else {
        sha1_update(context, padding, 64 - i + 56);
    }

    // Append length
    sha1_update(context, final_count, 8);

    // Output hash
    for (i = 0; i < 5; i++) {
        hash[i * 4 + 0] = (uint8_t)((context->state[i] >> 24) & 0xFF);
        hash[i * 4 + 1] = (uint8_t)((context->state[i] >> 16) & 0xFF);
        hash[i * 4 + 2] = (uint8_t)((context->state[i] >> 8) & 0xFF);
        hash[i * 4 + 3] = (uint8_t)(context->state[i] & 0xFF);
    }
}
