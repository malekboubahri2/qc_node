#include "config_loader.h"
#include "config_store.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char *config_loader_get_products(size_t *out_len)
{
    if (!out_len) return NULL;

    uint8_t buf[CONFIG_STORE_PRODUCTS_SIZE];
    uint16_t len = sizeof(buf);

    if (config_store_read_products(buf, &len) != 0) {
        printf("config_loader: failed to read products\n");
        *out_len = 0;
        return NULL;
    }

    if (len == 0) {
        printf("config_loader: no products config in flash\n");
        *out_len = 0;
        return NULL;
    }

    /* Allocate and copy */
    char *result = (char *)malloc(len + 1);
    if (!result) {
        printf("config_loader: malloc failed for products\n");
        *out_len = 0;
        return NULL;
    }

    memcpy(result, buf, len);
    result[len] = '\0';
    *out_len = len;

    printf("config_loader: loaded %zu bytes of products\n", len);
    return result;
}

char *config_loader_get_operators(size_t *out_len)
{
    if (!out_len) return NULL;

    uint8_t buf[CONFIG_STORE_OPERATORS_SIZE];
    uint16_t len = sizeof(buf);

    if (config_store_read_operators(buf, &len) != 0) {
        printf("config_loader: failed to read operators\n");
        *out_len = 0;
        return NULL;
    }

    if (len == 0) {
        printf("config_loader: no operators config in flash\n");
        *out_len = 0;
        return NULL;
    }

    /* Allocate and copy */
    char *result = (char *)malloc(len + 1);
    if (!result) {
        printf("config_loader: malloc failed for operators\n");
        *out_len = 0;
        return NULL;
    }

    memcpy(result, buf, len);
    result[len] = '\0';
    *out_len = len;

    printf("config_loader: loaded %zu bytes of operators\n", len);
    return result;
}

int config_loader_get_metadata(uint16_t *config_version,
                                uint32_t *products_timestamp_ms,
                                uint32_t *operators_timestamp_ms)
{
    if (!config_version || !products_timestamp_ms || !operators_timestamp_ms) {
        return -1;
    }

    config_store_meta_t meta;
    if (config_store_read_meta(&meta) != 0) {
        printf("config_loader: no metadata in flash\n");
        *config_version = 0;
        *products_timestamp_ms = 0;
        *operators_timestamp_ms = 0;
        return -1;
    }

    *config_version = meta.config_schema_version;
    *products_timestamp_ms = meta.products_timestamp_ms;
    *operators_timestamp_ms = meta.operators_timestamp_ms;

    printf("config_loader: metadata loaded (schema_v=%u, prod_time=%lu, ops_time=%lu)\n",
           *config_version, *products_timestamp_ms, *operators_timestamp_ms);
    return 0;
}
