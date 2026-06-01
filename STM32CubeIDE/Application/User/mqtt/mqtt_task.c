#include "mqtt_task.h"
#include "esp01_transport.h"
#include "mqtt_agent.h"
#include "mqtt_topics.h"
#include "mqtt/mqtt_config_callbacks.h"
#include "net/inspection_queue.h"
#include "net/persistent_inspection_queue.h"
#include "persistence/config_store.h"

#include "cmsis_os2.h"

#include <stdio.h>
#include <string.h>

#include "app_log.h"
#if defined(__GNUC__)
#define MQTT_TASK_MAYBE_UNUSED __attribute__((unused))
#else
#define MQTT_TASK_MAYBE_UNUSED
#endif

#define MQTT_TASK_NAME                "mqtt_task"
#define MQTT_TASK_STACK_SZ            4096U
#define MQTT_TASK_PRIORITY            osPriorityNormal
#define MQTT_TASK_WIFI_RETRY_MS       5000U
#define MQTT_TASK_TCP_RETRY_MS        5000U
#define MQTT_TASK_MQTT_RETRY_MS       5000U
#define MQTT_TASK_AGENT_STOP_MS       2000U
#define MQTT_TASK_CONNECTED_WAIT_MS   5000U
#define MQTT_TASK_PUBLISH_WAIT_MS     1000U

static MqttTaskConfig_t s_cfg;
static osThreadId_t     s_threadId = NULL;
static NetworkContext_t s_netCtx;
static TransportInterface_t s_transport;
static bool s_mqttConnected = false;

static config_store_credentials_t s_loadedCreds;

typedef enum
{
    MQTT_TASK_STATE_WIFI_CHECK = 0,
    MQTT_TASK_STATE_WIFI_CONNECT,
    MQTT_TASK_STATE_TCP_CONNECT,
    MQTT_TASK_STATE_MQTT_START,
    MQTT_TASK_STATE_MQTT_RUN,
} MqttTaskState_t;

static const char *mqtt_task_state_name(MqttTaskState_t state)
{
    switch (state) {
        case MQTT_TASK_STATE_WIFI_CHECK:   return "wifi_check";
        case MQTT_TASK_STATE_WIFI_CONNECT: return "wifi_connect";
        case MQTT_TASK_STATE_TCP_CONNECT:  return "tcp_connect";
        case MQTT_TASK_STATE_MQTT_START:   return "mqtt_start";
        case MQTT_TASK_STATE_MQTT_RUN:     return "mqtt_run";
        default:                           return "unknown";
    }
}

static bool mqtt_task_config_is_valid(void)
{
    return (s_cfg.wifi_ssid != NULL) &&
           (s_cfg.wifi_password != NULL) &&
           (s_cfg.broker_host != NULL) &&
           (s_cfg.broker_port != 0U) &&
           (s_cfg.client_id != NULL);
}

static void load_credentials_from_storage(MqttTaskConfig_t *cfg)
{
    if (cfg == NULL) {
        return;
    }

    uint8_t buf[sizeof(config_store_credentials_t)];
    uint16_t len = sizeof(buf);

    if (config_store_read_credentials(buf, &len) != 0) {
        LOG_INFO(APP_LAYER_MQTT, "no credentials found in storage");
        return;
    }

    if (len != sizeof(config_store_credentials_t)) {
        LOG_ERR(APP_LAYER_MQTT, "invalid credentials size (%u)" , (unsigned)len);
        return;
    }

    config_store_credentials_t *creds = (config_store_credentials_t *)buf;
    uint32_t stored_crc = creds->crc32;

    if (stored_crc == 0xFFFFFFFFUL) {
        LOG_INFO(APP_LAYER_MQTT, "no credentials found in storage");
        return;
    }

    creds->crc32 = 0;
    uint32_t computed_crc = config_store_crc32_compute(
        (const uint8_t *)creds,
        sizeof(config_store_credentials_t) - sizeof(creds->crc32));
    creds->crc32 = stored_crc;

    if (stored_crc != computed_crc) {
        LOG_ERR(APP_LAYER_MQTT, "CRC mismatch in credentials (stored=0x%08lx, computed=0x%08lx)",
                stored_crc, computed_crc);
        return;
    }

    memcpy(&s_loadedCreds, creds, sizeof(s_loadedCreds));

    cfg->wifi_ssid = s_loadedCreds.wifi_ssid;
    cfg->wifi_password = s_loadedCreds.wifi_password;
    cfg->broker_host = s_loadedCreds.mqtt_broker_host;
    cfg->broker_port = s_loadedCreds.mqtt_broker_port;
    cfg->client_id = s_loadedCreds.mqtt_client_id;
    cfg->username = (s_loadedCreds.mqtt_username[0] != '\0') ? s_loadedCreds.mqtt_username : NULL;
    cfg->password = (s_loadedCreds.mqtt_password[0] != '\0') ? s_loadedCreds.mqtt_password : NULL;

    LOG_INFO(APP_LAYER_MQTT, "loaded credentials from storage");
}

