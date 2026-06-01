#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file config_store.h
 * @brief Persistent configuration storage on Octo-SPI flash.
 *
 * Layout in the reserved top 128 KB of the memory-mapped Octo-SPI flash:
 *   0x93FE0000 - 0x93FE0FFF (4 KB):  Metadata (version, timestamps)
 *   0x93FE1000 - 0x93FE1FFF (4 KB):  Operator config JSON
 *   0x93FE2000 - 0x93FEFFFF (56 KB): Product config JSON
 *   0x93FF0000 - 0x93FF0FFF (4 KB):  Wi-Fi/MQTT credentials
 *
 * The linker reserves 0x93FE0000..0x93FFFFFF for this store so TouchGFX
 * assets cannot be linked over persistent data.
 */

/* Octo-SPI memory map and reserved config-store window. */
#define CONFIG_STORE_OSPI_MEMORY_BASE     0x90000000UL
#define CONFIG_STORE_OSPI_FLASH_SIZE      0x04000000UL
#define CONFIG_STORE_RESERVED_SIZE        0x00020000UL
#define CONFIG_STORE_BASE \
    (CONFIG_STORE_OSPI_MEMORY_BASE + CONFIG_STORE_OSPI_FLASH_SIZE - CONFIG_STORE_RESERVED_SIZE)
#define CONFIG_STORE_FLASH_OFFSET \
    (CONFIG_STORE_BASE - CONFIG_STORE_OSPI_MEMORY_BASE)

/* Offsets relative to CONFIG_STORE_BASE. */
#define CONFIG_STORE_META_OFFSET    0x00000000UL
#define CONFIG_STORE_OPERATORS_OFFSET 0x00001000UL
#define CONFIG_STORE_PRODUCTS_OFFSET  0x00002000UL
#define CONFIG_STORE_CREDENTIALS_OFFSET 0x00010000UL

#define CONFIG_STORE_META_SIZE      0x1000UL       /* 4 KB */
#define CONFIG_STORE_OPERATORS_SIZE 0x1000UL       /* 4 KB */
#define CONFIG_STORE_PRODUCTS_SIZE  0xE000UL       /* 56 KB */
#define CONFIG_STORE_CREDENTIALS_SIZE 0x1000UL     /* 4 KB */
#define CONFIG_STORE_REGION_SIZE \
    (CONFIG_STORE_CREDENTIALS_OFFSET + CONFIG_STORE_CREDENTIALS_SIZE)

#if CONFIG_STORE_REGION_SIZE > CONFIG_STORE_RESERVED_SIZE
#error "Config store layout exceeds reserved Octo-SPI flash window"
#endif

#define CONFIG_STORE_OSPI_INSTANCE  0  /* OSPI1 */

/**
 * @brief Credentials JSON stored in flash.
 * 
 * Contains Wi-Fi SSID, password, MQTT broker host, port, client ID, username, and password.
 */
typedef struct
{
    char wifi_ssid[32 + 1];         /**< Wi-Fi SSID (null-terminated) */
    char wifi_password[64 + 1];     /**< Wi-Fi password (null-terminated) */
    char mqtt_broker_host[64 + 1];  /**< MQTT broker host (null-terminated) */
    uint16_t mqtt_broker_port;      /**< MQTT broker port */
    char mqtt_client_id[32 + 1];    /**< MQTT client ID (null-terminated) */
    char mqtt_username[32 + 1];     /**< MQTT username (null-terminated, optional) */
    char mqtt_password[32 + 1];     /**< MQTT password (null-terminated, optional) */
    uint32_t crc32;                 /**< CRC32 of the credentials struct */
} config_store_credentials_t;

/**
 * @brief Metadata header stored in flash.
 *
 * Allows quick validation of stored configs without full parse.
 */
