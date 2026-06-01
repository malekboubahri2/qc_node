#ifndef PIN_HASH_H
#define PIN_HASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file pin_hash.h
 * @brief Verify operator PINs against the server's encoded hash.
 *
 * The server stores/transmits PINs as "sha256:<hex_salt>:<hex_digest>" where
 *   digest = SHA256(salt_string || pin_string)   (ASCII bytes, lowercase hex).
 * See server/app/security.py:hash_pin. This module re-computes the digest for
 * an entered PIN and compares it to the stored one.
 */

/** @brief No-op initialiser kept for symmetry with other domain modules. */
int pin_hash_init(void);

/**
 * @brief Verify an entered PIN against an encoded hash string.
 *
 * @param pin      Entered PIN (null-terminated).
 * @param encoded  "sha256:<hex_salt>:<hex_digest>" from the server.
 *
 * @return true if the PIN matches, false on mismatch or malformed input.
 */
bool pin_hash_verify_encoded(const char *pin, const char *encoded);

#ifdef __cplusplus
}
#endif

#endif /* PIN_HASH_H */
