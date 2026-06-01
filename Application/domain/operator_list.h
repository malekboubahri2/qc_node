#ifndef OPERATOR_LIST_H
#define OPERATOR_LIST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file operator_list.h
 * @brief Operator list + PIN validation (from qc/config/operators).
 *
 * The server sends each operator's PIN as a hash string, never the plaintext
 * PIN. Format: "sha256:<hex_salt>:<hex_digest>" where
 *   digest = SHA256(salt_string || pin_string). Validation re-hashes the
 * entered PIN and compares (see pin_hash.h / sha256.h).
 */

/* Holds "sha256:<16 hex>:<64 hex>" = 7 + 16 + 1 + 64 = 88 chars + NUL. */
#define OPERATOR_PIN_HASH_MAX 96

/** A single operator. */
typedef struct
{
    int  id;                              /**< Server-side operator id. */
    char name[32 + 1];                    /**< Display name, null-terminated. */
    char pin_hash[OPERATOR_PIN_HASH_MAX]; /**< Encoded PIN hash from server. */
} operator_entry_t;

/**
 * @brief Initialise the module. Idempotent: clears storage only on the first
 *        call (see defect_config_init for the rationale).
 */
int operator_list_init(void);

/** @brief Replace the operator list. @return 0 on success, negative on error. */
int operator_list_set(const operator_entry_t *operators, int operator_count);

/** @return Number of configured operators (0 if none). */
int operator_list_get_count(void);

/** @return 0 and fills @p op on success, negative if @p idx out of range. */
int operator_list_get(int idx, operator_entry_t *op);

/**
 * @brief Find the operator whose stored PIN hash matches @p pin.
 *
 * @param[in]  pin      Entered PIN (null-terminated digit string).
 * @param[out] out_idx  Receives the matching operator index (may be NULL).
 *
 * @return true if a match was found.
 */
bool operator_list_check_pin(const char *pin, int *out_idx);

#ifdef __cplusplus
}
#endif

#endif /* OPERATOR_LIST_H */
