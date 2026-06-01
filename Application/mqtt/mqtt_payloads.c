#include "mqtt_payloads.h"
#include <stdio.h>
#include <string.h>

int mqtt_serialize_status(char *buf, size_t buflen,
                          const char *device_id,
                          uint32_t uptime_ms,
                          uint32_t config_version,
                          uint32_t operator_version,
                          uint32_t queue_depth,
                          int wifi_rssi,
                          uint32_t mqtt_reconnects)
{
    if (!buf || !device_id) return -1;
    int n = snprintf(buf, buflen,
        "{\"schema_version\":1,\"device_id\":\"%s\",\"uptime_ms\":%lu,\"config_version\":%lu,\"operator_version\":%lu,\"queue_depth\":%lu,\"wifi_rssi\":%d,\"mqtt_reconnects\":%lu}",
        device_id, (unsigned long)uptime_ms, (unsigned long)config_version,
        (unsigned long)operator_version, (unsigned long)queue_depth,
        wifi_rssi, (unsigned long)mqtt_reconnects);
    return (n < 0 || (size_t)n >= buflen) ? -1 : n;
}

int mqtt_serialize_session(char *buf, size_t buflen,
                           const char *device_id,
                           uint32_t operator_id,
                           uint32_t product_id,
                           const char *started_at_iso)
{
    if (!buf || !device_id || !started_at_iso) return -1;
    int n = snprintf(buf, buflen,
        "{\"schema_version\":1,\"device_id\":\"%s\",\"operator_id\":%lu,\"product_id\":%lu,\"started_at\":\"%s\"}",
        device_id, (unsigned long)operator_id, (unsigned long)product_id,
        started_at_iso);
    return (n < 0 || (size_t)n >= buflen) ? -1 : n;
}

int mqtt_serialize_inspection(char *buf, size_t buflen,
                              const char *device_id,
                              uint32_t operator_id,
                              uint32_t product_id,
                              const char *outcome,
                              uint32_t defect_type_id,
                              const char *note,
                              const char *logged_at_iso)
{
    if (!buf || !device_id || !outcome || !logged_at_iso) return -1;

    if (note == NULL)
    {
        int n = snprintf(buf, buflen,
            "{\"schema_version\":3,\"device_id\":\"%s\",\"operator_id\":%lu,\"product_id\":%lu,\"outcome\":\"%s\",\"defect_type_id\":%lu,\"note\":null,\"logged_at\":\"%s\"}",
            device_id, (unsigned long)operator_id, (unsigned long)product_id,
            outcome, (unsigned long)defect_type_id, logged_at_iso);
        return (n < 0 || (size_t)n >= buflen) ? -1 : n;
    }
    else
    {
        int n = snprintf(buf, buflen,
            "{\"schema_version\":3,\"device_id\":\"%s\",\"operator_id\":%lu,\"product_id\":%lu,\"outcome\":\"%s\",\"defect_type_id\":%lu,\"note\":\"%s\",\"logged_at\":\"%s\"}",
            device_id, (unsigned long)operator_id, (unsigned long)product_id,
            outcome, (unsigned long)defect_type_id, note, logged_at_iso);
        return (n < 0 || (size_t)n >= buflen) ? -1 : n;
    }
}
