#ifndef MQTT_PAYLOADS_H
#define MQTT_PAYLOADS_H

#include <stdint.h>
#include <stddef.h>

/* Small JSON helpers for MQTT payloads. Minimal implementations using snprintf.
 * For parsing incoming config messages, later we'll add jsmn-based parsers.
 */

/* Serialize status heartbeat into provided buffer. Returns number of bytes written or negative on error. */
int mqtt_serialize_status(char *buf, size_t buflen,
                          const char *device_id,
                          uint32_t uptime_ms,
                          uint32_t config_version,
                          uint32_t operator_version,
                          uint32_t queue_depth,
                          int wifi_rssi,
                          uint32_t mqtt_reconnects);

/* Serialize session start message */
int mqtt_serialize_session(char *buf, size_t buflen,
                           const char *device_id,
                           uint32_t operator_id,
                           uint32_t product_id,
                           const char *started_at_iso);

/* Serialize inspection/defect message */
int mqtt_serialize_inspection(char *buf, size_t buflen,
                              const char *device_id,
                              uint32_t operator_id,
                              uint32_t product_id,
                              const char *outcome, /* "DEFECT" or "OK" */
                              uint32_t defect_type_id, /* 0 if none */
                              const char *note, /* nullable, pass NULL */
                              const char *logged_at_iso);

#endif /* MQTT_PAYLOADS_H */
