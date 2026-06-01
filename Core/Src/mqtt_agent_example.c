/**
 * @file mqtt_agent_example.c
 * @brief Usage examples for the coreMQTT v5 FreeRTOS agent.
 *
 * Shows:
 *  1. Initialisation and start-up
 *  2. Simple QoS 0 publish
 *  3. MQTT 5 publish with properties (expiry, content-type, user props)
 *  4. Request-response with Response Topic + Correlation Data
 *  5. Subscribe with wildcard filter
 *  6. Multi-task producer pattern
 *  7. Clean shutdown
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "mqtt_agent.h"
#include "esp01_transport.h"   /* or any other TransportInterface_t provider */

/* ─────────────────────────────────────────────────────────────────────────────
 * Transport (shared with mqtt_app_example.c if both files are in the project)
 * ───────────────────────────────────────────────────────────────────────────── */

extern UART_HandleTypeDef       ESP01_UART_HANDLE;

static NetworkContext_t         s_netCtx;
static TransportInterface_t     s_transport;

/* ─────────────────────────────────────────────────────────────────────────────
 * Topic definitions
 * ───────────────────────────────────────────────────────────────────────────── */

#define TOPIC_TELEMETRY         "devices/stm32/telemetry"
#define TOPIC_COMMANDS          "devices/stm32/commands"
#define TOPIC_COMMANDS_WILDCARD "devices/stm32/commands/#"
#define TOPIC_RESPONSE_BASE     "devices/stm32/responses/"
#define TOPIC_FIRMWARE_ALL      "devices/+/firmware"

/* ─────────────────────────────────────────────────────────────────────────────
 * 1. Initialisation helper
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief One-time setup called from main() (before the scheduler starts).
 *
 * We use the ESP01 transport from esp01_transport.h.  Swap in your own
 * TransportInterface_t if you have a different physical layer.
 */
