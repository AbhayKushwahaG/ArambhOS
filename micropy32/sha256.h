#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t digest[32]);

// Hash salt + password and format as 64-character lowercase hex string
void sha256_hash_password(const char *salt, const char *password, char out_hex[65]);

// Constant-time compare of two 64-char hex strings (returns 1 on match, 0 on mismatch)
int sha256_verify_password(const char *salt, const char *password, const char *expected_hex);

#endif // SHA256_H