static int MQTT_TASK_MAYBE_UNUSED save_credentials_to_storage(const MqttTaskConfig_t *cfg)
{
    if (cfg == NULL) {
        return -1;
    }

    config_store_credentials_t creds;
    memset(&creds, 0, sizeof(creds));

    strncpy(creds.wifi_ssid, cfg->wifi_ssid ? cfg->wifi_ssid : "", sizeof(creds.wifi_ssid) - 1U);
    strncpy(creds.wifi_password, cfg->wifi_password ? cfg->wifi_password : "", sizeof(creds.wifi_password) - 1U);
    strncpy(creds.mqtt_broker_host, cfg->broker_host ? cfg->broker_host : "", sizeof(creds.mqtt_broker_host) - 1U);
    creds.mqtt_broker_port = cfg->broker_port;
    strncpy(creds.mqtt_client_id, cfg->client_id ? cfg->client_id : "", sizeof(creds.mqtt_client_id) - 1U);
    strncpy(creds.mqtt_username, cfg->username ? cfg->username : "", sizeof(creds.mqtt_username) - 1U);
    strncpy(creds.mqtt_password, cfg->password ? cfg->password : "", sizeof(creds.mqtt_password) - 1U);

    creds.crc32 = config_store_crc32_compute(
        (const uint8_t *)&creds,
        sizeof(config_store_credentials_t) - sizeof(creds.crc32));

    if (config_store_write_credentials((const uint8_t *)&creds, sizeof(creds)) != 0) {
        LOG_ERR(APP_LAYER_MQTT, "failed to save credentials to storage");
        return -1;
    }

    LOG_INFO(APP_LAYER_MQTT, "saved credentials to storage");
    return 0;
}

static int MQTT_TASK_MAYBE_UNUSED clear_credentials_from_storage(void)
{
    if (config_store_clear_credentials() != 0) {
        LOG_ERR(APP_LAYER_MQTT, "failed to clear credentials from storage");
        return -1;
    }

    LOG_INFO(APP_LAYER_MQTT, "cleared credentials from storage");
    return 0;
}

static int mqtt_task_publish_to_topic(const char *topic,
                                      const char *payload,
                                      size_t      payload_len)
{
    if ((topic == NULL) || (payload == NULL) || (payload_len == 0U)) {
        return -1;
    }

    if (MqttAgent_GetState() != MQTT_AGENT_STATE_CONNECTED) {
        return -1;
    }

    MqttAgentStatus_t ret = MqttAgent_Publish(topic,
                                              (uint16_t)strlen(topic),
                                              payload,
                                              payload_len,
                                              MQTTQoS1,
                                              false,
                                              NULL,
                                              MQTT_TASK_PUBLISH_WAIT_MS);
    return (ret == MQTT_AGENT_SUCCESS) ? 0 : -1;
}

static void mqtt_task_store_inspection(const inspection_msg_t *msg)
{
    if (msg == NULL) {
        return;
    }

    persistent_inspection_msg_t pmsg;
    memset(&pmsg, 0, sizeof(pmsg));

    pmsg.schema_version = msg->schema_version;
    strncpy(pmsg.outcome, msg->outcome, sizeof(pmsg.outcome) - 1U);
    pmsg.product_id = msg->product_id;
    pmsg.operator_id = msg->operator_id;
    pmsg.defect_type_id = msg->defect_type_id;
    strncpy(pmsg.note, msg->note, sizeof(pmsg.note) - 1U);
    pmsg.timestamp = osKernelGetTickCount();

    if (persistent_inspection_queue_store(&pmsg) == 0) {
        LOG_INFO(APP_LAYER_MQTT, "stored inspection for later transmission");
    }
}

