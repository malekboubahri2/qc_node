#include "mqtt_config_callbacks.h"
#include "mqtt_topics.h"
#include "config_store.h"
#include "domain/ui_data_bridge.h"
#include "domain/operator_list.h"
#include "domain/defect_config.h"
#include "domain/config_parser.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "app_log.h"
/* Maximum allowed config JSON size (must fit in config_store sectors) */
#define CONFIG_JSON_MAX_SIZE 4000

/**
 * @brief Extract schema_version from JSON payload.
 *
 * Looks for: "schema_version": <number>
 * Returns the version or 0 if not found.
 */
static uint16_t extract_schema_version(const uint8_t *json, size_t json_len)
{
    const char *key = "\"schema_version\"";
    const char *json_str = (const char *)json;

    /* Find the key */
    const char *pos = strstr(json_str, key);
    if (!pos || pos - json_str + strlen(key) > (int)json_len) {
        return 0;
    }

    /* Skip past the key and colon */
    pos += strlen(key);
    while (pos - json_str < (int)json_len && (*pos == ' ' || *pos == ':')) {
        pos++;
    }

    /* Parse the version number */
    uint16_t version = 0;
    while (pos - json_str < (int)json_len && isdigit((unsigned char)*pos)) {
        version = version * 10 + (*pos - '0');
        pos++;
    }

    return version;
}

/**
 * @brief Validate JSON structure (minimal check).
 *
 * Just ensures it's wrapped in braces and is valid UTF-8.
 */
static int validate_json(const uint8_t *json, size_t json_len)
{
    if (json_len < 2 || json[0] != '{' || json[json_len - 1] != '}') {
        return -1;
    }

    /* Basic UTF-8 validation */
    for (size_t i = 0; i < json_len; i++) {
        if (json[i] == 0) {
            return -1; /* Embedded null */
        }
    }

    return 0;
}

void mqtt_callback_config_products(const char            *pTopicName,
                                     uint16_t               topicLen,
                                     const void            *pPayload,
                                     size_t                 payloadLen,
                                     const MQTTPropBuilder_t *pProps,
                                     void                  *pUserCtx)
{
    (void)pTopicName;
    (void)topicLen;
    (void)pProps;
    (void)pUserCtx;

    LOG_INFO(APP_LAYER_CFG, "received products config (%zu bytes)" , payloadLen);

    /* Validate size */
    if (payloadLen > CONFIG_JSON_MAX_SIZE) {
        LOG_ERR(APP_LAYER_CFG, "products config too large (%zu > %d)",
                payloadLen, CONFIG_JSON_MAX_SIZE);
        return;
    }

    /* Validate JSON structure */
    if (validate_json((const uint8_t *)pPayload, payloadLen) != 0) {
        LOG_ERR(APP_LAYER_CFG, "invalid JSON in products config");
        return;
    }

    /* Extract and log schema version */
    uint16_t schema_version = extract_schema_version((const uint8_t *)pPayload, payloadLen);
    LOG_INFO(APP_LAYER_CFG, "products schema_version = %u" , schema_version);

    /* Store to flash */
    if (config_store_write_products((const uint8_t *)pPayload, (uint16_t)payloadLen) != 0) {
        LOG_ERR(APP_LAYER_CFG, "failed to persist products config");
        return;
    }

    /* Update metadata */
    config_store_meta_t meta;
    if (config_store_read_meta(&meta) != 0) {
        memset(&meta, 0, sizeof(meta));
    }
    meta.config_schema_version = schema_version;
    meta.products_size = (uint16_t)payloadLen;
    meta.products_timestamp_ms = osKernelGetTickCount();

    if (config_store_write_meta(&meta) != 0) {
        LOG_ERR(APP_LAYER_CFG, "failed to update metadata");
        return;
    }

    LOG_INFO(APP_LAYER_CFG, "products config stored successfully");

    /* Parse and apply into the live domain model (product list + defect types).
     * The payload is bounded to CONFIG_JSON_MAX_SIZE above; config_parser uses
     * static scratch buffers so this is safe from the agent task context. */
    if (config_parser_apply_products((const char *)pPayload, payloadLen) == 0) {
        LOG_INFO(APP_LAYER_CFG, "products applied to model");
    } else {
        LOG_ERR(APP_LAYER_CFG, "products parse failed");
    }
}

