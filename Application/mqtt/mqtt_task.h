#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    const char *wifi_ssid;
    const char *wifi_password;
    const char *broker_host;
    uint16_t     broker_port;
    const char *client_id;
    const char *username; /* optional */
    const char *password; /* optional */
} MqttTaskConfig_t;

/** Initialise mqtt task with given configuration and create its FreeRTOS task. */
int mqtt_task_init(const MqttTaskConfig_t *cfg);

/* Publish an inspection payload that is already serialized.
 * Returns 0 on success, negative on error.
 */
int mqtt_task_publish_inspection(const char *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_TASK_H */
