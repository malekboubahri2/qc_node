#ifndef DEFECT_CONFIG_H
#define DEFECT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file defect_config.h
 * @brief Product-scoped defect type configuration (ADR-013).
 *
 * Holds the product list and, per (product, category), the list of defect
 * types. Populated from the qc/config/products MQTT payload (parsed by
 * config_parser) or from the Octo-SPI cache at boot.
 *
 * Categories are the fixed plant-wide enum: 0 = PMP, 1 = INJECTION.
 */

/* Category indices — must match the server's PMP/INJECTION ordering. */
#define DEFECT_CONFIG_CATEGORY_PMP            0
#define DEFECT_CONFIG_CATEGORY_INJ            1
#define DEFECT_CONFIG_MAX_CATEGORIES          2

/* 12 user-defined + 1 auto "Autre — préciser" fallback per (product,category). */
#define DEFECT_CONFIG_MAX_DEFECTS_PER_CATEGORY 13
#define DEFECT_CONFIG_MAX_PRODUCTS            16

/** A single defect type within a product/category. */
typedef struct
{
    int  id;                    /**< Server-side defect_type primary key. */
    char label[24 + 1];         /**< UTF-8 label, null-terminated. */
    bool is_other;              /**< True for the "Autre — préciser" fallback. */
} defect_type_t;

/** A product the operator can inspect. */
typedef struct
{
    int  id;                    /**< Server-side product primary key. */
    char name[32 + 1];          /**< Product name, null-terminated. */
} product_entry_t;

/**
 * @brief Initialise the module. Idempotent: clears storage only on the first
 *        call so boot-time parsing and the Model constructor cannot wipe each
 *        other regardless of task start order.
 */
int defect_config_init(void);

/**
 * @brief Replace the product list. Clears all previously stored defect types
 *        (they are re-supplied per product via defect_config_set_defect_types).
 *
 * @return 0 on success, negative on error.
 */
int defect_config_set_products(const product_entry_t *products, int count);

/**
 * @brief Set the defect types for one (product, category) pair.
 *
 * @param product_id  Product the types belong to (must already be in the list).
 * @param category    DEFECT_CONFIG_CATEGORY_PMP or _INJ.
 * @param list        Defect types (capped at DEFECT_CONFIG_MAX_DEFECTS_PER_CATEGORY).
 * @param count       Number of entries in @p list.
 *
 * @return 0 on success, negative on error (unknown product, bad category).
 */
int defect_config_set_defect_types(int product_id, int category,
                                   const defect_type_t *list, int count);

/** @return Number of configured products (0 if none). */
int defect_config_get_product_count(void);

/** @return 0 and fills @p product on success, negative if @p idx out of range. */
int defect_config_get_product(int idx, product_entry_t *product);

/**
 * @brief Get the defect types for a product/category.
 *
 * @param[out] defect_types  Receives a pointer to the internal array.
 * @param[out] count         Receives the entry count.
 * @return 0 on success, negative if the product is unknown.
 */
int defect_config_get_defect_types(int product_id, int category,
                                   const defect_type_t **defect_types,
                                   int *count);

/** @return true if @p defect_type_id exists for the product/category. */
bool defect_config_is_valid_defect_type(int product_id, int category,
                                        int defect_type_id);

#ifdef __cplusplus
}
#endif

#endif /* DEFECT_CONFIG_H */