static void mqtt_task_flush_persistent_inspections(const char *topic)
{
    char json_payload[256];

    while (persistent_inspection_queue_has_pending()) {
        persistent_inspection_msg_t pmsg;

        if (persistent_inspection_queue_retrieve(&pmsg) != 0) {
            LOG_ERR(APP_LAYER_MQTT, "failed to retrieve stored inspection");
            break;
        }

        int len = snprintf(json_payload,
                           sizeof(json_payload),
                           "{\"schema_version\":%d,\"outcome\":\"%s\",\"product_id\":%d,\"operator_id\":%d,\"defect_type_id\":%d,\"note\":\"%s\"}",
                           pmsg.schema_version,
                           pmsg.outcome,
                           pmsg.product_id,
                           pmsg.operator_id,
                           pmsg.defect_type_id,
                           pmsg.note);

        if ((len <= 0) || (len >= (int)sizeof(json_payload))) {
            LOG_ERR(APP_LAYER_MQTT, "failed to create stored inspection JSON payload");
            break;
        }

        if (mqtt_task_publish_to_topic(topic, json_payload, (size_t)len) != 0) {
            LOG_ERR(APP_LAYER_MQTT, "failed to publish stored inspection");
            (void)persistent_inspection_queue_store(&pmsg);
            break;
        }

        LOG_INFO(APP_LAYER_MQTT, "stored inspection queued successfully");
    }
}

static void mqtt_task_service_inspection_queue(void)
{
    inspection_msg_t msg;

    if (!s_mqttConnected || (s_cfg.client_id == NULL)) {
        return;
    }

    char topic[96];
    int tn = snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_INSPECT_FMT, s_cfg.client_id);
    if ((tn <= 0) || (tn >= (int)sizeof(topic))) {
        LOG_ERR(APP_LAYER_MQTT, "failed to generate inspection topic");
        return;
    }

    mqtt_task_flush_persistent_inspections(topic);

    while (inspection_queue_receive(&msg, 0U) == 0) {
        LOG_INFO(APP_LAYER_MQTT, "sending inspection (product=%d, outcome=%s)",
                 msg.product_id, msg.outcome);

        char json_payload[256];
        int len = snprintf(json_payload,
                           sizeof(json_payload),
                           "{\"schema_version\":%d,\"outcome\":\"%s\",\"product_id\":%d,\"operator_id\":%d,\"defect_type_id\":%d,\"note\":\"%s\"}",
                           msg.schema_version,
                           msg.outcome,
                           msg.product_id,
                           msg.operator_id,
                           msg.defect_type_id,
                           msg.note);

        if ((len <= 0) || (len >= (int)sizeof(json_payload))) {
            LOG_ERR(APP_LAYER_MQTT, "failed to create inspection JSON payload");
            continue;
        }

        if (mqtt_task_publish_to_topic(topic, json_payload, (size_t)len) != 0) {
            LOG_ERR(APP_LAYER_MQTT, "failed to publish inspection; storing for retry");
            mqtt_task_store_inspection(&msg);
            break;
        }

        LOG_INFO(APP_LAYER_MQTT, "inspection queued successfully");
    }
}

static void mqtt_task_stop_agent(bool *pAgentStarted)
{
    if ((pAgentStarted == NULL) || !(*pAgentStarted)) {
        return;
    }

    MqttAgentStatus_t ret = MqttAgent_Stop(MQTT_TASK_AGENT_STOP_MS);
    if (ret != MQTT_AGENT_SUCCESS) {
        LOG_INFO(APP_LAYER_MQTT, "MqttAgent_Stop returned %d" , (int)ret);
    }

    *pAgentStarted = false;
    s_mqttConnected = false;
}