void App_MqttInit( void )
{
    /* ── Bring up the transport layer ───────────────────────────────────── */
    ESP01_Init( &s_netCtx, &ESP01_UART_HANDLE );
    ESP01_WifiConnect( "MySSID", "MyPassword" );
    ESP01_Connect( &s_netCtx, "broker.example.com", 1883 );
    ESP01_FillTransportInterface( &s_transport, &s_netCtx );

    /* ── MQTT 5 CONNECT properties ──────────────────────────────────────── */
    static const MQTTUserProperty_t connectUserProps[] = {
        { .pKey = "firmware", .keyLength = 8,
          .pValue = "v2.1.0",  .valueLength = 6 },
        { .pKey = "board",    .keyLength = 5,
          .pValue = "nucleo",  .valueLength = 6 },
    };

    static const MqttConnectProps_t connectProps = {
        .sessionExpirySec = 300,        /* resume session for up to 5 min     */
        .receiveMaximum   = 4,
        .maxPacketSize    = 1024,
        .will = {
            .pTopic               = "devices/stm32/status",
            .topicLen             = sizeof("devices/stm32/status") - 1,
            .pPayload             = "offline",
            .payloadLen           = 7,
            .qos                  = MQTTQoS1,
            .retain               = true,
            .willDelayIntervalSec = 5,  /* broker delays Will by 5 s          */
        },
        .pUserProps     = connectUserProps,
        .userPropsCount = 2,
    };

    /* ── Agent configuration ────────────────────────────────────────────── */
    MqttAgentConfig_t cfg = {
        .pTransport       = &s_transport,
        .pClientId        = "stm32-nucleo-01",
        .clientIdLen      = sizeof("stm32-nucleo-01") - 1,
        .pUserName        = NULL,
        .pPassword        = NULL,
        .keepAliveSec     = 60,
        .cleanStart       = false,          /* resume session if broker has one */
        .pConnectProps    = &connectProps,
        .pfStateChangeCb  = NULL,
        .pStateChangeCbCtx = NULL,
    };

    MqttAgentStatus_t ret = MqttAgent_Init( &cfg );
    configASSERT( ret == MQTT_AGENT_SUCCESS );

    ret = MqttAgent_Start();
    configASSERT( ret == MQTT_AGENT_SUCCESS );
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 2. Simple QoS 0 publish (fire-and-forget, no MQTT 5 properties)
 * ───────────────────────────────────────────────────────────────────────────── */

void Example_SimplePublish( void )
{
    const char *payload = "{\"temp\":23.5,\"hum\":61}";

    MqttAgent_Publish( TOPIC_TELEMETRY,
                       sizeof(TOPIC_TELEMETRY) - 1,
                       payload,
                       strlen( payload ),
                       MQTTQoS0,
                       false,      /* retain = false                          */
                       NULL,       /* no MQTT 5 properties                    */
                       100 );      /* wait up to 100 ms for queue slot        */
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 3. MQTT 5 publish with rich properties
 * ───────────────────────────────────────────────────────────────────────────── */

void Example_RichPublish( void )
{
    const char *payload = "{\"alert\":\"high_temp\",\"value\":85.2}";

    /* User properties that travel with the message to the broker / subscribers. */
    static const MQTTUserProperty_t pubUserProps[] = {
        { .pKey = "sensor-id", .keyLength = 9, .pValue = "T01", .valueLength = 3 },
        { .pKey = "unit",      .keyLength = 4, .pValue = "C",   .valueLength = 1 },
    };

    MqttPublishProps_t props = {
        .messageExpiryIntervalSec = 60,        /* broker drops if undelivered after 60 s */
        .payloadFormatIndicator   = 1,         /* 1 = UTF-8 JSON                          */
        .pContentType             = "application/json",
        .contentTypeLen           = sizeof("application/json") - 1,
        .pUserProps               = pubUserProps,
        .userPropsCount           = 2,
    };

    MqttAgent_Publish( TOPIC_TELEMETRY,
                       sizeof(TOPIC_TELEMETRY) - 1,
                       payload,
                       strlen( payload ),
                       MQTTQoS1,    /* QoS 1: guaranteed delivery            */
                       false,
                       &props,
                       portMAX_DELAY );
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 4. Request-response pattern using Response Topic + Correlation Data
 *    (MQTT 5 §4.10)
 * ───────────────────────────────────────────────────────────────────────────── */

static SemaphoreHandle_t s_responseSem;
static char              s_responsePayload[ 128 ];
static size_t            s_responseLen;

static void on_response( const char *pTopic, uint16_t topicLen,
                         const void *pPayload, size_t payloadLen,
                         const MQTTPropBuilder_t *pProps, void *pCtx )
{
    ( void )pTopic; ( void )topicLen; ( void )pProps; ( void )pCtx;

    /* Copy response payload for the requesting task. */
    size_t copy = ( payloadLen < sizeof(s_responsePayload) - 1 )
                      ? payloadLen
                      : sizeof(s_responsePayload) - 1;
    memcpy( s_responsePayload, pPayload, copy );
    s_responseLen = copy;

    xSemaphoreGive( s_responseSem );   /* wake the requesting task */
}

void Example_RequestResponse( void )
{
    static StaticSemaphore_t semBuf;
    s_responseSem = xSemaphoreCreateBinaryStatic( &semBuf );

    /* Subscribe to our personal response topic first. */
    MqttAgent_Subscribe( TOPIC_RESPONSE_BASE "stm32-nucleo-01",
                          MQTTQoS0,
                          on_response,
                          NULL,
                          portMAX_DELAY );

    /* Correlation data — any opaque token that ties request to reply. */
    static const uint8_t correlId[] = { 0xDE, 0xAD, 0xBE, 0xEF };

    MqttPublishProps_t props = {
        .pResponseTopic     = TOPIC_RESPONSE_BASE "stm32-nucleo-01",
        .responseTopicLen   = sizeof(TOPIC_RESPONSE_BASE "stm32-nucleo-01") - 1,
        .pCorrelationData   = correlId,
        .correlationDataLen = sizeof(correlId),
    };

    const char *request = "{\"cmd\":\"get_config\"}";

    MqttAgent_Publish( TOPIC_COMMANDS,
                       sizeof(TOPIC_COMMANDS) - 1,
                       request,
                       strlen( request ),
                       MQTTQoS1,
                       false,
                       &props,
                       portMAX_DELAY );

    /* Block waiting for the response (5-second timeout). */
    if( xSemaphoreTake( s_responseSem, pdMS_TO_TICKS(5000) ) == pdTRUE )
    {
        /* s_responsePayload[ 0..s_responseLen ] holds the reply. */
        MQTT_AGENT_LOG( "Response received: %.*s",
                        (int)s_responseLen, s_responsePayload );
    }
    else
    {
        MQTT_AGENT_LOG( "Request timed out." );
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 5. Subscribe with wildcard filter + reading inbound MQTT 5 properties
 * ───────────────────────────────────────────────────────────────────────────── */

static void on_command( const char *pTopic, uint16_t topicLen,
                         const void *pPayload, size_t payloadLen,
                         const MQTTPropBuilder_t *pProps, void *pCtx )
{
    ( void )pCtx;
    MQTT_AGENT_LOG( "CMD on '%.*s': %.*s",
                    topicLen,    pTopic,
                    (int)payloadLen, (const char *)pPayload );

    /* Read MQTT 5 inbound properties if present. */
    if( pProps != NULL )
    {
        /* Content-Type. */
        const char *pCT = NULL;
        uint16_t    ctLen = 0;
        if( MQTTPropGet_ContentType( pProps, 0, &pCT, &ctLen ) == MQTTSuccess )
        {
            MQTT_AGENT_LOG( "  content-type: %.*s", ctLen, pCT );
        }

        /* Correlation Data (for request-response replies). */
        const void *pCD = NULL;
        uint16_t    cdLen = 0;
        if( MQTTPropGet_CorrelationData( pProps, 0, &pCD, &cdLen ) == MQTTSuccess )
        {
            MQTT_AGENT_LOG( "  correlation-data: %u bytes", cdLen );
        }

        /* User Properties — iterate until MQTTEndOfProperties. */
        MQTTUserProperty_t up;
        uint16_t           idx = 0;
        while( MQTTPropGet_UserProp( pProps, &idx, &up ) == MQTTSuccess )
        {
            MQTT_AGENT_LOG( "  user-prop: %.*s = %.*s",
                            up.keyLength,   up.pKey,
                            up.valueLength, up.pValue );
        }
    }
}

void Example_WildcardSubscribe( void )
{
    /* "#" matches the topic and all sub-levels:
     *   devices/stm32/commands
     *   devices/stm32/commands/ota
     *   devices/stm32/commands/reboot
     */
    MqttAgent_Subscribe( TOPIC_COMMANDS_WILDCARD,
                          MQTTQoS1,
                          on_command,
                          NULL,
                          portMAX_DELAY );

    /* Cross-device firmware topic using single-level wildcard '+'. */
    MqttAgent_Subscribe( TOPIC_FIRMWARE_ALL,
                          MQTTQoS1,
                          on_command,
                          NULL,
                          portMAX_DELAY );
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 6. Multi-task producer: sensor task + actuator task sharing the agent
 * ───────────────────────────────────────────────────────────────────────────── */

static void sensor_task( void *pv )
{
    ( void )pv;

    /* Wait until the MQTT session is live before publishing. */
    MqttAgent_WaitConnected( portMAX_DELAY );

    for( ;; )
    {
        char buf[64];
        snprintf( buf, sizeof(buf), "{\"temp\":%.1f}", 25.0f );

        MqttAgent_Publish( "sensors/temperature",
                           sizeof("sensors/temperature") - 1,
                           buf, strlen(buf),
                           MQTTQoS0, false, NULL, 0 );

        vTaskDelay( pdMS_TO_TICKS(10000) );   /* publish every 10 s */
    }
}

static void actuator_task( void *pv )
{
    ( void )pv;
    MqttAgent_WaitConnected( portMAX_DELAY );

    for( ;; )
    {
        MqttAgent_Publish( "actuators/pump/state",
                           sizeof("actuators/pump/state") - 1,
                           "ON", 2,
                           MQTTQoS1, true /* retain */, NULL, 0 );

        vTaskDelay( pdMS_TO_TICKS(30000) );
    }
}

void Example_MultiTaskProducers( void )
{
    static StackType_t  sensorStack[ 256 ], actuatorStack[ 256 ];
    static StaticTask_t sensorTCB,          actuatorTCB;

    xTaskCreateStatic( sensor_task,   "Sensor",
                       256, NULL, tskIDLE_PRIORITY + 1,
                       sensorStack,   &sensorTCB );

    xTaskCreateStatic( actuator_task, "Actuator",
                       256, NULL, tskIDLE_PRIORITY + 1,
                       actuatorStack, &actuatorTCB );
}

/* ─────────────────────────────────────────────────────────────────────────────
 * 7. Graceful shutdown
 * ───────────────────────────────────────────────────────────────────────────── */

void Example_Shutdown( void )
{
    /* Send a retained "online = false" message before disconnecting. */
    const char *offlinePayload = "{\"online\":false}";
    MqttAgent_Publish( "devices/stm32/status",
                       sizeof("devices/stm32/status") - 1,
                       offlinePayload, strlen(offlinePayload),
                       MQTTQoS1, true, NULL, portMAX_DELAY );

    /* Stop the agent — sends MQTT DISCONNECT, waits up to 3 s. */
    MqttAgent_Stop( 3000 );

    /* Close the underlying TCP link. */
    ESP01_Disconnect( &s_netCtx );
}

/* ─────────────────────────────────────────────────────────────────────────────
 * FreeRTOS hook (forward UART events to the ESP01 driver)
 * ───────────────────────────────────────────────────────────────────────────── */

void HAL_UARTEx_RxEventCallback( UART_HandleTypeDef *huart, uint16_t Size )
{
    ESP01_UART_RxEventCallback( huart, Size );
}