typedef struct
{
    uint16_t config_schema_version;  /* MQTT config schema version */
    uint32_t products_timestamp_ms;  /* When product config was stored */
    uint32_t operators_timestamp_ms; /* When operator config was stored */
    uint32_t credentials_timestamp_ms; /* When credentials were stored */
    uint16_t products_size;          /* Actual JSON size (excluding padding) */
    uint16_t operators_size;         /* Actual JSON size (excluding padding) */
    uint16_t credentials_size;       /* Actual credentials size (excluding padding) */
    uint32_t crc32;                  /*CRC32 of metadata itself */
} config_store_meta_t;

/**
 * @brief Initialise the config store.
 *
 * Initialises the Octo-SPI driver if not already done.
 * Reads and validates metadata from flash.
 *
 * @return 0 on success, negative on error.
 */
int config_store_init(void);

/**
 * @brief Read the metadata header.
 *
 * @param[out] meta  Pointer to metadata struct.
 *
 * @return 0 on success, negative on error.
 */
int config_store_read_meta(config_store_meta_t *meta);

/**
 * @brief Write the metadata header and persist to flash.
 *
 * @param[in] meta  Pointer to metadata struct.
 *
 * @return 0 on success, negative on error.
 */
int config_store_write_meta(const config_store_meta_t *meta);

/**
 * @brief Read operator config JSON from flash.
 *
 * @param[out] buf     Buffer to read into.
 * @param[in,out] len  In: buffer size; Out: bytes read.
 *
 * @return 0 on success, negative on error.
 */
int config_store_read_operators(uint8_t *buf, uint16_t *len);

/**
 * @brief Write operator config JSON to flash.
 *
 * Erases the 4 KB sector, then writes up to CONFIG_STORE_OPERATORS_SIZE bytes.
 *
 * @param[in] buf     JSON buffer.
 * @param[in] len     JSON size in bytes (must fit in CONFIG_STORE_OPERATORS_SIZE).
 *
 * @return 0 on success, negative on error.
 */
int config_store_write_operators(const uint8_t *buf, uint16_t len);

/**
 * @brief Read product config JSON from flash.
 *
 * @param[out] buf     Buffer to read into.
 * @param[in,out] len  In: buffer size; Out: bytes read.
 *
 * @return 0 on success, negative on error.
 */
int config_store_read_products(uint8_t *buf, uint16_t *len);

/**
 * @brief Write product config JSON to flash.
 *
 * Erases the 56 KB region, then writes up to CONFIG_STORE_PRODUCTS_SIZE bytes.
 *
 * @param[in] buf     JSON buffer.
 * @param[in] len     JSON size in bytes (must fit in CONFIG_STORE_PRODUCTS_SIZE).
 *
 * @return 0 on success, negative on error.
 */
int config_store_write_products(const uint8_t *buf, uint16_t len);

/**
 * @brief Read credentials JSON from flash.
 *
 * @param[out] buf     Buffer to read into.
 * @param[in,out] len  In: buffer size; Out: bytes read.
 *
 * @return 0 on success, negative on error.
 */
int config_store_read_credentials(uint8_t *buf, uint16_t *len);

/**
 * @brief Write credentials JSON to flash.
 *
 * Erases the 4 KB sector, then writes up to CONFIG_STORE_CREDENTIALS_SIZE bytes.
 *
 * @param[in] buf     JSON buffer.
 * @param[in] len     JSON size in bytes (must fit in CONFIG_STORE_CREDENTIALS_SIZE).
 *
 * @return 0 on success, negative on error.
 */
int config_store_write_credentials(const uint8_t *buf, uint16_t len);

/**
 * @brief Compute CRC32 using the config-store polynomial/settings.
 *
 * @param[in] buf Data buffer.
 * @param[in] len Data length in bytes.
 *
 * @return CRC32 value.
 */
uint32_t config_store_crc32_compute(const uint8_t *buf, size_t len);

/**
 * @brief Clear the credentials sector.
 *
 * @return 0 on success, negative on error.
 */
int config_store_clear_credentials(void);

/**
 * @brief Clear all stored config (set to zeros).
 *
 * @return 0 on success, negative on error.
 */
int config_store_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STORE_H */
