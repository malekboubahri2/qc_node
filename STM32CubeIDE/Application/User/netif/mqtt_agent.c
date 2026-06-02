/**
 * @file mqtt_agent.c
 * @brief coreMQTT v5 FreeRTOS agent — implementation.
 *
 * Internal architecture
 * ──────────────────────
 *
 *  agentTask()
 *  ┌──────────────────────────────────────────────────────────────────┐
 *  │                                                                  │
 *  │  while connected:                                                │
 *  │    drain_command_queue()   ← publishes, subscribes, stop        │
 *  │    MQTT_ProcessLoop()      ← keep-alive, inbound dispatch       │
 *  │    if( disconnected )                                            │
 *  │        reconnect_with_backoff()                                  │
 *  │        resubscribe_all()   ← replay subscription registry       │
 *  │                                                                  │
 *  │  inbound PUBLISH → dispatch_to_subscribers()                     │
 *  │      walks subscription registry, calls matching callbacks       │
 *  └──────────────────────────────────────────────────────────────────┘
 *
 *  Command pool
 *  ─────────────
 *  MqttCommand_t objects are never heap-allocated; they come from a fixed
 *  static pool of MQTT_AGENT_COMMAND_QUEUE_LEN entries.  A FreeRTOS mutex
 *  (s_poolMutex) serialises pool alloc/free.  The command queue itself
 *  holds pointers, not copies, so sizeof(QueueHandle) stays small.
 */

#include "mqtt_agent.h"
#include "app_log.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal command types
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum
{
    CMD_PUBLISH     = 0,
    CMD_SUBSCRIBE,
    CMD_UNSUBSCRIBE,
    CMD_STOP,
} CommandType_t;

/**
 * @brief A command enqueued by a producer task for the agent task to execute.
 *
 * The struct is allocated from the static pool and a pointer is sent over
 * the FreeRTOS queue.  On completion the agent frees it back to the pool.
 */
typedef struct MqttCommand
{
    CommandType_t type;

    union
    {
        /* ── CMD_PUBLISH ──────────────────────────────────────────────────── */
        struct
        {
            MQTTPublishInfo_t info;

            /* Backing store for the property builder. */
            uint8_t           propBuf[ MQTT_AGENT_PROP_BUF_SIZE ];
            MQTTPropBuilder_t propBuilder;
            bool              hasProps;

            /*
             * Topic and payload are stored inline so the caller's buffers
             * can be released immediately after MqttAgent_Publish() returns.
             *
             * NOTE: Topic is always copied. Payload is copied when it fits in
             * payloadBuf; larger QoS 1/2 payloads must remain valid until
             * PUBACK/PUBCOMP because coreMQTT may retransmit them.
             */
            char    topicBuf[ MQTT_AGENT_MAX_TOPIC_FILTER_LEN ];
            uint8_t payloadBuf[ 256 ];   /* inline copy for small QoS 0 msgs */
            bool    payloadCopied;
        } publish;

        /* ── CMD_SUBSCRIBE ────────────────────────────────────────────────── */
        struct
        {
            MQTTSubscribeInfo_t info;
            char                filterBuf[ MQTT_AGENT_MAX_TOPIC_FILTER_LEN ];
            MqttTopicCallback_t pfCallback;
            void               *pUserCtx;
        } subscribe;

        /* ── CMD_UNSUBSCRIBE ──────────────────────────────────────────────── */
        struct
        {
            MQTTSubscribeInfo_t info;
            char                filterBuf[ MQTT_AGENT_MAX_TOPIC_FILTER_LEN ];
        } unsubscribe;
    } u;

    /**
     * @brief Optional completion semaphore.
     *
     * If non-NULL, the agent gives it after the command completes so that a
     * synchronous caller can block until done.  NULL = fire-and-forget.
     */
    SemaphoreHandle_t  hCompletionSem;
    MqttAgentStatus_t  result;
    bool               inUse;

} MqttCommand_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Subscription registry entry
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct
{
    char                filterBuf[ MQTT_AGENT_MAX_TOPIC_FILTER_LEN ];
    uint16_t            filterLen;
    MQTTQoS_t           qos;
    MqttTopicCallback_t pfCallback;
    void               *pUserCtx;
    bool                active;
} SubEntry_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Module-static state
 * ───────────────────────────────────────────────────────────────────────────── */

/* ── coreMQTT objects ────────────────────────────────────────────────────── */
static MQTTContext_t        s_mqttCtx;
static uint8_t              s_netBuf[ MQTT_AGENT_NETWORK_BUF_SIZE ];
static MQTTPubAckInfo_t     s_outRecords[ MQTT_AGENT_OUTGOING_PUBLISH_MAX ];
static MQTTPubAckInfo_t     s_inRecords [ MQTT_AGENT_INCOMING_PUBLISH_MAX ];
static uint8_t              s_ackPropsBuf[ MQTT_AGENT_PROP_BUF_SIZE ];

/* ── Subscription registry ───────────────────────────────────────────────── */
static SubEntry_t           s_subs[ MQTT_AGENT_MAX_SUBSCRIPTIONS ];

/* ── Command pool and queue ──────────────────────────────────────────────── */
static MqttCommand_t        s_cmdPool[ MQTT_AGENT_COMMAND_QUEUE_LEN ];
static StaticSemaphore_t    s_poolMutexBuf;
static SemaphoreHandle_t    s_poolMutex;
static StaticQueue_t        s_queueBuf;
static uint8_t              s_queueStorage[ MQTT_AGENT_COMMAND_QUEUE_LEN
                                             * sizeof( MqttCommand_t * ) ];
static QueueHandle_t        s_cmdQueue;

/* ── Connected event group bit (used by WaitConnected) ───────────────────── */
static StaticSemaphore_t    s_connectedSemBuf;
static SemaphoreHandle_t    s_connectedSem;   /* binary sem, given when connected */

