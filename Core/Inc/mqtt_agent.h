/**
 * @file mqtt_agent.h
 * @brief coreMQTT v5 FreeRTOS agent — public API.
 *
 * Overview
 * ─────────
 * The agent wraps coreMQTT v5 (MQTT 5.0 protocol) in a dedicated FreeRTOS
 * task.  Other tasks interact with the broker exclusively through the
 * thread-safe API below; they never touch the MQTTContext_t directly.
 *
 *  ┌──────────────┐   MqttAgent_Publish()   ┌───────────────────────────┐
 *  │  Sensor Task ├────────────────────────►│                           │
 *  └──────────────┘                         │       MQTT Agent Task     │
 *  ┌──────────────┐   MqttAgent_Subscribe() │  MQTT_ProcessLoop()       │
 *  │  Config Task ├────────────────────────►│  command-queue drain      │
 *  └──────────────┘                         │  subscription dispatch    │◄──► Broker
 *  ┌──────────────┐   topic callback fires  │  reconnect / back-off     │
 *  │  Any Task    │◄────────────────────────┤                           │
 *  └──────────────┘                         └───────────────────────────┘
 *
 * Thread safety model
 * ────────────────────
 * - MqttAgent_Publish / Subscribe / Unsubscribe / Disconnect may be called
 *   from ANY task or from a software timer callback.
 * - They are NEVER safe to call from an ISR (use xQueueSendFromISR +
 *   a dedicated task if ISR-originated publishes are needed).
 * - The MQTT agent task owns the MQTTContext exclusively; no other code
 *   should touch it.
 *
 * MQTT 5 features exposed
 * ────────────────────────
 * - Session Expiry Interval, Receive Maximum, Max Packet Size (CONNECT)
 * - Message Expiry Interval, Content Type, Response Topic, Correlation Data,
 *   Topic Alias, Payload Format Indicator, User Properties (PUBLISH)
 * - Reason codes on DISCONNECT
 * - Per-subscription callbacks receive the full inbound MQTTPropBuilder_t
 *   so higher layers can read any property the broker attached
 */

#ifndef MQTT_AGENT_H
#define MQTT_AGENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "queue.h"

/* coreMQTT v5 */
#include "core_mqtt.h"
#include "core_mqtt_serializer.h"

/* Transport (e.g. the ESP01 driver from esp01_transport.h) */
#include "transport_interface.h"

/* Agent configuration knobs */
#include "mqtt_agent_config.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Return codes
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum
{
    MQTT_AGENT_SUCCESS       =  0,
    MQTT_AGENT_ERR_PARAM     = -1,  /**< Invalid argument passed.               */
    MQTT_AGENT_ERR_QUEUE     = -2,  /**< Command queue full / send failed.       */
    MQTT_AGENT_ERR_TIMEOUT   = -3,  /**< Caller timed out waiting for completion.*/
    MQTT_AGENT_ERR_MQTT      = -4,  /**< Underlying coreMQTT call failed.        */
    MQTT_AGENT_ERR_NO_SLOT   = -5,  /**< Subscription registry is full.         */
    MQTT_AGENT_ERR_NOT_FOUND = -6,  /**< Subscription filter not in registry.   */
    MQTT_AGENT_ERR_STATE     = -7,  /**< Agent not in the right state.           */
} MqttAgentStatus_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Agent state (read-only, reported via MqttAgent_GetState)
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum
{
    MQTT_AGENT_STATE_UNINIT        = 0,
    MQTT_AGENT_STATE_DISCONNECTED,      /**< Transport up, MQTT session closed. */
    MQTT_AGENT_STATE_CONNECTING,        /**< MQTT CONNECT in progress.          */
    MQTT_AGENT_STATE_CONNECTED,         /**< MQTT session active.               */
    MQTT_AGENT_STATE_RECONNECTING,      /**< Waiting in back-off before retry.  */
    MQTT_AGENT_STATE_STOPPED,           /**< MqttAgent_Disconnect called.       */
} MqttAgentState_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Subscription callback
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Invoked by the agent task when an inbound PUBLISH matches a registered
 *        topic filter.
 *
 * @note This callback runs in the context of the MQTT agent task.  Keep it
 *       short; defer heavy work to a notification or a queue.  Do NOT call
 *       any MqttAgent_* API from inside the callback — that would deadlock.
 *
 * @param pTopicName    Topic name string (NOT NUL-terminated; use topicLen).
 * @param topicLen      Length of pTopicName in bytes.
 * @param pPayload      Message payload bytes.
 * @param payloadLen    Payload length in bytes.
 * @param pProps        Pointer to the incoming PUBLISH property builder, or
 *                      NULL if no properties were present. Use MQTTPropGet_*
 *                      functions to extract individual properties.
 * @param pUserCtx      The user-supplied context pointer from MqttAgent_Subscribe.
 */