static void mqtt_task(void *pv)
{
    (void)pv;
    LOG_INFO(APP_LAYER_MQTT, "started");

    if (config_store_init() != 0) {
        LOG_ERR(APP_LAYER_MQTT, "config_store_init failed; using provided config only");
    } else {
        load_credentials_from_storage(&s_cfg);
    }

    if (!mqtt_task_config_is_valid()) {
        LOG_ERR(APP_LAYER_MQTT, "missing Wi-Fi or MQTT configuration");
        osThreadExit();
        return;
    }

    if (ESP01_Init(&s_netCtx, &huart2) != ESP01_SUCCESS) {
        LOG_ERR(APP_LAYER_MQTT, "ESP01_Init failed");
        osThreadExit();
        return;
    }

    if (persistent_inspection_queue_init() != 0) {
        LOG_ERR(APP_LAYER_MQTT, "failed to initialize persistent inspection queue");
    }

    if (inspection_queue_init() != 0) {
        LOG_ERR(APP_LAYER_MQTT, "failed to initialize inspection queue");
    }

    MqttTaskState_t taskState = MQTT_TASK_STATE_WIFI_CHECK;
    bool wifiConnected = false;
    bool tcpConnected = false;
    bool agentStarted = false;
    bool configCallbacksReady = false;

    for (;;) {
        LOG_DBG(APP_LAYER_MQTT, "state=%s" , mqtt_task_state_name(taskState));

        switch (taskState) {
        case MQTT_TASK_STATE_WIFI_CHECK: {
            bool joined = false;
            ESP01_Status_t ret = ESP01_IsWifiConnected(&joined);

            if ((ret == ESP01_SUCCESS) && joined) {
                wifiConnected = true;
                LOG_INFO(APP_LAYER_MQTT, "Wi-Fi already joined, skipping CWJAP");
                taskState = MQTT_TASK_STATE_TCP_CONNECT;
            } else {
                wifiConnected = false;
                taskState = MQTT_TASK_STATE_WIFI_CONNECT;
            }
            break;
        }

        case MQTT_TASK_STATE_WIFI_CONNECT: {
            bool targetSeen = false;
            ESP01_Status_t scanStatus = ESP01_LogAvailableNetworks(s_cfg.wifi_ssid, &targetSeen);

            if (scanStatus != ESP01_SUCCESS) {
                LOG_ERR(APP_LAYER_MQTT, "Wi-Fi scan failed (%d), trying join anyway" , (int)scanStatus);
            } else if (!targetSeen) {
                LOG_WARN(APP_LAYER_MQTT, "target Wi-Fi '%s' not visible, trying join anyway",
                         s_cfg.wifi_ssid);
            }

            LOG_INFO(APP_LAYER_MQTT, "joining Wi-Fi '%s'" , s_cfg.wifi_ssid);
            if (ESP01_WifiConnect(s_cfg.wifi_ssid, s_cfg.wifi_password) == ESP01_SUCCESS) {
                wifiConnected = true;
                taskState = MQTT_TASK_STATE_TCP_CONNECT;
            } else {
                wifiConnected = false;
                LOG_ERR(APP_LAYER_MQTT, "Wi-Fi join failed, retry in %lu ms",
                        (unsigned long)MQTT_TASK_WIFI_RETRY_MS);
                osDelay(MQTT_TASK_WIFI_RETRY_MS);
            }
            break;
        }

        case MQTT_TASK_STATE_TCP_CONNECT: {
            if (!wifiConnected) {
                taskState = MQTT_TASK_STATE_WIFI_CHECK;
                break;
            }

            if (tcpConnected) {
                (void)ESP01_Disconnect(&s_netCtx);
                tcpConnected = false;
            }

            LOG_INFO(APP_LAYER_MQTT, "opening TCP to %s:%u",
                     s_cfg.broker_host, s_cfg.broker_port);

            if (ESP01_Connect(&s_netCtx, s_cfg.broker_host, s_cfg.broker_port) == ESP01_SUCCESS) {
                tcpConnected = true;
                taskState = MQTT_TASK_STATE_MQTT_START;
            } else {
                bool joined = false;
                ESP01_Status_t ret = ESP01_IsWifiConnected(&joined);
                wifiConnected = (ret == ESP01_SUCCESS) && joined;
                LOG_ERR(APP_LAYER_MQTT, "TCP open failed, %s",
                        wifiConnected ? "Wi-Fi still joined" : "Wi-Fi is not joined");
                taskState = wifiConnected ? MQTT_TASK_STATE_TCP_CONNECT
                                          : MQTT_TASK_STATE_WIFI_CONNECT;
                osDelay(MQTT_TASK_TCP_RETRY_MS);
            }
            break;
        }

        case MQTT_TASK_STATE_MQTT_START: {
            if (!tcpConnected) {
                taskState = MQTT_TASK_STATE_TCP_CONNECT;
                break;
            }

            ESP01_FillTransportInterface(&s_transport, &s_netCtx);

            MqttAgentConfig_t agentCfg;
            memset(&agentCfg, 0, sizeof(agentCfg));
            agentCfg.pTransport = &s_transport;
            agentCfg.pClientId = s_cfg.client_id;
            agentCfg.clientIdLen = (uint16_t)strlen(s_cfg.client_id);
            agentCfg.pUserName = s_cfg.username;
            agentCfg.userNameLen = s_cfg.username ? (uint16_t)strlen(s_cfg.username) : 0U;
            agentCfg.pPassword = s_cfg.password;
            agentCfg.passwordLen = s_cfg.password ? (uint16_t)strlen(s_cfg.password) : 0U;
            agentCfg.keepAliveSec = 60U;
            agentCfg.cleanStart = true;

            if (MqttAgent_Init(&agentCfg) != MQTT_AGENT_SUCCESS) {
                LOG_ERR(APP_LAYER_MQTT, "MqttAgent_Init failed");
                (void)ESP01_Disconnect(&s_netCtx);
                tcpConnected = false;
                osDelay(MQTT_TASK_MQTT_RETRY_MS);
                taskState = MQTT_TASK_STATE_TCP_CONNECT;
                break;
            }

            if (MqttAgent_Start() != MQTT_AGENT_SUCCESS) {
                LOG_ERR(APP_LAYER_MQTT, "MqttAgent_Start failed");
                (void)ESP01_Disconnect(&s_netCtx);
                tcpConnected = false;
                osDelay(MQTT_TASK_MQTT_RETRY_MS);
                taskState = MQTT_TASK_STATE_TCP_CONNECT;
                break;
            }

            agentStarted = true;
            configCallbacksReady = false;

            if (MqttAgent_WaitConnected(MQTT_TASK_CONNECTED_WAIT_MS) == MQTT_AGENT_SUCCESS) {
                s_mqttConnected = true;
                LOG_INFO(APP_LAYER_MQTT, "MQTT connected");
            } else {
                s_mqttConnected = false;
                LOG_INFO(APP_LAYER_MQTT, "MQTT not connected yet; agent is retrying");
            }

            taskState = MQTT_TASK_STATE_MQTT_RUN;
            break;
        }

        case MQTT_TASK_STATE_MQTT_RUN: {
            MqttAgentState_t agentState = MqttAgent_GetState();
            s_mqttConnected = (agentState == MQTT_AGENT_STATE_CONNECTED);

            if (s_netCtx.linkClosed || (agentState == MQTT_AGENT_STATE_STOPPED)) {
                LOG_INFO(APP_LAYER_MQTT, "MQTT/TCP link stopped, reopening transport");
                mqtt_task_stop_agent(&agentStarted);
                (void)ESP01_Disconnect(&s_netCtx);
                tcpConnected = false;
                configCallbacksReady = false;

                bool joined = false;
                ESP01_Status_t ret = ESP01_IsWifiConnected(&joined);
                wifiConnected = (ret == ESP01_SUCCESS) && joined;
                taskState = wifiConnected ? MQTT_TASK_STATE_TCP_CONNECT
                                          : MQTT_TASK_STATE_WIFI_CONNECT;
                osDelay(MQTT_TASK_MQTT_RETRY_MS);
                break;
            }

            if (!configCallbacksReady && s_mqttConnected) {
                if (mqtt_config_callbacks_init() == 0) {
                    configCallbacksReady = true;
                    LOG_INFO(APP_LAYER_MQTT, "config subscriptions registered");
                } else {
                    LOG_ERR(APP_LAYER_MQTT, "failed to register config subscriptions");
                }
            }

            mqtt_task_service_inspection_queue();
            osDelay(1000U);
            break;
        }

        default:
            taskState = MQTT_TASK_STATE_WIFI_CHECK;
            break;
        }
    }
}

