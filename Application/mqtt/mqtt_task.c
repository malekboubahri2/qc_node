#include "mqtt_task.h"
#include "esp01_transport.h"
#include "mqtt_agent.h"
#include "mqtt_topics.h"
#include "mqtt_config_callbacks.h"
#include "config_store.h"
#include <string.h>
#include <stdio.h>

/* CMSIS‑RTOS2 includes */
#include "cmsis_os2.h"

/* Task settings – CMSIS‑RTOS2 */
#define MQTT_TASK_NAME        "mqtt_task"
#define MQTT_TASK_STACK_SZ    4096U
/* Map FreeRTOS priority (idle+2) to a CMSIS priority. Normal priority is a good match. */
#define MQTT_TASK_PRIORITY    osPriorityNormal

static MqttTaskConfig_t s_cfg;
static osThreadId_t     s_threadId = NULL;

static NetworkContext_t s_netCtx;
static TransportInterface_t s_transport;
static bool s_config_callbacks_registered = false;

static void mqtt_task(void *pv)
{
    (void)pv;
    printf("mqtt_task: started\n");

    /* Initialise config store (OSPI flash already initialised in main) */
    if (config_store_init() != 0) {
        printf("config_store_init failed\n");
        osThreadExit();
        return;
    }

    /* Initialise ESP01 driver */
    if (ESP01_Init(&s_netCtx, &huart2) != ESP01_SUCCESS) {
        printf("ESP01_Init failed\n");
        osThreadExit();
        return;
    }

    /* ──────────────────────────────────────────────────────────────────────
     * MQTT Agent initialization — ONCE, outside the reconnection loop
     * ────────────────────────────────────────────────────────────────────── */
    MqttAgentConfig_t agentCfg;
    memset(&agentCfg, 0, sizeof(agentCfg));
    agentCfg.pClientId = s_cfg.client_id;
    agentCfg.clientIdLen = (uint16_t)strlen(s_cfg.client_id);
    agentCfg.pUserName = s_cfg.username;
    agentCfg.userNameLen = s_cfg.username ? (uint16_t)strlen(s_cfg.username) : 0;
    agentCfg.pPassword = s_cfg.password;
    agentCfg.passwordLen = s_cfg.password ? (uint16_t)strlen(s_cfg.password) : 0;
    agentCfg.keepAliveSec = 60;
    agentCfg.cleanStart = true;

    /* Subscription state, persists across WiFi reconnections */
    bool config_subscribed = false;
    bool mqtt_agent_started = false;

    /* ──────────────────────────────────────────────────────────────────────
     * WiFi/TCP reconnection loop — NOT MQTT re-initialization
     * ────────────────────────────────────────────────────────────────────── */
    for (;;) {
        bool target_seen = false;
        ESP01_Status_t scan_status = ESP01_LogAvailableNetworks(s_cfg.wifi_ssid, &target_seen);

        if (scan_status != ESP01_SUCCESS) {
            printf("mqtt_task: Wi-Fi scan failed (%d), trying connection anyway\n", (int)scan_status);
        } else if (!target_seen) {
            printf("mqtt_task: target Wi-Fi '%s' not visible in scan, trying connection anyway\n",
                   s_cfg.wifi_ssid ? s_cfg.wifi_ssid : "");
        }

        printf("mqtt_task: connecting to Wi-Fi '%s'\n", s_cfg.wifi_ssid);
        if (ESP01_WifiConnect(s_cfg.wifi_ssid, s_cfg.wifi_password) != ESP01_SUCCESS) {
            printf("ESP01_WifiConnect failed, retry in 5s\n");
            osDelay(5000U);
            continue;
        }

        printf("mqtt_task: opening TCP to %s:%u\n", s_cfg.broker_host, s_cfg.broker_port);
        if (ESP01_Connect(&s_netCtx, s_cfg.broker_host, s_cfg.broker_port) != ESP01_SUCCESS) {
            printf("ESP01_Connect failed, retry in 5s\n");
            osDelay(5000U);
            continue;
        }

        /* ──────────────────────────────────────────────────────────────────
         * Transport connected; initialize MQTT agent on first connection only
         * ────────────────────────────────────────────────────────────────── */
        if (!mqtt_agent_started) {
            ESP01_FillTransportInterface(&s_transport, &s_netCtx);
            agentCfg.pTransport = &s_transport;

            if (MqttAgent_Init(&agentCfg) != MQTT_AGENT_SUCCESS) {
                printf("MqttAgent_Init failed\n");
                ESP01_Disconnect(&s_netCtx);
                osDelay(5000U);
                continue;
            }

            if (MqttAgent_Start() != MQTT_AGENT_SUCCESS) {
                printf("MqttAgent_Start failed\n");
                ESP01_Disconnect(&s_netCtx);
                osDelay(5000U);
                continue;
            }

            mqtt_agent_started = true;
            printf("mqtt_task: MQTT agent started\n");
        }

        /* ──────────────────────────────────────────────────────────────────
         * Monitor MQTT agent state and manage subscriptions
         * ────────────────────────────────────────────────────────────────── */
        for (;;) {
            MqttAgentState_t state = MqttAgent_GetState();

            /* Subscribe to config topics once connected */
            if (!config_subscribed && state == MQTT_AGENT_STATE_CONNECTED) {
                if (mqtt_config_callbacks_init() == 0) {
                    config_subscribed = true;
                    s_config_callbacks_registered = true;
                    printf("mqtt_task: config subscriptions registered\n");
                } else {
                    printf("mqtt_task: failed to register config subscriptions\n");
                }
            }

            /* Reset subscription flag on disconnect to resubscribe after reconnection */
            if (config_subscribed && state != MQTT_AGENT_STATE_CONNECTED) {
                config_subscribed = false;
                printf("mqtt_task: cleared subscription flag for next reconnection\n");
            }

            /* Break on stop to attempt WiFi reconnection */
            if (state == MQTT_AGENT_STATE_STOPPED) {
                printf("mqtt_task: agent stopped, will retry WiFi connection\n");
                break;
            }

            osDelay(1000U);
        }

        /* Clean up and try reconnect sequence */
        printf("mqtt_task: connection lost, disconnecting transport\n");
        ESP01_Disconnect(&s_netCtx);
        osDelay(5000U);
    }
}