typedef void ( *MqttTopicCallback_t )( const char            *pTopicName,
                                        uint16_t               topicLen,
                                        const void            *pPayload,
                                        size_t                 payloadLen,
                                        const MQTTPropBuilder_t *pProps,
                                        void                  *pUserCtx );

/* ─────────────────────────────────────────────────────────────────────────────
 * MQTT 5 publish-property helper
 *
 * Callers fill this lightweight struct and pass a pointer to
 * MqttAgent_Publish().  Fields left at their zero-initialised defaults are
 * not added to the outgoing packet (the coreMQTT property builder skips
 * zero-length strings and zero integer values for optional properties).
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct
{
    /** Message Expiry Interval (seconds). 0 = no expiry declared. */
    uint32_t    messageExpiryIntervalSec;

    /**
     * @brief Topic Alias (1–65535). 0 = no alias.
     *
     * The broker must have advertised Topic Alias Maximum > 0 in its CONNACK
     * before an alias can be used.  First use on a new alias MUST include the
     * full topic name; subsequent publishes may send an empty topic with the
     * same alias number.  The agent does NOT manage the alias table; callers
     * are responsible for alias lifecycle.
     */
    uint16_t    topicAlias;

    /**
     * @brief Payload Format Indicator. 0 = unspecified, 1 = UTF-8 encoded.
     */
    uint8_t     payloadFormatIndicator;

    /** NUL-terminated MIME content type string, or NULL to omit. */
    const char *pContentType;
    uint16_t    contentTypeLen;

    /**
     * @brief Response Topic for request-response pattern [MQTT5 §4.10].
     *        NULL to omit.
     */
    const char *pResponseTopic;
    uint16_t    responseTopicLen;

    /** Correlation Data blob. NULL to omit. */
    const void *pCorrelationData;
    uint16_t    correlationDataLen;

    /**
     * @brief Optional array of MQTT 5 User Properties to attach.
     *        Set pUserProps = NULL and userPropsCount = 0 to omit.
     */
    const MQTTUserProperty_t *pUserProps;
    uint16_t                  userPropsCount;
} MqttPublishProps_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * MQTT 5 CONNECT property overrides
 *
 * Used in MqttAgentConfig_t.  Optional — zero-initialise to use defaults
 * from mqtt_agent_config.h.
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct
{
    uint32_t    sessionExpirySec;       /**< 0 = clean start.  0xFFFFFFFF = never. */
    uint16_t    receiveMaximum;         /**< 0 = use MQTT_AGENT_RECEIVE_MAXIMUM.    */
    uint32_t    maxPacketSize;          /**< 0 = use MQTT_AGENT_MAX_PACKET_SIZE.    */

    /** Optional Will (Last Testament) message. Set pWillTopic = NULL to skip. */
    struct
    {
        const char  *pTopic;
        uint16_t     topicLen;
        const void  *pPayload;
        size_t       payloadLen;
        MQTTQoS_t    qos;
        bool         retain;
        uint32_t     willDelayIntervalSec;
    } will;

    /** Optional user properties on CONNECT. Set pUserProps = NULL to skip. */
    const MQTTUserProperty_t *pUserProps;
    uint16_t                  userPropsCount;
} MqttConnectProps_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Agent initialisation configuration
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct
{
    /* Transport layer (must be fully initialised before MqttAgent_Init). */
    TransportInterface_t  *pTransport;

    /* Broker connection parameters. */
    const char            *pClientId;
    uint16_t               clientIdLen;
    const char            *pUserName;     /**< NULL if not used. */
    uint16_t               userNameLen;
    const char            *pPassword;     /**< NULL if not used. */
    uint16_t               passwordLen;
    uint16_t               keepAliveSec;  /**< 0 = use MQTT_AGENT_KEEP_ALIVE_SEC. */

    /**
     * @brief Whether to request a clean start from the broker.
     *
     * MQTT 5 replaces the MQTT 3.1.1 "clean session" flag with a two-part
     * mechanism: Clean Start bit (clears existing session on the broker) +
     * Session Expiry Interval (how long the broker retains it after disconnect).
     */
    bool                   cleanStart;

    /** Optional MQTT 5 CONNECT property overrides.  NULL = use config defaults. */
    const MqttConnectProps_t *pConnectProps;

    /**
     * @brief Optional callback invoked on every state transition.
     *        Called from the agent task context.  May be NULL.
     */
    void ( *pfStateChangeCb )( MqttAgentState_t newState, void *pCtx );
    void  *pStateChangeCbCtx;

} MqttAgentConfig_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Initialisation & lifecycle
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the agent.
 *
 * Allocates all static resources (queue, mutex, registry) and configures
 * coreMQTT.  Does NOT create the FreeRTOS task or connect to the broker;
 * call MqttAgent_Start() for that.
 *
 * Must be called once before any other MqttAgent_* function.
 *
 * @param[in] pConfig  Fully populated agent configuration.
 *
 * @return MQTT_AGENT_SUCCESS or a negative error code.
 */
MqttAgentStatus_t MqttAgent_Init( const MqttAgentConfig_t *pConfig );