int mqtt_task_init(const MqttTaskConfig_t *cfg)
{
    if (cfg == NULL) {
        return -1;
    }

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg = *cfg;

    if (s_threadId != NULL) {
        return 0;
    }

    const osThreadAttr_t attr = {
        .name = MQTT_TASK_NAME,
        .stack_size = MQTT_TASK_STACK_SZ,
        .priority = MQTT_TASK_PRIORITY,
        .cb_mem = NULL,
        .cb_size = 0U,
        .stack_mem = NULL,
        .tz_module = 0U,
    };

    s_threadId = osThreadNew(mqtt_task, NULL, &attr);
    if (s_threadId == NULL) {
        LOG_ERR(APP_LAYER_MQTT, "failed to create thread");
        return -1;
    }

    return 0;
}

int mqtt_task_publish_inspection(const char *payload, size_t payload_len)
{
    if ((payload == NULL) ||
        (payload_len == 0U) ||
        (s_cfg.client_id == NULL) ||
        !s_mqttConnected) {
        return -1;
    }

    char topic[96];
    int tn = snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_INSPECT_FMT, s_cfg.client_id);
    if ((tn <= 0) || (tn >= (int)sizeof(topic))) {
        return -1;
    }

    return mqtt_task_publish_to_topic(topic, payload, payload_len);
}
