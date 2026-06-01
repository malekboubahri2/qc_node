#ifndef SESSION_H
#define SESSION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file session.h
 * @brief Session management for QC operations.
 * 
 * Tracks the current operator, product, and session statistics.
 */

 /**
 * @brief Initialize the session module.
 * 
 * @return 0 on success, negative on error.
 */
int session_init(void);

/**
 * @brief Start a new session.
 * 
 * @param operator_id   ID of the operator starting the session
 * @param product_id    ID of the product being inspected
 * 
 * @return 0 on success, negative on error (e.g., invalid operator/product).
 */
int session_start(int operator_id, int product_id);

/**
 * @brief End the current session.
 * 
 * @return 0 on success, negative on error (e.g., no active session).
 */
int session_end(void);

/**
 * @brief Check if a session is currently active.
 * 
 * @return true if session is active, false otherwise.
 */
bool session_is_active(void);

/**
 * @brief Get the current operator ID.
 * 
 * @return Operator ID (0 if no active session).
 */
int session_get_operator_id(void);

/**
 * @brief Get the current product ID.
 * 
 * @return Product ID (0 if no active session).
 */
int session_get_product_id(void);

/**
 * @brief Increment the defect count for the current session.
 * 
 * @return 0 on success, negative on error (e.g., no active session).
 */
int session_increment_defect_count(void);

/**
 * @brief Get the current defect count for the session.
 * 
 * @return Defect count (0 if no active session).
 */
int session_get_defect_count(void);

/**
 * @brief Get session information.
 * 
 * @param operator_id   Pointer to store operator ID (can be NULL)
 * @param product_id    Pointer to store product ID (can be NULL)
 * @param defect_count  Pointer to store defect count (can be NULL)
 * 
 * @return 0 on success, negative on error (e.g., no active session).
 */
int session_get_info(int *operator_id, int *product_id, int *defect_count);

#ifdef __cplusplus
}
#endif

#endif /* SESSION_H */