/* ── Agent state ─────────────────────────────────────────────────────────── */
static volatile MqttAgentState_t s_state = MQTT_AGENT_STATE_UNINIT;
static MqttAgentConfig_t         s_cfg;                 /* copy of init config */

/* ── FreeRTOS task handle ────────────────────────────────────────────────── */
static TaskHandle_t    s_agentTask;
static StackType_t     s_agentStack[ MQTT_AGENT_TASK_STACK_DEPTH ];
static StaticTask_t    s_agentTCB;

/* ── Reconnect backoff state ─────────────────────────────────────────────── */
static uint32_t        s_backoffMs     = MQTT_AGENT_RECONNECT_DELAY_BASE_MS;
static uint32_t        s_retryCount    = 0U;

/* ─────────────────────────────────────────────────────────────────────────────
 * Forward declarations (internal)
 * ───────────────────────────────────────────────────────────────────────────── */

static void             agent_task          ( void *pv );
static MQTTStatus_t     do_connect          ( void );
static MQTTStatus_t     do_subscribe        ( MqttCommand_t *pCmd );
static MQTTStatus_t     do_unsubscribe      ( MqttCommand_t *pCmd );
static MQTTStatus_t     do_publish          ( MqttCommand_t *pCmd );
static void             drain_command_queue ( uint32_t maxCmds );
static void             resubscribe_all     ( void );
static void             dispatch_inbound    ( const MQTTPublishInfo_t  *pPub,
                                              const MQTTPropBuilder_t  *pProps );
static void             set_state           ( MqttAgentState_t s );

static MqttCommand_t   *pool_alloc          ( void );
static void             pool_free           ( MqttCommand_t *p );

static bool             mqtt_event_callback ( MQTTContext_t               *pCtx,
                                              MQTTPacketInfo_t           *pPacket,
                                              MQTTDeserializedInfo_t     *pInfo,
                                              MQTTSuccessFailReasonCode_t *pReasonCode,
                                              MQTTPropBuilder_t          *pSendProps,
                                              MQTTPropBuilder_t          *pGetProps );
static uint32_t         get_time_ms         ( void );

/* ─────────────────────────────────────────────────────────────────────────────
 * Helpers
 * ───────────────────────────────────────────────────────────────────────────── */

static inline void set_state( MqttAgentState_t s )
{
    s_state = s;

    if( s_cfg.pfStateChangeCb != NULL )
    {
        s_cfg.pfStateChangeCb( s, s_cfg.pStateChangeCbCtx );
    }

    if( s == MQTT_AGENT_STATE_CONNECTED )
    {
        /* Unblock any task waiting in MqttAgent_WaitConnected(). */
        xSemaphoreGive( s_connectedSem );
    }
}

