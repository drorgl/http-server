#ifndef SHA1_H
#define SHA1_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define SHA1_HASH_SIZE 20

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} sha1_context_t;

/**
 * @brief Initializes the SHA1 context.
 *
 * @param context Pointer to the SHA1 context structure.
 */
void sha1_init(sha1_context_t *context);

/**
 * @brief Updates the SHA1 hash with a block of data.
 *
 * @param context Pointer to the SHA1 context structure.
 * @param data Pointer to the input data.
 * @param len Length of the input data in bytes.
 */
void sha1_update(sha1_context_t *context, const uint8_t *data, size_t len);

/**
 * @brief Finalizes the SHA1 hash computation and stores the result.
 *
 * @param context Pointer to the SHA1 context structure.
 * @param hash Pointer to a buffer where the 20-byte SHA1 hash will be stored.
 */
void sha1_final(sha1_context_t *context, uint8_t hash[SHA1_HASH_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SHA1_H */
