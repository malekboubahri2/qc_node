#ifndef MQTT_TOPICS_H
#define MQTT_TOPICS_H

/* Single source of truth for MQTT topic strings */

#define MQTT_TOPIC_CONFIG_PRODUCTS    "qc/config/products"
#define MQTT_TOPIC_CONFIG_OPERATORS   "qc/config/operators"
#define MQTT_TOPIC_CONFIG_FLAGS       "qc/config/flags"

/* Device topics format macros - use snprintf to build full topic with device id */
#define MQTT_TOPIC_DEVICE_CMD_FMT     "qc/device/%s/cmd"
#define MQTT_TOPIC_DEVICE_STATUS_FMT  "qc/device/%s/status"
#define MQTT_TOPIC_DEVICE_SESSION_FMT "qc/device/%s/session"
#define MQTT_TOPIC_DEVICE_INSPECT_FMT "qc/device/%s/inspection"

#endif /* MQTT_TOPICS_H */
