/**
 * @file mqtt_agent_config.h
 * @brief Compile-time knobs for the coreMQTT v5 FreeRTOS agent.
 *
 * Override any of these in your project's compiler flags or by creating a
 * "mqtt_agent_config_override.h" that is included before this file.
 */

#ifndef MQTT_AGENT_CONFIG_H
#define MQTT_AGENT_CONFIG_H

/* ─────────────────────────────────────────────────────────────────────────────
 * FreeRTOS task parameters
 * ───────────────────────────────────────────────────────────────────────────── */

/** Stack depth (words, not bytes) for the MQTT agent task. */
#ifndef MQTT_AGENT_TASK_STACK_DEPTH
#define MQTT_AGENT_TASK_STACK_DEPTH         512U
#endif

/** FreeRTOS priority of the MQTT agent task. */
#ifndef MQTT_AGENT_TASK_PRIORITY
#define MQTT_AGENT_TASK_PRIORITY            ( tskIDLE_PRIORITY + 3U )
#endif

/**
 * @brief Maximum number of commands the agent's command queue can hold.
 *
 * Each slot is backed by a static pool entry. Increase if bursts of
 * back-to-back publishes are expected from multiple producer tasks.
 */
#ifndef MQTT_AGENT_COMMAND_QUEUE_LEN
#define MQTT_AGENT_COMMAND_QUEUE_LEN        8U
#endif

/**
 * @brief Maximum number of simultaneous topic-filter subscriptions tracked
 *        by the agent's subscription registry.
 */
#ifndef MQTT_AGENT_MAX_SUBSCRIPTIONS
#define MQTT_AGENT_MAX_SUBSCRIPTIONS        8U
#endif

/** Maximum length of a topic filter string (including NUL terminator). */
#ifndef MQTT_AGENT_MAX_TOPIC_FILTER_LEN
#define MQTT_AGENT_MAX_TOPIC_FILTER_LEN     96U
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * coreMQTT network buffer
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Size of the network buffer shared by all coreMQTT serialise /
 *        deserialise operations. Must fit the largest expected MQTT packet
 *        including all MQTT 5 property fields.
 */
#ifndef MQTT_AGENT_NETWORK_BUF_SIZE
#define MQTT_AGENT_NETWORK_BUF_SIZE         1024U
#endif

/**
 * @brief Size of the backing store (bytes) for each MQTTPropBuilder_t
 *        embedded inside an outgoing command's property slot.
 */
#ifndef MQTT_AGENT_PROP_BUF_SIZE
#define MQTT_AGENT_PROP_BUF_SIZE            128U
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * QoS state engine (required for QoS 1 / QoS 2 publish reliability)
 * ───────────────────────────────────────────────────────────────────────────── */

/** Maximum concurrent outgoing QoS 1 / QoS 2 publishes tracked. */
#ifndef MQTT_AGENT_OUTGOING_PUBLISH_MAX
#define MQTT_AGENT_OUTGOING_PUBLISH_MAX     4U
#endif

/** Maximum concurrent incoming QoS 2 publishes tracked. */
#ifndef MQTT_AGENT_INCOMING_PUBLISH_MAX
#define MQTT_AGENT_INCOMING_PUBLISH_MAX     4U
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * MQTT 5 CONNECT properties (sent to the broker on every connection)
 * ───────────────────────────────────────────────────────────────────────────── */

/** MQTT keep-alive interval in seconds (0 = broker manages keep-alive). */
#ifndef MQTT_AGENT_KEEP_ALIVE_SEC
#define MQTT_AGENT_KEEP_ALIVE_SEC           60U
#endif

/**
 * @brief Session Expiry Interval (seconds) [MQTT 5 §3.1.3.2.5].
 *  0          = session ends on disconnect (equivalent to MQTT 3.1.1 clean session).
 *  0xFFFFFFFF = session never expires on the broker.
 */
#ifndef MQTT_AGENT_SESSION_EXPIRY_SEC
#define MQTT_AGENT_SESSION_EXPIRY_SEC       0U
#endif

/**
 * @brief Receive Maximum [MQTT 5 §3.1.3.2.3].
 *
 * Caps how many QoS 1/2 PUBLISH packets the broker may send concurrently
 * before receiving an ACK. Match this to MQTT_AGENT_INCOMING_PUBLISH_MAX.
 */
#ifndef MQTT_AGENT_RECEIVE_MAXIMUM
#define MQTT_AGENT_RECEIVE_MAXIMUM          4U
#endif

/**
 * @brief Maximum MQTT Packet Size this client will accept [MQTT 5 §3.1.3.2.4].
 *  0 = do not advertise a limit (broker may send packets of any size).
 */
#ifndef MQTT_AGENT_MAX_PACKET_SIZE
#define MQTT_AGENT_MAX_PACKET_SIZE          MQTT_AGENT_NETWORK_BUF_SIZE
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Reconnect / exponential back-off
 * ───────────────────────────────────────────────────────────────────────────── */

/** Initial reconnect back-off delay (ms). Doubles on each failed attempt. */
#ifndef MQTT_AGENT_RECONNECT_DELAY_BASE_MS
#define MQTT_AGENT_RECONNECT_DELAY_BASE_MS  1000U
#endif

/** Maximum reconnect back-off delay (ms). */
#ifndef MQTT_AGENT_RECONNECT_DELAY_MAX_MS
#define MQTT_AGENT_RECONNECT_DELAY_MAX_MS   30000U
#endif

/**
 * @brief Maximum reconnect attempts before the agent task halts.
 *  0 = retry forever (recommended for always-on devices).
 */
#ifndef MQTT_AGENT_RECONNECT_MAX_RETRIES
#define MQTT_AGENT_RECONNECT_MAX_RETRIES    0U
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * ProcessLoop call cadence
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Milliseconds given to each MQTT_ProcessLoop() invocation.
 *
 * Shorter = lower publish/keep-alive latency, higher CPU wake frequency.
 * 10–50 ms is appropriate for most embedded targets.
 */
#ifndef MQTT_AGENT_PROCESS_LOOP_TIMEOUT_MS
#define MQTT_AGENT_PROCESS_LOOP_TIMEOUT_MS  20U
#endif

/**
 * @brief Maximum command-queue entries the agent will drain in a single
 *        task iteration before calling ProcessLoop again.
 */
#ifndef MQTT_AGENT_CMDS_PER_LOOP
#define MQTT_AGENT_CMDS_PER_LOOP            4U
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Logging
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Log macro for the agent. Routed to the unified logger (layer = mqtt);
 *        severity is gated at run time via app_log_set_level(APP_LAYER_MQTT,..).
 */
#ifndef MQTT_AGENT_LOG
#include "app_log.h"
#define MQTT_AGENT_LOG( fmt, ... )  app_log_emit( APP_LAYER_MQTT, APP_LOG_INFO, fmt, ##__VA_ARGS__ )
#endif

#endif /* MQTT_AGENT_CONFIG_H */
