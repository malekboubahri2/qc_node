#include "domain/pin_hash.h"
#include "domain/sha256.h"
#include <string.h>
#include <stdio.h>

int pin_hash_init(void)
{
    return 0;
}

/* Case-insensitive, length-checked compare of two hex strings. */
static bool hex_equal(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

bool pin_hash_verify_encoded(const char *pin, const char *encoded)
{
    if (!pin || !encoded) {
        return false;
    }

    /* Expect "sha256:<salt>:<digest>". */
    const char *p = encoded;
    if (strncmp(p, "sha256:", 7) != 0) {
        return false;
    }
    p += 7;

    const char *salt = p;
    const char *colon = strchr(p, ':');
    if (!colon) {
        return false;
    }
    size_t salt_len = (size_t)(colon - salt);
    const char *expected = colon + 1;
    size_t expected_len = strlen(expected);

    if (salt_len == 0 || salt_len > 32 || expected_len != SHA256_DIGEST_SIZE * 2) {
        return false;
    }

    /* Build salt_string || pin_string and hash it. */
    char msg[32 + 16]; /* salt (<=32) + pin (login PIN is short) */
    size_t pin_len = strlen(pin);
    if (salt_len + pin_len > sizeof(msg)) {
        return false;
    }
    memcpy(msg, salt, salt_len);
    memcpy(msg + salt_len, pin, pin_len);

    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256((const uint8_t *)msg, salt_len + pin_len, digest);

    char hex[SHA256_DIGEST_SIZE * 2 + 1];
    sha256_to_hex(digest, SHA256_DIGEST_SIZE, hex);

    return hex_equal(hex, expected, SHA256_DIGEST_SIZE * 2);
}
