#include "domain/defect_config.h"
#include <string.h>
#include <stdio.h>

// Maximum limits from app_config.h (would normally be included)
#ifndef APP_MAX_DEFECTS_PER_CATEGORY
#define APP_MAX_DEFECTS_PER_CATEGORY 12
#endif
#ifndef APP_MAX_CATEGORIES
#define APP_MAX_CATEGORIES 2
#endif
#ifndef APP_DEFECT_LABEL_MAX
#define APP_DEFECT_LABEL_MAX 24
#endif

// Maximum products (should match system limits)
#define MAX_PRODUCTS 100

// Internal storage
static product_entry_t s_products[MAX_PRODUCTS];
static int s_product_count = 0;

// Defect types organized by [product_id][category]
static defect_type_t s_defect_types[MAX_PRODUCTS][APP_MAX_CATEGORIES][APP_MAX_DEFECTS_PER_CATEGORY];
static int s_defect_type_counts[MAX_PRODUCTS][APP_MAX_CATEGORIES] = { { 0 } };

int defect_config_init(void)
{
    // Clear all stored data
    memset(s_products, 0, sizeof(s_products));
    s_product_count = 0;
    memset(s_defect_types, 0, sizeof(s_defect_types));
    memset(s_defect_type_counts, 0, sizeof(s_defect_type_counts));
    
    printf("defect_config: initialized\n");
    return 0;
}

int defect_config_set(const product_entry_t *products, int product_count,
                     const defect_type_t *defect_types, int defect_type_count)
{
    int i;
    
    // Validate inputs
    if (!products && product_count > 0) return -1;
    if (!defect_types && defect_type_count > 0) return -1;
    if (product_count > MAX_PRODUCTS) return -1;
    
    // Clear existing data
    memset(s_products, 0, sizeof(s_products));
    s_product_count = 0;
    memset(s_defect_types, 0, sizeof(s_defect_types));
    memset(s_defect_type_counts, 0, sizeof(s_defect_type_counts));
    
    // Copy products
    for (i = 0; i < product_count && i < MAX_PRODUCTS; i++)
    {
        s_products[i] = products[i];
        // Ensure null termination
        s_products[i].name[sizeof(s_products[i].name) - 1] = '\0';
    }
    s_product_count = i;
    
    // For simplicity in this implementation, we'll assume defect_types
    // are organized in the same order as products and categories
    // In a real implementation, the MQTT payload would include
    // product_id and category for each defect type
    
    // This is a simplified approach - in reality, we'd parse the JSON
    // structure to properly associate defect types with products
    
    printf("defect_config: set %d products\n", s_product_count);
    return 0;
}

int defect_config_get_product_count(void)
{
    return s_product_count;
}

int defect_config_get_product(int idx, product_entry_t *product)
{
    if (!product || idx < 0 || idx >= s_product_count)
        return -1;
    
    *product = s_products[idx];
    return 0;
}

int defect_config_get_defect_types(int product_id, int category,
                                  const defect_type_t **defect_types,
                                  int *count)
{
    int i;
    
    // Find the product
    for (i = 0; i < s_product_count; i++)
    {
        if (s_products[i].id == product_id)
        {
            // Found the product
            if (category < 0 || category >= APP_MAX_CATEGORIES)
                return -1;
                
            *defect_types = s_defect_types[i][category];
            *count = s_defect_type_counts[i][category];
            return 0;
        }
    }
    
    // Product not found
    return -1;
}

bool defect_config_is_valid_defect_type(int product_id, int category,
                                       int defect_type_id)
{
    const defect_type_t *types;
    int count, i;
    
    if (defect_config_get_defect_types(product_id, category, &types, &count) != 0)
        return false;
        
    for (i = 0; i < count; i++)
    {
        if (types[i].id == defect_type_id)
            return true;
    }
    
    return false;
}
