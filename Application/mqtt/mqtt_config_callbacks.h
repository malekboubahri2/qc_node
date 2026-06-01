#ifndef MQTT_CONFIG_CALLBACKS_H
#define MQTT_CONFIG_CALLBACKS_H

#include "mqtt_agent.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file mqtt_config_callbacks.h
 * @brief Subscription callbacks for MQTT config topics.
 *
 * Subscribes to:
 *   - qc/config/products    → product definitions with defect types
 *   - qc/config/operators   → operator PIN list
 *
 * Received JSON is validated, parsed, and persisted to Octo-SPI flash.
 */

/**
 * @brief Callback fired when qc/config/products message arrives.
 *
 * Validates schema_version, stores JSON to flash, and updates the in-memory
 * product cache.
 *
 * Runs in MQTT agent task context—keep it short.
 */
void mqtt_callback_config_products(const char            *pTopicName,
                                    uint16_t               topicLen,
                                    const void            *pPayload,
                                    size_t                 payloadLen,
                                    const MQTTPropBuilder_t *pProps,
                                    void                  *pUserCtx);

/**
 * @brief Callback fired when qc/config/operators message arrives.
 *
 * Validates schema_version, stores JSON to flash, and updates the in-memory
 * operator PIN cache.
 *
 * Runs in MQTT agent task context—keep it short.
 */
void mqtt_callback_config_operators(const char            *pTopicName,
                                     uint16_t               topicLen,
                                     const void            *pPayload,
                                     size_t                 payloadLen,
                                     const MQTTPropBuilder_t *pProps,
                                     void                  *pUserCtx);

/**
 * @brief Initialise config callbacks and subscribe to topics.
 *
 * Should be called after mqtt_task_init() and after the agent is connected.
 * Typically called from mqtt_task() after MQTT_AGENT_STATE_CONNECTED.
 *
 * @return 0 on success, negative on error.
 */
int mqtt_config_callbacks_init(void);

/**
 * @brief Dispatch an already-decoded MQTT message to the config handlers.
 *
 * Returns 1 when the topic was handled, 0 when ignored, negative on invalid
 * input.
 */
int mqtt_config_callbacks_handle_message(const char *pTopicName,
                                         uint16_t    topicLen,
                                         const void *pPayload,
                                         size_t      payloadLen);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_CONFIG_CALLBACKS_H */
