#ifndef DEFECT_CONFIG_H
#define DEFECT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file defect_config.h
 * @brief Defect type configuration management.
 * 
 * Manages product-specific defect types and their labels.
 * Config data is received via MQTT on qc/config/products topic.
 */

/**
 * @brief Defect type definition.
 */
typedef struct
{
    int  id;                    /**< Defect type ID */
    char label[24 + 1];         /**< Defect type label (null-terminated) */
} defect_type_t;

/**
 * @brief Product definition with associated defect types.
 */
typedef struct
{
    int  id;                    /**< Product ID */
    char name[32 + 1];          /**< Product name (null-terminated) */
    // Note: defect types are stored separately and looked up by product_id
} product_entry_t;

/**
 * @brief Initialize the defect config module.
 * 
 * @return 0 on success, negative on error.
 */
int defect_config_init(void);

/**
 * @brief Set the complete product and defect type configuration.
 * 
 * This function is called when a new products config is received via MQTT.
 * 
 * @param products    Array of product entries
 * @param product_count Number of products in the array
 * @param defect_types Array of defect type entries (organized by product and category)
 * @param defect_type_count Total number of defect type entries
 * 
 * @return 0 on success, negative on error.
 */
int defect_config_set(const product_entry_t *products, int product_count,
                     const defect_type_t *defect_types, int defect_type_count);

/**
 * @brief Get the number of configured products.
 * 
 * @return Number of products (0 if none configured).
 */
int defect_config_get_product_count(void);

/**
 * @brief Get a product by index.
 * 
 * @param idx     Product index (0-based)
 * @param product Pointer to store the product data
 * 
 * @return 0 on success, negative on error (e.g., index out of bounds).
 */
int defect_config_get_product(int idx, product_entry_t *product);

/**
 * @brief Get the defect types for a specific product and category.
 * 
 * @param product_id   Product ID to get defect types for
 * @param category     Category (0 = PMP, 1 = INJ)
 * @param defect_types Pointer to store the defect type array (output)
 * @param count        Pointer to store the number of defect types (output)
 * 
 * @return 0 on success, negative on error (e.g., product not found).
 */
int defect_config_get_defect_types(int product_id, int category,
                                  const defect_type_t **defect_types,
                                  int *count);

/**
 * @brief Validate if a defect type ID is valid for a given product and category.
 * 
 * @param product_id   Product ID
 * @param category     Category (0 = PMP, 1 = INJ)
 * @param defect_type_id Defect type ID to validate
 * 
 * @return true if valid, false otherwise.
 */
bool defect_config_is_valid_defect_type(int product_id, int category,
                                       int defect_type_id);

#ifdef __cplusplus
}
#endif

#endif /* DEFECT_CONFIG_H */
