#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file sha256.h
 * @brief Minimal, self-contained SHA-256 (FIPS 180-4). No allocation.
 *
 * Used to verify operator PIN hashes received from the server, which are
 * formatted as "sha256:<salt>:<digest>" where
 *   digest = SHA256(salt_string || pin_string)  (ASCII bytes, hex-encoded).
 */

#define SHA256_DIGEST_SIZE 32

/**
 * @brief One-shot hash. Writes SHA256_DIGEST_SIZE bytes to @p out.
 */
void sha256(const uint8_t *data, size_t len, uint8_t out[SHA256_DIGEST_SIZE]);

/**
 * @brief Lowercase hex-encode @p in (len bytes) into @p out.
 *        @p out must hold at least 2*len + 1 bytes (NUL-terminated).
 */
void sha256_to_hex(const uint8_t *in, size_t len, char *out);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
