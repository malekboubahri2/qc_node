#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file config_loader.h
 * @brief Load persisted configs from flash on startup.
 *
 * Provides utilities to read product and operator configs cached from MQTT
 * subscriptions. If available, returns the cached JSON; if not, returns NULL.
 */

/**
 * @brief Load cached product config from flash.
 *
 * Caller must free the returned buffer with free().
 *
 * @param[out] out_len  Length of returned buffer.
 *
 * @return Pointer to JSON buffer, or NULL if not available or error.
 */
char *config_loader_get_products(size_t *out_len);

/**
 * @brief Load cached operator config from flash.
 *
 * Caller must free the returned buffer with free().
 *
 * @param[out] out_len  Length of returned buffer.
 *
 * @return Pointer to JSON buffer, or NULL if not available or error.
 */
char *config_loader_get_operators(size_t *out_len);

/**
 * @brief Load config metadata (timestamps, versions, sizes).
 *
 * @param[out] config_version  Cached schema version from MQTT, or 0 if not set.
 * @param[out] products_timestamp_ms  When products config was received, or 0.
 * @param[out] operators_timestamp_ms  When operators config was received, or 0.
 *
 * @return 0 on success, negative if metadata not available.
 */
int config_loader_get_metadata(uint16_t *config_version,
                                uint32_t *products_timestamp_ms,
                                uint32_t *operators_timestamp_ms);

#endif /* CONFIG_LOADER_H */
