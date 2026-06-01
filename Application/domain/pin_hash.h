#ifndef PIN_HASH_H
#define PIN_HASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file pin_hash.h
 * @brief Secure PIN hashing for operator authentication.
 * 
 * Provides secure storage and verification of operator PINs.
 * Supports Argon2 if enabled, otherwise falls back to SHA-256 with salt.
 */

 /**
 * @brief Initialize the PIN hash module.
 * 
 * @return 0 on success, negative on error.
 */
int pin_hash_init(void);

/**
 * @brief Hash a PIN for secure storage.
 * 
 * @param pin         PIN to hash (null-terminated string)
 * @param salt        Salt value (if NULL, a random salt is generated)
 * @param salt_len    Length of salt in bytes
 * @param hash_buf    Buffer to store the resulting hash
 * @param hash_buf_len Size of hash buffer
 * @param salt_out    Buffer to store the salt used (can be NULL)
 * @param salt_out_len Size of salt output buffer
 * 
 * @return 0 on success, negative on error.
 */
int pin_hash_hash(const char *pin, const uint8_t *salt, int salt_len,
                 uint8_t *hash_buf, int hash_buf_len,
                 uint8_t *salt_out, int salt_out_len);

/**
 * @brief Verify a PIN against a stored hash.
 * 
 * @param pin         PIN to verify (null-terminated string)
 * @param salt        Salt used when the hash was created
 * @param salt_len    Length of salt in bytes
 * @param hash        Hash value to verify against
 * @param hash_len    Length of hash in bytes
 * 
 * @return true if PIN matches the hash, false otherwise.
 */
bool pin_hash_verify(const char *pin, const uint8_t *salt, int salt_len,
                    const uint8_t *hash, int hash_len);

#ifdef APP_FEATURE_ARGON2_PIN
/**
 * @brief Argon2-specific parameters (if enabled)
 */
typedef struct
{
    uint32_t time_cost;      /**< Number of iterations */
    uint32_t memory_cost;    /**< Memory usage in KB */
    uint32_t parallelism;    /**< Number of threads */
} pin_hash_argon2_params_t;

/**
 * @brief Get the default Argon2 parameters.
 * 
 * @param params Pointer to store the parameters
 */
void pin_hash_get_argon2_params(pin_hash_argon2_params_t *params);
#endif /* APP_FEATURE_ARGON2_PIN */

#ifdef __cplusplus
}
#endif

#endif /* PIN_HASH_H */
