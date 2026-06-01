#include "domain/defect_config.h"
#include <string.h>
#include <stdio.h>

/* Product list. */
static product_entry_t s_products[DEFECT_CONFIG_MAX_PRODUCTS];
static int s_product_count = 0;

/* Defect types indexed by [product slot][category]. The product slot is the
 * same index as s_products, NOT the product id. */
static defect_type_t s_defect_types[DEFECT_CONFIG_MAX_PRODUCTS]
                                   [DEFECT_CONFIG_MAX_CATEGORIES]
                                   [DEFECT_CONFIG_MAX_DEFECTS_PER_CATEGORY];
static int s_defect_type_counts[DEFECT_CONFIG_MAX_PRODUCTS]
                               [DEFECT_CONFIG_MAX_CATEGORIES];

static bool s_initialized = false;

static int find_product_slot(int product_id)
{
    for (int i = 0; i < s_product_count; i++) {
        if (s_products[i].id == product_id) {
            return i;
        }
    }
    return -1;
}

static void clear_all(void)
{
    memset(s_products, 0, sizeof(s_products));
    s_product_count = 0;
    memset(s_defect_types, 0, sizeof(s_defect_types));
    memset(s_defect_type_counts, 0, sizeof(s_defect_type_counts));
}

int defect_config_init(void)
{
    if (s_initialized) {
        return 0; /* idempotent — preserve data populated by boot/MQTT */
    }
    clear_all();
    s_initialized = true;
    printf("defect_config: initialized\n");
    return 0;
}

int defect_config_set_products(const product_entry_t *products, int count)
{
    if (!products && count > 0) {
        return -1;
    }
    if (count < 0) {
        return -1;
    }

    clear_all();

    int n = 0;
    for (int i = 0; i < count && n < DEFECT_CONFIG_MAX_PRODUCTS; i++) {
        s_products[n] = products[i];
        s_products[n].name[sizeof(s_products[n].name) - 1] = '\0';
        n++;
    }
    s_product_count = n;
    s_initialized = true;

    printf("defect_config: set %d products\n", s_product_count);
    return 0;
}

int defect_config_set_defect_types(int product_id, int category,
                                   const defect_type_t *list, int count)
{
    if (category < 0 || category >= DEFECT_CONFIG_MAX_CATEGORIES) {
        return -1;
    }
    if (!list && count > 0) {
        return -1;
    }

    int slot = find_product_slot(product_id);
    if (slot < 0) {
        printf("defect_config: set_defect_types for unknown product %d\n",
               product_id);
        return -1;
    }

    int n = 0;
    for (int i = 0; i < count && n < DEFECT_CONFIG_MAX_DEFECTS_PER_CATEGORY; i++) {
        s_defect_types[slot][category][n] = list[i];
        s_defect_types[slot][category][n].label
            [sizeof(s_defect_types[slot][category][n].label) - 1] = '\0';
        n++;
    }
    s_defect_type_counts[slot][category] = n;

    printf("defect_config: product %d category %d -> %d defect types\n",
           product_id, category, n);
    return 0;
}

int defect_config_get_product_count(void)
{
    return s_product_count;
}

int defect_config_get_product(int idx, product_entry_t *product)
{
    if (!product || idx < 0 || idx >= s_product_count) {
        return -1;
    }
    *product = s_products[idx];
    return 0;
}

int defect_config_get_defect_types(int product_id, int category,
                                   const defect_type_t **defect_types,
                                   int *count)
{
    if (!defect_types || !count) {
        return -1;
    }
    if (category < 0 || category >= DEFECT_CONFIG_MAX_CATEGORIES) {
        return -1;
    }

    int slot = find_product_slot(product_id);
    if (slot < 0) {
        return -1;
    }

    *defect_types = s_defect_types[slot][category];
    *count = s_defect_type_counts[slot][category];
    return 0;
}

bool defect_config_is_valid_defect_type(int product_id, int category,
                                        int defect_type_id)
{
    const defect_type_t *types;
    int count;

    if (defect_config_get_defect_types(product_id, category, &types, &count) != 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (types[i].id == defect_type_id) {
            return true;
        }
    }
    return false;
}