/**
 * @brief Create the agent FreeRTOS task and initiate the first connection.
 *
 * May only be called after a successful MqttAgent_Init().
 *
 * @return MQTT_AGENT_SUCCESS or MQTT_AGENT_ERR_STATE if already started.
 */
MqttAgentStatus_t MqttAgent_Start( void );

/**
 * @brief Send a graceful MQTT DISCONNECT and stop the agent task.
 *
 * Blocks until the DISCONNECT packet has been sent or timeoutMs expires.
 *
 * @param[in] timeoutMs  Maximum time to wait (portMAX_DELAY = wait forever).
 *
 * @return MQTT_AGENT_SUCCESS or MQTT_AGENT_ERR_TIMEOUT.
 */
MqttAgentStatus_t MqttAgent_Stop( uint32_t timeoutMs );

/**
 * @brief Return the current agent state.
 *
 * Thread-safe; may be called from any context including ISR-deferred tasks.
 */
MqttAgentState_t MqttAgent_GetState( void );

/* ─────────────────────────────────────────────────────────────────────────────
 * Publish
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Enqueue an MQTT 5 PUBLISH for transmission.
 *
 * The function copies all necessary data into an internal command slot so
 * the caller's buffers may be modified or freed immediately after return.
 *
 * @note Payloads that fit the agent command buffer are copied before return.
 *       Larger QoS 1/2 payloads must remain valid until PUBACK/PUBCOMP because
 *       coreMQTT may retransmit them.
 *
 * @param[in] pTopicName     Destination topic string (need not be NUL-terminated).
 * @param[in] topicLen       Length of pTopicName.
 * @param[in] pPayload       Payload bytes. May be NULL for zero-length payloads.
 * @param[in] payloadLen     Payload byte count.
 * @param[in] qos            QoS level for this message.
 * @param[in] retain         Set the RETAIN flag.
 * @param[in] pProps         Optional MQTT 5 publish properties. NULL = none.
 * @param[in] timeoutMs      How long to wait if the command queue is full.
 *                           0 = non-blocking; portMAX_DELAY = block forever.
 *
 * @return MQTT_AGENT_SUCCESS if the command was enqueued successfully.
 *         MQTT_AGENT_ERR_QUEUE if the queue was full within timeoutMs.
 */
MqttAgentStatus_t MqttAgent_Publish( const char              *pTopicName,
                                      uint16_t                 topicLen,
                                      const void              *pPayload,
                                      size_t                   payloadLen,
                                      MQTTQoS_t                qos,
                                      bool                     retain,
                                      const MqttPublishProps_t *pProps,
                                      uint32_t                 timeoutMs );

/* ─────────────────────────────────────────────────────────────────────────────
 * Subscribe / Unsubscribe
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Register a callback for a topic filter and subscribe to it.
 *
 * The filter and callback are added to the local subscription registry first,
 * then a SUBSCRIBE command is enqueued for the agent task.
 *
 * @note Wildcard filters ('+', '#') are stored verbatim and matched by
 *       coreMQTT's built-in MQTT_MatchTopic() on every inbound PUBLISH.
 *
 * @param[in] pFilter        NUL-terminated topic filter string.
 * @param[in] qos            Requested QoS level.
 * @param[in] pfCallback     Function invoked when a matching message arrives.
 * @param[in] pUserCtx       Passed back to pfCallback unmodified.
 * @param[in] timeoutMs      Queue wait timeout.
 *
 * @return MQTT_AGENT_SUCCESS, MQTT_AGENT_ERR_NO_SLOT, or MQTT_AGENT_ERR_QUEUE.
 */
MqttAgentStatus_t MqttAgent_Subscribe( const char         *pFilter,
                                        MQTTQoS_t           qos,
                                        MqttTopicCallback_t pfCallback,
                                        void               *pUserCtx,
                                        uint32_t            timeoutMs );

/**
 * @brief Unsubscribe from a topic filter and remove the local callback.
 *
 * @param[in] pFilter    NUL-terminated topic filter (must match exactly what
 *                       was passed to MqttAgent_Subscribe).
 * @param[in] timeoutMs  Queue wait timeout.
 *
 * @return MQTT_AGENT_SUCCESS, MQTT_AGENT_ERR_NOT_FOUND, or MQTT_AGENT_ERR_QUEUE.
 */
MqttAgentStatus_t MqttAgent_Unsubscribe( const char *pFilter,
                                          uint32_t    timeoutMs );

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Block the calling task until the agent reaches MQTT_AGENT_STATE_CONNECTED
 *        or timeoutMs elapses.
 *
 * Useful for tasks that must not publish before the session is established.
 *
 * @return MQTT_AGENT_SUCCESS when connected, MQTT_AGENT_ERR_TIMEOUT otherwise.
 */
MqttAgentStatus_t MqttAgent_WaitConnected( uint32_t timeoutMs );

#ifdef __cplusplus
}
#endif

#endif /* MQTT_AGENT_H */