int mqtt_task_init(const MqttTaskConfig_t *cfg)
{
    if (!cfg) return -1;
    /* Copy config */
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.wifi_ssid = cfg->wifi_ssid;
    s_cfg.wifi_password = cfg->wifi_password;
    s_cfg.broker_host = cfg->broker_host;
    s_cfg.broker_port = cfg->broker_port;
    s_cfg.client_id = cfg->client_id;
    s_cfg.username = cfg->username;
    s_cfg.password = cfg->password;

    if (s_threadId != NULL) return 0; /* already running */

    /* CMSIS‑RTOS2 thread creation */
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
        printf("Failed to create mqtt_task thread\n");
        return -1;
    }

    return 0;
}

int mqtt_task_publish_inspection(const char *payload, size_t payload_len)
{
    if (!payload || payload_len == 0) return -1;

    if (s_cfg.client_id == NULL) return -1;

    char topic[128];
    int tn = snprintf(topic, sizeof(topic), MQTT_TOPIC_DEVICE_INSPECT_FMT, s_cfg.client_id);
    if (tn <= 0 || (size_t)tn >= sizeof(topic)) return -1;

    MqttAgentStatus_t ret = MqttAgent_Publish(topic, (uint16_t)tn,
                                              payload, payload_len,
                                              MQTTQoS1, false, NULL, 0);
    return (ret == MQTT_AGENT_SUCCESS) ? 0 : -1;
}
