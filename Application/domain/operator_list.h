#ifndef OPERATOR_LIST_H
#define OPERATOR_LIST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file operator_list.h
 * @brief Operator PIN list management.
 * 
 * Manages operator information and PIN validation.
 * Config data is received via MQTT on qc/config/operators topic.
 */

/**
 * @brief Operator definition.
 */
typedef struct
{
    int  id;                    /**< Operator ID */
    char name[32 + 1];          /**< Operator name (null-terminated) */
    char pin[8 + 1];            /**< PIN code (null-terminated, max 7 digits + null) */
} operator_entry_t;

/**
 * @brief Initialize the operator list module.
 * 
 * @return 0 on success, negative on error.
 */
int operator_list_init(void);

/**
 * @brief Set the complete operator list configuration.
 * 
 * This function is called when a new operators config is received via MQTT.
 * 
 * @param operators   Array of operator entries
 * @param operator_count Number of operators in the array
 * 
 * @return 0 on success, negative on error.
 */
int operator_list_set(const operator_entry_t *operators, int operator_count);

/**
 * @brief Get the number of configured operators.
 * 
 * @return Number of operators (0 if none configured).
 */
int operator_list_get_count(void);

/**
 * @brief Get an operator by index.
 * 
 * @param idx        Operator index (0-based)
 * @param operator   Pointer to store the operator data
 * 
 * @return 0 on success, negative on error (e.g., index out of bounds).
 */
int operator_list_get(int idx, operator_entry_t *op);

/**
 * @brief Validate an operator PIN.
 * 
 * @param operator_id Operator ID to validate
 * @param pin         PIN to check (null-terminated string)
 * @param operator    Pointer to store operator data if valid (can be NULL)
 * 
 * @return true if PIN is valid for the operator, false otherwise.
 */
bool operator_list_validate_pin(int operator_id, const char *pin,
                                operator_entry_t *op);

#ifdef __cplusplus
}
#endif

#endif /* OPERATOR_LIST_H */