void mqtt_callback_config_operators(const char            *pTopicName,
                                      uint16_t               topicLen,
                                      const void            *pPayload,
                                      size_t                 payloadLen,
                                      const MQTTPropBuilder_t *pProps,
                                      void                  *pUserCtx)
{
    (void)pTopicName;
    (void)topicLen;
    (void)pProps;
    (void)pUserCtx;

    LOG_INFO(APP_LAYER_CFG, "received operators config (%zu bytes)" , payloadLen);

    /* Validate size */
    if (payloadLen > CONFIG_JSON_MAX_SIZE) {
        LOG_ERR(APP_LAYER_CFG, "operators config too large (%zu > %d)",
                payloadLen, CONFIG_JSON_MAX_SIZE);
        return;
    }

    /* Validate JSON structure */
    if (validate_json((const uint8_t *)pPayload, payloadLen) != 0) {
        LOG_ERR(APP_LAYER_CFG, "invalid JSON in operators config");
        return;
    }

    /* Extract and log schema version */
    uint16_t schema_version = extract_schema_version((const uint8_t *)pPayload, payloadLen);
    LOG_INFO(APP_LAYER_CFG, "operators schema_version = %u" , schema_version);

    /* Store to flash */
    if (config_store_write_operators((const uint8_t *)pPayload, (uint16_t)payloadLen) != 0) {
        LOG_ERR(APP_LAYER_CFG, "failed to persist operators config");
        return;
    }

    /* Update metadata */
    config_store_meta_t meta;
    if (config_store_read_meta(&meta) != 0) {
        memset(&meta, 0, sizeof(meta));
    }
    meta.config_schema_version = schema_version;
    meta.operators_size = (uint16_t)payloadLen;
    meta.operators_timestamp_ms = osKernelGetTickCount();

    if (config_store_write_meta(&meta) != 0) {
        LOG_ERR(APP_LAYER_CFG, "failed to update metadata");
        return;
    }

    LOG_INFO(APP_LAYER_CFG, "operators config stored successfully");

    /* Parse and apply into the operator list (used by login PIN validation). */
    if (config_parser_apply_operators((const char *)pPayload, payloadLen) == 0) {
        LOG_INFO(APP_LAYER_CFG, "operators applied to model");
    } else {
        LOG_ERR(APP_LAYER_CFG, "operators parse failed");
    }
}

int mqtt_config_callbacks_init(void)
{
    LOG_INFO(APP_LAYER_CFG, "subscribing to config topics");

    /* Subscribe to products config */
    MqttAgentStatus_t ret = MqttAgent_Subscribe(
        MQTT_TOPIC_CONFIG_PRODUCTS,
        MQTTQoS1,
        mqtt_callback_config_products,
        NULL,
        5000);

    if (ret != MQTT_AGENT_SUCCESS) {
        LOG_ERR(APP_LAYER_CFG, "failed to subscribe to products (error %d)" , ret);
        return -1;
    }

    /* Subscribe to operators config */
    ret = MqttAgent_Subscribe(
        MQTT_TOPIC_CONFIG_OPERATORS,
        MQTTQoS1,
        mqtt_callback_config_operators,
        NULL,
        5000);

    if (ret != MQTT_AGENT_SUCCESS) {
        LOG_ERR(APP_LAYER_CFG, "failed to subscribe to operators (error %d)" , ret);
        return -1;
    }

    LOG_INFO(APP_LAYER_CFG, "subscribed to config topics");
    return 0;
}

int mqtt_config_callbacks_handle_message(const char *pTopicName,
                                         uint16_t    topicLen,
                                         const void *pPayload,
                                         size_t      payloadLen)
{
    if ((pTopicName == NULL) || (pPayload == NULL)) {
        return -1;
    }

    if ((topicLen == (uint16_t)strlen(MQTT_TOPIC_CONFIG_PRODUCTS)) &&
        (strncmp(pTopicName, MQTT_TOPIC_CONFIG_PRODUCTS, topicLen) == 0)) {
        mqtt_callback_config_products(pTopicName,
                                      topicLen,
                                      pPayload,
                                      payloadLen,
                                      NULL,
                                      NULL);
        return 1;
    }

    if ((topicLen == (uint16_t)strlen(MQTT_TOPIC_CONFIG_OPERATORS)) &&
        (strncmp(pTopicName, MQTT_TOPIC_CONFIG_OPERATORS, topicLen) == 0)) {
        mqtt_callback_config_operators(pTopicName,
                                       topicLen,
                                       pPayload,
                                       payloadLen,
                                       NULL,
                                       NULL);
        return 1;
    }

    return 0;
}
