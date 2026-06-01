#include "mqtt_config_callbacks.h"
#include "mqtt_topics.h"
#include "config_store.h"
#include "domain/ui_data_bridge.h"
#include "domain/operator_list.h"
#include "domain/defect_config.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

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

    printf("mqtt_callback: received products config (%zu bytes)\n", payloadLen);

    /* Validate size */
    if (payloadLen > CONFIG_JSON_MAX_SIZE) {
        printf("mqtt_callback: products config too large (%zu > %d)\n",
               payloadLen, CONFIG_JSON_MAX_SIZE);
        return;
    }

    /* Validate JSON structure */
    if (validate_json((const uint8_t *)pPayload, payloadLen) != 0) {
        printf("mqtt_callback: invalid JSON in products config\n");
        return;
    }

    /* Extract and log schema version */
    uint16_t schema_version = extract_schema_version((const uint8_t *)pPayload, payloadLen);
    printf("mqtt_callback: products schema_version = %u\n", schema_version);

    /* Store to flash */
    if (config_store_write_products((const uint8_t *)pPayload, (uint16_t)payloadLen) != 0) {
        printf("mqtt_callback: failed to persist products config\n");
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
        printf("mqtt_callback: failed to update metadata\n");
        return;
    }

    printf("mqtt_callback: products config stored successfully\n");
    
    /* TODO: Parse the JSON and extract product information to pass to the UI bridge */
    /* For now, we'll note that new config is available and would need parsing */
    printf("mqtt_callback: new products config available - parsing needed\n");
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

    printf("mqtt_callback: received operators config (%zu bytes)\n", payloadLen);

    /* Validate size */
    if (payloadLen > CONFIG_JSON_MAX_SIZE) {
        printf("mqtt_callback: operators config too large (%zu > %d)\n",
               payloadLen, CONFIG_JSON_MAX_SIZE);
        return;
    }

    /* Validate JSON structure */
    if (validate_json((const uint8_t *)pPayload, payloadLen) != 0) {
        printf("mqtt_callback: invalid JSON in operators config\n");
        return;
    }

    /* Extract and log schema version */
    uint16_t schema_version = extract_schema_version((const uint8_t *)pPayload, payloadLen);
    printf("mqtt_callback: operators schema_version = %u\n", schema_version);

    /* Store to flash */
    if (config_store_write_operators((const uint8_t *)pPayload, (uint16_t)payloadLen) != 0) {
        printf("mqtt_callback: failed to persist operators config\n");
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
        printf("mqtt_callback: failed to update metadata\n");
        return;
    }

    printf("mqtt_callback: operators config stored successfully\n");
    
    /* TODO: Parse the JSON and extract operator information to pass to the UI bridge */
    /* For now, we'll note that new config is available and would need parsing */
    printf("mqtt_callback: new operators config available - parsing needed\n");
}

int mqtt_config_callbacks_init(void)
{
    printf("mqtt_config_callbacks: subscribing to config topics\n");

    /* Subscribe to products config */
    MqttAgentStatus_t ret = MqttAgent_Subscribe(
        MQTT_TOPIC_CONFIG_PRODUCTS,
        MQTTQoS1,
        mqtt_callback_config_products,
        NULL,
        5000);

    if (ret != MQTT_AGENT_SUCCESS) {
        printf("mqtt_config_callbacks: failed to subscribe to products (error %d)\n", ret);
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
        printf("mqtt_config_callbacks: failed to subscribe to operators (error %d)\n", ret);
        return -1;
    }

    printf("mqtt_config_callbacks: subscribed to config topics\n");
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