static uint32_t get_time_ms( void )
{
    return ( uint32_t )( xTaskGetTickCount() * portTICK_PERIOD_MS );
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Command pool
 * ───────────────────────────────────────────────────────────────────────────── */

static MqttCommand_t *pool_alloc( void )
{
    MqttCommand_t *p = NULL;

    if( xSemaphoreTake( s_poolMutex, pdMS_TO_TICKS( 10U ) ) == pdTRUE )
    {
        for( uint32_t i = 0U; i < MQTT_AGENT_COMMAND_QUEUE_LEN; i++ )
        {
            if( !s_cmdPool[ i ].inUse )
            {
                memset( &s_cmdPool[ i ], 0, sizeof( s_cmdPool[ i ] ) );
                s_cmdPool[ i ].inUse = true;
                p = &s_cmdPool[ i ];
                break;
            }
        }

        xSemaphoreGive( s_poolMutex );
    }

    return p;
}

static void pool_free( MqttCommand_t *p )
{
    if( p == NULL ) return;

    if( xSemaphoreTake( s_poolMutex, portMAX_DELAY ) == pdTRUE )
    {
        p->inUse = false;
        xSemaphoreGive( s_poolMutex );
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * MQTT 5 property builder helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Populate a MQTTPropBuilder_t from our MqttPublishProps_t DTO.
 *
 * @return MQTTSuccess or the first error encountered while adding properties.
 */
static MQTTStatus_t build_publish_props( MQTTPropBuilder_t       *pBuilder,
                                          uint8_t                 *pBuf,
                                          size_t                   bufSize,
                                          const MqttPublishProps_t *pProps )
{
    MQTTStatus_t ret = MQTTSuccess;

    /* Initialise builder against the backing buffer. */
    memset( pBuilder, 0, sizeof( *pBuilder ) );
    pBuilder->pBuffer = pBuf;
    pBuilder->bufferLength    = bufSize;

    if( pProps == NULL )
    {
        return MQTTSuccess;
    }

    if( ( ret == MQTTSuccess ) && ( pProps->payloadFormatIndicator != 0U ) )
    {
        ret = MQTTPropAdd_PayloadFormat( pBuilder,
                                         pProps->payloadFormatIndicator,
                                         MQTT_PROP_NO_VALIDATE );
    }

    if( ( ret == MQTTSuccess ) && ( pProps->messageExpiryIntervalSec != 0U ) )
    {
        ret = MQTTPropAdd_MessageExpiry( pBuilder,
                                          pProps->messageExpiryIntervalSec,
                                          MQTT_PROP_NO_VALIDATE );
    }

    if( ( ret == MQTTSuccess ) && ( pProps->topicAlias != 0U ) )
    {
        ret = MQTTPropAdd_TopicAlias( pBuilder,
                                      pProps->topicAlias,
                                      MQTT_PROP_NO_VALIDATE );
    }

    if( ( ret == MQTTSuccess )
        && ( pProps->pResponseTopic != NULL )
        && ( pProps->responseTopicLen > 0U ) )
    {
        ret = MQTTPropAdd_ResponseTopic( pBuilder,
                                          pProps->pResponseTopic,
                                          pProps->responseTopicLen,
                                          MQTT_PROP_NO_VALIDATE );
    }

    if( ( ret == MQTTSuccess )
        && ( pProps->pCorrelationData != NULL )
        && ( pProps->correlationDataLen > 0U ) )
    {
        ret = MQTTPropAdd_CorrelationData( pBuilder,
                                            pProps->pCorrelationData,
                                            pProps->correlationDataLen,
                                            MQTT_PROP_NO_VALIDATE );
    }

    if( ( ret == MQTTSuccess )
        && ( pProps->pContentType != NULL )
        && ( pProps->contentTypeLen > 0U ) )
    {
        ret = MQTTPropAdd_ContentType( pBuilder,
                                        pProps->pContentType,
                                        pProps->contentTypeLen,
                                        MQTT_PROP_NO_VALIDATE );
    }

    for( uint16_t i = 0U;
         ( ret == MQTTSuccess ) && ( i < pProps->userPropsCount );
         i++ )
    {
        ret = MQTTPropAdd_UserProp( pBuilder,
                                    &pProps->pUserProps[ i ],
                                    MQTT_PROP_NO_VALIDATE );
    }

    return ret;
}

/**
 * @brief Build the CONNECT property block (session expiry, receive max, etc.)
 */
static MQTTStatus_t build_connect_props( MQTTPropBuilder_t        *pBuilder,
                                          uint8_t                  *pBuf,
                                          size_t                    bufSize,
                                          const MqttConnectProps_t *pOvr )
{
    MQTTStatus_t ret    = MQTTSuccess;
    uint32_t     expiry = MQTT_AGENT_SESSION_EXPIRY_SEC;
    uint16_t     rcvMax = MQTT_AGENT_RECEIVE_MAXIMUM;
    uint32_t     maxPkt = MQTT_AGENT_MAX_PACKET_SIZE;

    memset( pBuilder, 0, sizeof( *pBuilder ) );
    pBuilder->pBuffer = pBuf;
    pBuilder->bufferLength    = bufSize;

    /* Apply caller overrides. */
    if( pOvr != NULL )
    {
        if( pOvr->sessionExpirySec != 0U )  expiry = pOvr->sessionExpirySec;
        if( pOvr->receiveMaximum   != 0U )  rcvMax = pOvr->receiveMaximum;
        if( pOvr->maxPacketSize    != 0U )  maxPkt = pOvr->maxPacketSize;
    }

    ret = MQTTPropAdd_SessionExpiry( pBuilder, expiry, MQTT_PROP_VALIDATE_CONNECT );

    if( ret == MQTTSuccess )
    {
        ret = MQTTPropAdd_ReceiveMax( pBuilder, rcvMax, MQTT_PROP_VALIDATE_CONNECT );
    }

    if( ret == MQTTSuccess )
    {
        ret = MQTTPropAdd_MaxPacketSize( pBuilder, maxPkt, MQTT_PROP_VALIDATE_CONNECT );
    }

    /* Optional user properties on CONNECT. */
    if( ( ret == MQTTSuccess ) && ( pOvr != NULL ) && ( pOvr->pUserProps != NULL ) )
    {
        for( uint16_t i = 0U;
             ( ret == MQTTSuccess ) && ( i < pOvr->userPropsCount );
             i++ )
        {
            ret = MQTTPropAdd_UserProp( pBuilder,
                                        &pOvr->pUserProps[ i ],
                                        MQTT_PROP_NO_VALIDATE );
        }
    }

    return ret;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * MQTT connect / reconnect
 * ───────────────────────────────────────────────────────────────────────────── */

static MQTTStatus_t do_connect( void )
{
    MQTTStatus_t     ret;
    MQTTConnectInfo_t ci;
    bool              sessionPresent = false;

    /* ── CONNECT info ───────────────────────────────────────────────────── */
    memset( &ci, 0, sizeof( ci ) );
    ci.cleanSession           = s_cfg.cleanStart;
    ci.pClientIdentifier      = s_cfg.pClientId;
    ci.clientIdentifierLength = s_cfg.clientIdLen;
    ci.keepAliveSeconds       = ( s_cfg.keepAliveSec != 0U )
                                    ? s_cfg.keepAliveSec
                                    : MQTT_AGENT_KEEP_ALIVE_SEC;

    if( s_cfg.pUserName != NULL )
    {
        ci.pUserName    = s_cfg.pUserName;
        ci.userNameLength = s_cfg.userNameLen;
    }

    if( s_cfg.pPassword != NULL )
    {
        ci.pPassword      = s_cfg.pPassword;
        ci.passwordLength = s_cfg.passwordLen;
    }

    /* ── CONNECT properties ─────────────────────────────────────────────── */
    uint8_t           propBuf[ MQTT_AGENT_PROP_BUF_SIZE ];
    MQTTPropBuilder_t propBuilder;

    ret = build_connect_props( &propBuilder,
                                propBuf,
                                sizeof( propBuf ),
                                s_cfg.pConnectProps );

    if( ret != MQTTSuccess )
    {
        MQTT_AGENT_LOG( "CONNECT property build failed: %d", ret );
        return ret;
    }

    /* ── Will / LWT ─────────────────────────────────────────────────────── */
    MQTTPublishInfo_t  willInfo;
    MQTTPropBuilder_t  willProps;
    uint8_t            willPropBuf[ 32 ];
    MQTTPublishInfo_t *pWill = NULL;
    MQTTPropBuilder_t *pWillProps = NULL;

    if( ( s_cfg.pConnectProps != NULL )
        && ( s_cfg.pConnectProps->will.pTopic != NULL ) )
    {
        const MqttConnectProps_t *cp = s_cfg.pConnectProps;

        memset( &willInfo, 0, sizeof( willInfo ) );
        willInfo.pTopicName      = cp->will.pTopic;
        willInfo.topicNameLength = cp->will.topicLen;
        willInfo.pPayload        = cp->will.pPayload;
        willInfo.payloadLength   = cp->will.payloadLen;
        willInfo.qos             = cp->will.qos;
        willInfo.retain          = cp->will.retain;
        pWill = &willInfo;

        /* Will Delay Interval property. */
        if( cp->will.willDelayIntervalSec > 0U )
        {
            memset( &willProps,    0, sizeof( willProps    ) );
            willProps.pBuffer = willPropBuf;
            willProps.bufferLength    = sizeof( willPropBuf );
            MQTTPropAdd_WillDelayInterval( &willProps,
                                            cp->will.willDelayIntervalSec,
                                            MQTT_PROP_NO_VALIDATE );
            pWillProps = &willProps;
        }
    }

    ret = MQTT_Connect( &s_mqttCtx,
                        &ci,
                        pWill,
                        5000U,
                        &sessionPresent,
                        &propBuilder,
                        pWillProps );

    if( ret == MQTTSuccess )
    {
        MQTT_AGENT_LOG( "Connected (sessionPresent=%d)", sessionPresent );
    }
    else
    {
        MQTT_AGENT_LOG( "CONNECT failed: %d", ret );
    }

    return ret;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Subscribe / unsubscribe helpers (run inside agent task)
 * ───────────────────────────────────────────────────────────────────────────── */

static MQTTStatus_t do_subscribe( MqttCommand_t *pCmd )
{
    MQTTStatus_t ret;
    uint16_t     packetId = MQTT_GetPacketId( &s_mqttCtx );

    ret = MQTT_Subscribe( &s_mqttCtx,
                           &pCmd->u.subscribe.info,
                           1U,
                           packetId,
                           NULL );

    if( ret == MQTTSuccess )
    {
        /* Register the callback in the local subscription table. */
        for( uint32_t i = 0U; i < MQTT_AGENT_MAX_SUBSCRIPTIONS; i++ )
        {
            if( !s_subs[ i ].active )
            {
                strncpy( s_subs[ i ].filterBuf,
                         pCmd->u.subscribe.filterBuf,
                         MQTT_AGENT_MAX_TOPIC_FILTER_LEN - 1U );
                s_subs[ i ].filterLen  = pCmd->u.subscribe.info.topicFilterLength;
                s_subs[ i ].qos        = pCmd->u.subscribe.info.qos;
                s_subs[ i ].pfCallback = pCmd->u.subscribe.pfCallback;
                s_subs[ i ].pUserCtx   = pCmd->u.subscribe.pUserCtx;
                s_subs[ i ].active     = true;
                break;
            }
        }
    }

    return ret;
}

static MQTTStatus_t do_unsubscribe( MqttCommand_t *pCmd )
{
    MQTTStatus_t ret;
    uint16_t     packetId = MQTT_GetPacketId( &s_mqttCtx );

    ret = MQTT_Unsubscribe( &s_mqttCtx,
                             &pCmd->u.unsubscribe.info,
                             1U,
                             packetId,
                             NULL );

    if( ret == MQTTSuccess )
    {
        /* Remove from local registry. */
        for( uint32_t i = 0U; i < MQTT_AGENT_MAX_SUBSCRIPTIONS; i++ )
        {
            if( s_subs[ i ].active &&
                ( strncmp( s_subs[ i ].filterBuf,
                           pCmd->u.unsubscribe.filterBuf,
                           MQTT_AGENT_MAX_TOPIC_FILTER_LEN ) == 0 ) )
            {
                s_subs[ i ].active = false;
                break;
            }
        }
    }

    return ret;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Publish helper (run inside agent task)
 * ───────────────────────────────────────────────────────────────────────────── */

static MQTTStatus_t do_publish( MqttCommand_t *pCmd )
{
    MQTTStatus_t       ret;
    MQTTPropBuilder_t *pProps = pCmd->u.publish.hasProps
                                    ? &pCmd->u.publish.propBuilder
                                    : NULL;
    uint16_t packetId = ( pCmd->u.publish.info.qos > MQTTQoS0 )
                            ? MQTT_GetPacketId( &s_mqttCtx )
                            : 0U;

    ret = MQTT_Publish( &s_mqttCtx,
                        &pCmd->u.publish.info,
                        packetId,
                        pProps );

    return ret;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Command queue drain (run inside agent task each iteration)
 * ───────────────────────────────────────────────────────────────────────────── */

static void drain_command_queue( uint32_t maxCmds )
{
    MqttCommand_t *pCmd  = NULL;
    uint32_t       count = 0U;

    while( ( count < maxCmds )
           && ( xQueueReceive( s_cmdQueue,
                               &pCmd,
                               0U ) == pdTRUE ) )
    {
        if( pCmd == NULL ) { continue; }

        MQTTStatus_t   mqttRet = MQTTSuccess;
        MqttAgentStatus_t agRet = MQTT_AGENT_SUCCESS;

        switch( pCmd->type )
        {
            case CMD_PUBLISH:
                mqttRet = do_publish( pCmd );
                agRet   = ( mqttRet == MQTTSuccess )
                              ? MQTT_AGENT_SUCCESS
                              : MQTT_AGENT_ERR_MQTT;
                break;

            case CMD_SUBSCRIBE:
                mqttRet = do_subscribe( pCmd );
                agRet   = ( mqttRet == MQTTSuccess )
                              ? MQTT_AGENT_SUCCESS
                              : MQTT_AGENT_ERR_MQTT;
                break;

            case CMD_UNSUBSCRIBE:
                mqttRet = do_unsubscribe( pCmd );
                agRet   = ( mqttRet == MQTTSuccess )
                              ? MQTT_AGENT_SUCCESS
                              : MQTT_AGENT_ERR_MQTT;
                break;

            case CMD_STOP:
                /* Send a clean MQTT DISCONNECT with a "Normal disconnection"
                 * reason code (0x00).  The broker will not send the Will. */
                {
                    uint8_t                      discPropBuf[ 16 ];
                    MQTTPropBuilder_t            discProps = { .pBuffer = discPropBuf,
                                                               .bufferLength = sizeof( discPropBuf ) };
                    MQTTSuccessFailReasonCode_t  reasonCode = MQTT_REASON_DISCONNECT_NORMAL_DISCONNECTION;
                    MQTT_Disconnect( &s_mqttCtx, &discProps, &reasonCode );
                }
                set_state( MQTT_AGENT_STATE_STOPPED );

                if( pCmd->hCompletionSem != NULL )
                {
                    pCmd->result = MQTT_AGENT_SUCCESS;
                    xSemaphoreGive( pCmd->hCompletionSem );
                }

                pool_free( pCmd );

                /* Terminate the task. */
                s_agentTask = NULL;
                vTaskDelete( NULL );
                return; /* unreachable — satisfies the compiler */

            default:
                break;
        }

        pCmd->result = agRet;

        if( pCmd->hCompletionSem != NULL )
        {
            xSemaphoreGive( pCmd->hCompletionSem );
        }

        pool_free( pCmd );
        count++;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Replay all active subscriptions after a reconnect
 * ───────────────────────────────────────────────────────────────────────────── */

static bool stop_if_requested( void )
{
    MqttCommand_t *pCmd = NULL;

    if( xQueuePeek( s_cmdQueue, &pCmd, 0U ) != pdTRUE )
    {
        return false;
    }

    if( ( pCmd == NULL ) || ( pCmd->type != CMD_STOP ) )
    {
        return false;
    }

    ( void )xQueueReceive( s_cmdQueue, &pCmd, 0U );

    set_state( MQTT_AGENT_STATE_STOPPED );

    if( pCmd->hCompletionSem != NULL )
    {
        pCmd->result = MQTT_AGENT_SUCCESS;
        xSemaphoreGive( pCmd->hCompletionSem );
    }

    pool_free( pCmd );
    s_agentTask = NULL;
    vTaskDelete( NULL );
    return true;
}

/* Retained for reference; the agent no longer self-reconnects (mqtt_task owns
 * transport rebuild and re-subscribes via mqtt_config_callbacks_init). */
__attribute__((unused))
static void resubscribe_all( void )
{
    for( uint32_t i = 0U; i < MQTT_AGENT_MAX_SUBSCRIPTIONS; i++ )
    {
        if( !s_subs[ i ].active ) continue;

        MQTTSubscribeInfo_t si = {
            .qos              = s_subs[ i ].qos,
            .pTopicFilter     = s_subs[ i ].filterBuf,
            .topicFilterLength = s_subs[ i ].filterLen,
        };

        MQTTStatus_t ret = MQTT_Subscribe( &s_mqttCtx,
                                            &si,
                                            1U,
                                            MQTT_GetPacketId( &s_mqttCtx ),
                                            NULL );

        if( ret != MQTTSuccess )
        {
            MQTT_AGENT_LOG( "Resubscribe failed for '%s': %d",
                            s_subs[ i ].filterBuf, ret );
        }
        else
        {
            /* Drain the SUBACK immediately. */
            MQTT_ProcessLoop( &s_mqttCtx );
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Inbound PUBLISH dispatcher
 * ───────────────────────────────────────────────────────────────────────────── */

static void dispatch_inbound( const MQTTPublishInfo_t  *pPub,
                               const MQTTPropBuilder_t  *pProps )
{
    LOG_INFO( APP_LAYER_MQTT, "inbound PUBLISH topic=%.*s payload=%u byte(s)%s",
              ( int )pPub->topicNameLength, pPub->pTopicName,
              ( unsigned )pPub->payloadLength,
              pPub->retain ? " [retained]" : "" );

    bool dispatched = false;

    for( uint32_t i = 0U; i < MQTT_AGENT_MAX_SUBSCRIPTIONS; i++ )
    {
        if( !s_subs[ i ].active ) continue;

        bool matched = false;

        MQTT_MatchTopic( pPub->pTopicName,
                         pPub->topicNameLength,
                         s_subs[ i ].filterBuf,
                         s_subs[ i ].filterLen,
                         &matched );

        if( matched && ( s_subs[ i ].pfCallback != NULL ) )
        {
            dispatched = true;
            s_subs[ i ].pfCallback( pPub->pTopicName,
                                     pPub->topicNameLength,
                                     pPub->pPayload,
                                     pPub->payloadLength,
                                     pProps,
                                     s_subs[ i ].pUserCtx );
        }
    }

    if( !dispatched )
    {
        LOG_WARN( APP_LAYER_MQTT, "inbound PUBLISH matched no subscription" );
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * coreMQTT event callback
 * ───────────────────────────────────────────────────────────────────────────── */

static bool mqtt_event_callback( MQTTContext_t               *pCtx,
                                  MQTTPacketInfo_t           *pPacket,
                                  MQTTDeserializedInfo_t     *pInfo,
                                  MQTTSuccessFailReasonCode_t *pReasonCode,
                                  MQTTPropBuilder_t          *pSendProps,
                                  MQTTPropBuilder_t          *pGetProps )
{
    ( void )pCtx;
    ( void )pReasonCode;
    ( void )pSendProps;
    ( void )pGetProps;

    if( ( pPacket->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
    {
        if( ( pInfo != NULL ) && ( pInfo->pPublishInfo != NULL ) )
        {
            /* The property builder pointer lives in pCtx->connectionProperties
             * for CONNACK, and is passed through pGetProps for PUBLISH in v5. */
            dispatch_inbound( pInfo->pPublishInfo,
                              pGetProps );
        }
    }

    /* Return true to allow coreMQTT to continue processing the packet. */
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Agent task
 * ───────────────────────────────────────────────────────────────────────────── */

static void agent_task( void *pv )
{
    ( void )pv;

    set_state( MQTT_AGENT_STATE_CONNECTING );

    /* ── Initial connect (with back-off retry) ──────────────────────────── */
    {
        MQTTStatus_t ret;

        for( ;; )
        {
            if( stop_if_requested() ) { return; }

            ret = do_connect();

            if( ret == MQTTSuccess )
            {
                s_backoffMs  = MQTT_AGENT_RECONNECT_DELAY_BASE_MS;
                s_retryCount = 0U;
                set_state( MQTT_AGENT_STATE_CONNECTED );
                break;
            }

            s_retryCount++;

            /* The TCP link (over the ESP-01) is owned by mqtt_task. Retrying
             * MQTT_Connect on the same flaky/half-open socket rarely recovers,
             * so after a few attempts stop and let mqtt_task tear down the
             * ESP-01 connection and start a fresh agent over a new socket. */
            if( s_retryCount >= 4U )
            {
                MQTT_AGENT_LOG( "Initial connect failed %lu times — stopping for transport rebuild",
                                ( unsigned long )s_retryCount );
                set_state( MQTT_AGENT_STATE_STOPPED );
                s_agentTask = NULL;
                vTaskDelete( NULL );
                return;
            }

            MQTT_AGENT_LOG( "Initial connect failed. Retry in %lu ms.", s_backoffMs );
            vTaskDelay( pdMS_TO_TICKS( s_backoffMs ) );
            if( stop_if_requested() ) { return; }

            /* Exponential back-off with ceiling. */
            s_backoffMs = ( s_backoffMs * 2U < MQTT_AGENT_RECONNECT_DELAY_MAX_MS )
                              ? s_backoffMs * 2U
                              : MQTT_AGENT_RECONNECT_DELAY_MAX_MS;
        }
    }

    /* ── Main loop ──────────────────────────────────────────────────────── */
    for( ;; )
    {
        /* 1. Drain the command queue. */
        drain_command_queue( MQTT_AGENT_CMDS_PER_LOOP );

        /* 2. Drive coreMQTT: keep-alive, acks, inbound delivery. */
        MQTTStatus_t loopRet = MQTT_ProcessLoop( &s_mqttCtx );

        /* 3. On any error, hand back to the transport owner (mqtt_task).
         *
         * coreMQTT cannot reconnect by itself here: the "socket" is a TCP
         * connection over the ESP-01 that only mqtt_task can rebuild. Once the
         * link drops (or a send/keep-alive fails), calling MQTT_Connect on the
         * dead context just returns MQTTStatusConnected / DisconnectPending and
         * spins forever. So stop the agent; mqtt_task observes
         * MQTT_AGENT_STATE_STOPPED, tears down the ESP-01 TCP link, and starts a
         * fresh agent over a new connection (MqttAgent_Init re-subscribes via
         * mqtt_config_callbacks_init). */
        if( ( loopRet != MQTTSuccess ) &&
            ( loopRet != MQTTNeedMoreBytes ) )
        {
            MQTT_AGENT_LOG( "ProcessLoop error %d — stopping for transport rebuild", loopRet );
            set_state( MQTT_AGENT_STATE_STOPPED );
            s_agentTask = NULL;
            vTaskDelete( NULL );
            return; /* unreachable — satisfies the compiler */
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────── */

MqttAgentStatus_t MqttAgent_Init( const MqttAgentConfig_t *pConfig )
{
    if( ( pConfig == NULL ) || ( pConfig->pTransport == NULL ) )
    {
        return MQTT_AGENT_ERR_PARAM;
    }

    /* Copy config so callers can release theirs after Init. */
    s_cfg = *pConfig;

    /* ── FreeRTOS primitives ────────────────────────────────────────────── */
    s_poolMutex = xSemaphoreCreateMutexStatic( &s_poolMutexBuf );

    s_cmdQueue = xQueueCreateStatic( MQTT_AGENT_COMMAND_QUEUE_LEN,
                                      sizeof( MqttCommand_t * ),
                                      s_queueStorage,
                                      &s_queueBuf );

    s_connectedSem = xSemaphoreCreateBinaryStatic( &s_connectedSemBuf );

    /* ── coreMQTT context ───────────────────────────────────────────────── */
    MQTTFixedBuffer_t fixedBuf = { .pBuffer = s_netBuf,
                                   .size    = MQTT_AGENT_NETWORK_BUF_SIZE };

    MQTTStatus_t ret = MQTT_Init( &s_mqttCtx,
                                   pConfig->pTransport,
                                   get_time_ms,
                                   mqtt_event_callback,
                                   &fixedBuf );

    if( ret != MQTTSuccess )
    {
        MQTT_AGENT_LOG( "MQTT_Init failed: %d", ret );
        return MQTT_AGENT_ERR_MQTT;
    }

    /* ── QoS state engine (required for QoS 1 and QoS 2) ───────────────── */
    ret = MQTT_InitStatefulQoS( &s_mqttCtx,
                                  s_outRecords,
                                  MQTT_AGENT_OUTGOING_PUBLISH_MAX,
                                  s_inRecords,
                                  MQTT_AGENT_INCOMING_PUBLISH_MAX,
                                  s_ackPropsBuf,
                                  sizeof( s_ackPropsBuf ) );

    if( ret != MQTTSuccess )
    {
        MQTT_AGENT_LOG( "MQTT_InitStatefulQoS failed: %d", ret );
        return MQTT_AGENT_ERR_MQTT;
    }

    /* ── MQTT 5 connection properties object ────────────────────────────── */
    MQTTConnectionProperties_t *pConnProp = &s_mqttCtx.connectionProperties;
    ret = MQTT_InitConnect( pConnProp );

    if( ret != MQTTSuccess )
    {
        MQTT_AGENT_LOG( "MQTT_InitConnect failed: %d", ret );
        return MQTT_AGENT_ERR_MQTT;
    }

    /* Clear subscription registry. */
    memset( s_subs,    0, sizeof( s_subs    ) );
    memset( s_cmdPool, 0, sizeof( s_cmdPool ) );

    set_state( MQTT_AGENT_STATE_DISCONNECTED );

    MQTT_AGENT_LOG( "Initialised (coreMQTT %s)", MQTT_LIBRARY_VERSION );
    return MQTT_AGENT_SUCCESS;
}

MqttAgentStatus_t MqttAgent_Start( void )
{
    if( s_state == MQTT_AGENT_STATE_UNINIT )
    {
        return MQTT_AGENT_ERR_STATE;
    }

    if( s_agentTask != NULL )
    {
        return MQTT_AGENT_ERR_STATE;   /* already running */
    }

    s_agentTask = xTaskCreateStatic( agent_task,
                                      "MqttAgent",
                                      MQTT_AGENT_TASK_STACK_DEPTH,
                                      NULL,
                                      MQTT_AGENT_TASK_PRIORITY,
                                      s_agentStack,
                                      &s_agentTCB );

    return ( s_agentTask != NULL ) ? MQTT_AGENT_SUCCESS : MQTT_AGENT_ERR_STATE;
}

MqttAgentStatus_t MqttAgent_Stop( uint32_t timeoutMs )
{
    /* If the agent already stopped itself (e.g. after a transport error handed
     * control back to mqtt_task), there is no task to signal — report success
     * so the caller proceeds straight to rebuilding the connection. */
    if( ( s_agentTask == NULL ) || ( s_state == MQTT_AGENT_STATE_STOPPED ) )
    {
        return MQTT_AGENT_SUCCESS;
    }

    MqttCommand_t *pCmd = pool_alloc();
    if( pCmd == NULL ) return MQTT_AGENT_ERR_QUEUE;

    StaticSemaphore_t semBuf;
    SemaphoreHandle_t hDone = xSemaphoreCreateBinaryStatic( &semBuf );

    pCmd->type           = CMD_STOP;
    pCmd->hCompletionSem = hDone;

    if( xQueueSend( s_cmdQueue, &pCmd, pdMS_TO_TICKS( timeoutMs ) ) != pdTRUE )
    {
        pool_free( pCmd );
        return MQTT_AGENT_ERR_QUEUE;
    }

    TickType_t ticks = ( timeoutMs == portMAX_DELAY )
                           ? portMAX_DELAY
                           : pdMS_TO_TICKS( timeoutMs );

    MqttAgentStatus_t result =
        ( xSemaphoreTake( hDone, ticks ) == pdTRUE )
            ? MQTT_AGENT_SUCCESS
            : MQTT_AGENT_ERR_TIMEOUT;

    vSemaphoreDelete( hDone );
    return result;
}

MqttAgentState_t MqttAgent_GetState( void )
{
    return s_state;
}

MqttAgentStatus_t MqttAgent_WaitConnected( uint32_t timeoutMs )
{
    if( s_state == MQTT_AGENT_STATE_CONNECTED )
    {
        return MQTT_AGENT_SUCCESS;
    }

    TickType_t ticks = ( timeoutMs == portMAX_DELAY )
                           ? portMAX_DELAY
                           : pdMS_TO_TICKS( timeoutMs );

    return ( xSemaphoreTake( s_connectedSem, ticks ) == pdTRUE )
               ? MQTT_AGENT_SUCCESS
               : MQTT_AGENT_ERR_TIMEOUT;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Publish
 * ───────────────────────────────────────────────────────────────────────────── */

MqttAgentStatus_t MqttAgent_Publish( const char              *pTopicName,
                                      uint16_t                 topicLen,
                                      const void              *pPayload,
                                      size_t                   payloadLen,
                                      MQTTQoS_t                qos,
                                      bool                     retain,
                                      const MqttPublishProps_t *pProps,
                                      uint32_t                 timeoutMs )
{
    if( ( pTopicName == NULL ) || ( topicLen == 0U ) )
    {
        return MQTT_AGENT_ERR_PARAM;
    }

    if( topicLen >= MQTT_AGENT_MAX_TOPIC_FILTER_LEN )
    {
        return MQTT_AGENT_ERR_PARAM;
    }

    MqttCommand_t *pCmd = pool_alloc();
    if( pCmd == NULL ) return MQTT_AGENT_ERR_QUEUE;

    pCmd->type = CMD_PUBLISH;

    /* Copy topic into the command's inline buffer. */
    memcpy( pCmd->u.publish.topicBuf, pTopicName, topicLen );
    pCmd->u.publish.topicBuf[ topicLen ] = '\0';

    /* Copy small payloads inline so producer stack buffers are safe to reuse. */
    pCmd->u.publish.payloadCopied = false;

    if( ( pPayload != NULL )
        && ( payloadLen <= sizeof( pCmd->u.publish.payloadBuf ) ) )
    {
        memcpy( pCmd->u.publish.payloadBuf, pPayload, payloadLen );
        pCmd->u.publish.info.pPayload    = pCmd->u.publish.payloadBuf;
        pCmd->u.publish.payloadCopied    = true;
    }
    else
    {
        pCmd->u.publish.info.pPayload = pPayload;
    }

    pCmd->u.publish.info.pTopicName       = pCmd->u.publish.topicBuf;
    pCmd->u.publish.info.topicNameLength  = topicLen;
    pCmd->u.publish.info.payloadLength    = payloadLen;
    pCmd->u.publish.info.qos             = qos;
    pCmd->u.publish.info.retain          = retain;
    pCmd->u.publish.info.dup             = false;

    /* Build MQTT 5 publish properties. */
    if( pProps != NULL )
    {
        MQTTStatus_t ret = build_publish_props( &pCmd->u.publish.propBuilder,
                                                 pCmd->u.publish.propBuf,
                                                 MQTT_AGENT_PROP_BUF_SIZE,
                                                 pProps );
        pCmd->u.publish.hasProps = ( ret == MQTTSuccess );
    }

    /* Fire-and-forget: no completion semaphore for publish. */
    pCmd->hCompletionSem = NULL;

    TickType_t ticks = ( timeoutMs == portMAX_DELAY )
                           ? portMAX_DELAY
                           : pdMS_TO_TICKS( timeoutMs );

    if( xQueueSend( s_cmdQueue, &pCmd, ticks ) != pdTRUE )
    {
        pool_free( pCmd );
        return MQTT_AGENT_ERR_QUEUE;
    }

    return MQTT_AGENT_SUCCESS;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Subscribe
 * ───────────────────────────────────────────────────────────────────────────── */

MqttAgentStatus_t MqttAgent_Subscribe( const char         *pFilter,
                                        MQTTQoS_t           qos,
                                        MqttTopicCallback_t pfCallback,
                                        void               *pUserCtx,
                                        uint32_t            timeoutMs )
{
    if( ( pFilter == NULL ) || ( pfCallback == NULL ) )
    {
        return MQTT_AGENT_ERR_PARAM;
    }

    uint16_t filterLen = ( uint16_t )strnlen( pFilter,
                                               MQTT_AGENT_MAX_TOPIC_FILTER_LEN );

    if( filterLen >= MQTT_AGENT_MAX_TOPIC_FILTER_LEN )
    {
        return MQTT_AGENT_ERR_PARAM;
    }

    /* Check there is a free slot before allocating a command. */
    bool slotFound = false;
    for( uint32_t i = 0U; i < MQTT_AGENT_MAX_SUBSCRIPTIONS; i++ )
    {
        if( !s_subs[ i ].active ) { slotFound = true; break; }
    }
    if( !slotFound ) return MQTT_AGENT_ERR_NO_SLOT;

    MqttCommand_t *pCmd = pool_alloc();
    if( pCmd == NULL ) return MQTT_AGENT_ERR_QUEUE;

    pCmd->type = CMD_SUBSCRIBE;

    memcpy( pCmd->u.subscribe.filterBuf, pFilter, filterLen );
    pCmd->u.subscribe.filterBuf[ filterLen ] = '\0';

    pCmd->u.subscribe.info.pTopicFilter      = pCmd->u.subscribe.filterBuf;
    pCmd->u.subscribe.info.topicFilterLength = filterLen;
    pCmd->u.subscribe.info.qos              = qos;
    pCmd->u.subscribe.pfCallback            = pfCallback;
    pCmd->u.subscribe.pUserCtx              = pUserCtx;
    pCmd->hCompletionSem                    = NULL;

    TickType_t ticks = ( timeoutMs == portMAX_DELAY )
                           ? portMAX_DELAY
                           : pdMS_TO_TICKS( timeoutMs );

    if( xQueueSend( s_cmdQueue, &pCmd, ticks ) != pdTRUE )
    {
        pool_free( pCmd );
        return MQTT_AGENT_ERR_QUEUE;
    }

    return MQTT_AGENT_SUCCESS;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Unsubscribe
 * ───────────────────────────────────────────────────────────────────────────── */

MqttAgentStatus_t MqttAgent_Unsubscribe( const char *pFilter,
                                          uint32_t    timeoutMs )
{
    if( pFilter == NULL ) return MQTT_AGENT_ERR_PARAM;

    uint16_t filterLen = ( uint16_t )strnlen( pFilter,
                                               MQTT_AGENT_MAX_TOPIC_FILTER_LEN );

    /* Verify the filter is actually registered. */
    bool found = false;
    for( uint32_t i = 0U; i < MQTT_AGENT_MAX_SUBSCRIPTIONS; i++ )
    {
        if( s_subs[ i ].active &&
            ( strncmp( s_subs[ i ].filterBuf, pFilter,
                       MQTT_AGENT_MAX_TOPIC_FILTER_LEN ) == 0 ) )
        {
            found = true;
            break;
        }
    }
    if( !found ) return MQTT_AGENT_ERR_NOT_FOUND;

    MqttCommand_t *pCmd = pool_alloc();
    if( pCmd == NULL ) return MQTT_AGENT_ERR_QUEUE;

    pCmd->type = CMD_UNSUBSCRIBE;

    memcpy( pCmd->u.unsubscribe.filterBuf, pFilter, filterLen );
    pCmd->u.unsubscribe.filterBuf[ filterLen ] = '\0';

    pCmd->u.unsubscribe.info.pTopicFilter      = pCmd->u.unsubscribe.filterBuf;
    pCmd->u.unsubscribe.info.topicFilterLength = filterLen;
    pCmd->hCompletionSem                       = NULL;

    TickType_t ticks = ( timeoutMs == portMAX_DELAY )
                           ? portMAX_DELAY
                           : pdMS_TO_TICKS( timeoutMs );

    if( xQueueSend( s_cmdQueue, &pCmd, ticks ) != pdTRUE )
    {
        pool_free( pCmd );
        return MQTT_AGENT_ERR_QUEUE;
    }

    return MQTT_AGENT_SUCCESS;
}
