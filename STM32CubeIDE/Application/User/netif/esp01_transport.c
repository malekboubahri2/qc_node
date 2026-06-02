/**
 * @file esp01_transport.c
 * @brief coreMQTT transport interface — STM32 + ESP01 via AT commands (UART).
 *
 * See esp01_transport.h for the full architecture description.
 */

#include "esp01_transport.h"
#include "app_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>     /* strtol */
#include <limits.h>

/* Route the driver's logs to the unified logger (layer = net). Severity is
 * gated at run time per-layer via app_log_set_level(APP_LAYER_NET, ...). */
#define ESP01_LOG_ERROR( ... ) app_log_emit( APP_LAYER_NET, APP_LOG_ERROR, __VA_ARGS__ )
#define ESP01_LOG_INFO( ... )  app_log_emit( APP_LAYER_NET, APP_LOG_INFO,  __VA_ARGS__ )
#define ESP01_LOG_DEBUG( ... ) app_log_emit( APP_LAYER_NET, APP_LOG_DEBUG, __VA_ARGS__ )
#define ESP01_LOG_TRACE( ... ) app_log_emit( APP_LAYER_NET, APP_LOG_TRACE, __VA_ARGS__ )

#define ESP01_SCAN_LINE_MAX_LEN 192U

/* ─────────────────────────────────────────────────────────────────────────────
 * Compile-time assertions
 * ───────────────────────────────────────────────────────────────────────────── */

/* Ring sizes must be powers of two so the mask trick (& (SIZE-1)) is safe. */
_Static_assert( ( ESP01_RX_DMA_BUF_SIZE & ( ESP01_RX_DMA_BUF_SIZE - 1U ) ) == 0U,
                "ESP01_RX_DMA_BUF_SIZE must be a power of two" );
_Static_assert( ( ESP01_RX_RING_SIZE & ( ESP01_RX_RING_SIZE - 1U ) ) == 0U,
                "ESP01_RX_RING_SIZE must be a power of two" );

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal state
 * ───────────────────────────────────────────────────────────────────────────── */

/* ReceiveToIdle DMA buffer: written by DMA hardware, read by the ISR. */
static uint8_t dmaRxBuf[ ESP01_RX_DMA_BUF_SIZE ] __attribute__( ( aligned( 32 ) ) );

/* MQTT packets are coalesced here before one AT+CIPSEND transaction. */
static uint8_t txCoalesceBuf[ ESP01_TX_COALESCE_BUF_SIZE ];

/* ── Software ring-buffer ───────────────────────────────────────────────────── */
typedef struct
{
    uint8_t          buf[ ESP01_RX_RING_SIZE ];
    volatile uint16_t head;   /* write index (ISR)          */
    volatile uint16_t tail;   /* read  index (main context) */
} RingBuffer_t;

static RingBuffer_t esp01RxRing;

/* Set by esp01_rx_flush(); makes esp01_recv drop any half-parsed +IPD framing
 * carried over from a previous TCP connection. */
static volatile bool esp01RecvResetReq = false;

/* ── Active NetworkContext (set in ESP01_Init, used by the ISR) ─────────────── */
static NetworkContext_t *pActiveCtx = NULL;

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal helper prototypes
 * ───────────────────────────────────────────────────────────────────────────── */

static void     ring_push  ( RingBuffer_t *r, uint8_t byte );
static bool     ring_pop   ( RingBuffer_t *r, uint8_t *pByte );
static uint16_t ring_available( const RingBuffer_t *r );

static ESP01_Status_t at_send_cmd  ( UART_HandleTypeDef *pUart,
                                     const char *pCmd );
static ESP01_Status_t at_send_control_cmd( UART_HandleTypeDef *pUart,
                                           const char *pCmd );
static void at_drain_rx_until_quiet( const char *pReason,
                                     uint32_t    quietMs,
                                     uint32_t    timeoutMs );
static ESP01_Status_t at_wait_token( const char *pToken,
                                     uint32_t    timeoutMs );
static ESP01_Status_t at_wait_result( const char * const *ppSuccessTokens,
                                      uint32_t            successTokenCount,
                                      const char * const *ppFailureTokens,
                                      uint32_t            failureTokenCount,
                                      uint32_t            timeoutMs );
static ESP01_Status_t at_wait_network_scan( const char *pTargetSsid,
                                            bool       *pTargetSeen,
                                            uint32_t    timeoutMs );
static ESP01_Status_t at_wait_wifi_join( uint32_t timeoutMs );
static ESP01_Status_t at_wait_wifi_query( bool    *pConnected,
                                          uint32_t timeoutMs );
static ESP01_Status_t at_cmd_ok    ( UART_HandleTypeDef *pUart,
                                     const char *pCmd,
                                     uint32_t    timeoutMs );
static ESP01_Status_t uart_prepare_rx( UART_HandleTypeDef *pUart );
static void log_at_command( const char *pCmd );
static bool scan_line_matches_ssid( const char *pLine,
                                    const char *pSsid );

/* ─────────────────────────────────────────────────────────────────────────────
 * Ring-buffer helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Push one byte into the ring-buffer.
 *
 * Called from ISR context.  If the buffer is full the oldest byte is silently
 * overwritten (tail advances) — this is preferable to dropping new data because
 * "+IPD" headers occupy the front of the stream and losing them corrupts framing.
 */
static inline void ring_push( RingBuffer_t *r, uint8_t byte )
{
    uint16_t nextHead = ( r->head + 1U ) & ( ESP01_RX_RING_SIZE - 1U );

    if( nextHead == r->tail )
    {
        /* Buffer full — drop oldest byte so new bytes can still arrive. */
        r->tail = ( r->tail + 1U ) & ( ESP01_RX_RING_SIZE - 1U );
    }

    r->buf[ r->head ] = byte;
    r->head = nextHead;
}

/**
 * @brief Pop one byte from the ring-buffer.
 *
 * Called from task/main context only.
 *
 * @return true if a byte was available, false if the buffer was empty.
 */
static inline bool ring_pop( RingBuffer_t *r, uint8_t *pByte )
{
    if( r->tail == r->head )
    {
        return false;   /* empty */
    }

    *pByte  = r->buf[ r->tail ];
    r->tail = ( r->tail + 1U ) & ( ESP01_RX_RING_SIZE - 1U );
    return true;
}

/** @brief Return the number of bytes currently held in the ring-buffer. */
static inline uint16_t ring_available( const RingBuffer_t *r )
{
    return ( uint16_t )( ( r->head - r->tail ) & ( ESP01_RX_RING_SIZE - 1U ) );
}

/**
 * @brief Discard all buffered RX bytes and reset the recv framing state.
 *
 * Call when (re)opening a TCP connection so stale bytes from the previous
 * session — e.g. a PINGRESP that arrived just before the link dropped — cannot
 * be misread as the new connection's CONNACK.
 */
static void esp01_rx_flush( void )
{
    __disable_irq();
    esp01RxRing.head = 0U;
    esp01RxRing.tail = 0U;
    __enable_irq();
    esp01RecvResetReq = true;  /* esp01_recv resets its +IPD state machine */
}

static void at_drain_rx_until_quiet( const char *pReason,
                                     uint32_t    quietMs,
                                     uint32_t    timeoutMs )
{
    char line[ ESP01_SCAN_LINE_MAX_LEN ];
    uint16_t lineLen = 0U;
    uint8_t byte;
    uint32_t start = HAL_GetTick();
    uint32_t quietStart = start;

    memset( line, 0, sizeof( line ) );

    while( ( uint32_t )( HAL_GetTick() - start ) < timeoutMs )
    {
        if( !ring_pop( &esp01RxRing, &byte ) )
        {
            if( ( uint32_t )( HAL_GetTick() - quietStart ) >= quietMs )
            {
                break;
            }

            HAL_Delay( 1U );
            continue;
        }

        quietStart = HAL_GetTick();

        if( byte == '\r' )
        {
            continue;
        }

        if( byte != '\n' )
        {
            if( lineLen < ( uint16_t )( sizeof( line ) - 1U ) )
            {
                line[ lineLen++ ] = ( char )byte;
                line[ lineLen ] = '\0';
            }
            continue;
        }

        if( lineLen > 0U )
        {
            ESP01_LOG_DEBUG( "drained stale RX %s: %s\n",
                             ( pReason != NULL ) ? pReason : "",
                             line );
        }

        lineLen = 0U;
        line[ 0 ] = '\0';
    }

    if( lineLen > 0U )
    {
        ESP01_LOG_DEBUG( "drained stale RX %s: %s\n",
                         ( pReason != NULL ) ? pReason : "",
                         line );
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * AT command helpers
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Send a raw string over the UART (CR/LF appended automatically).
 *
 * @param pUart  HAL UART handle.
 * @param pCmd   NUL-terminated command string WITHOUT trailing CR/LF.
 */
static ESP01_Status_t at_send_cmd( UART_HandleTypeDef *pUart, const char *pCmd )
{
    size_t cmdLen;
    HAL_StatusTypeDef status;
    static const uint8_t crlf[] = "\r\n";

    log_at_command( pCmd );

    if( pCmd == NULL )
    {
        return ESP01_ERR_PARAM;
    }

    cmdLen = strlen( pCmd );
    if( cmdLen > UINT16_MAX )
    {
        return ESP01_ERR_PARAM;
    }

    status = HAL_UART_Transmit( pUart,
                                ( const uint8_t * )pCmd,
                                ( uint16_t )cmdLen,
                                ESP01_HAL_TX_TIMEOUT_MS );

    if( status != HAL_OK )
    {
        ESP01_LOG_ERROR( "UART TX failed for AT command (status=%d)\n", ( int )status );
        return ESP01_ERR_TIMEOUT;
    }

    status = HAL_UART_Transmit( pUart,
                                crlf,
                                2U,
                                ESP01_HAL_TX_TIMEOUT_MS );

    if( status != HAL_OK )
    {
        ESP01_LOG_ERROR( "UART TX failed for AT command (status=%d)\n", ( int )status );
        return ESP01_ERR_TIMEOUT;
    }

    return ESP01_SUCCESS;
}

static ESP01_Status_t at_send_control_cmd( UART_HandleTypeDef *pUart,
                                           const char *pCmd )
{
    at_drain_rx_until_quiet( "before command",
                             ESP01_AT_PRE_CMD_QUIET_MS,
                             ESP01_AT_PRE_CMD_DRAIN_TIMEOUT_MS );

    return at_send_cmd( pUart, pCmd );
}

/**
 * @brief Block until a specific token appears in the ring-buffer stream or the
 *        timeout expires.
 *
 * Bytes are consumed from the ring-buffer one-at-a-time and compared against a
 * sliding window equal to the length of pToken.  All consumed bytes are
 * discarded (they are AT command echo / response lines, not TCP payload).
 *
 * @param pToken     NUL-terminated string to search for (e.g. "OK", "SEND OK").
 * @param timeoutMs  Maximum wait time in milliseconds.
 *
 * @return ESP01_SUCCESS if found, ESP01_ERR_TIMEOUT if not found in time.
 */
static ESP01_Status_t at_wait_result( const char * const *ppSuccessTokens,
                                      uint32_t            successTokenCount,
                                      const char * const *ppFailureTokens,
                                      uint32_t            failureTokenCount,
                                      uint32_t            timeoutMs )
{
    char     window[ ESP01_AT_RSP_MAX_LEN + 1U ];
    uint16_t windowLen = 0U;
    uint8_t  byte;
    uint32_t deadline = HAL_GetTick() + timeoutMs;

    memset( window, 0, sizeof( window ) );

    while( HAL_GetTick() < deadline )
    {
        if( !ring_pop( &esp01RxRing, &byte ) )
        {
            /* Nothing yet — yield briefly so the ISR can fill the ring. */
            HAL_Delay( 1U );
            continue;
        }

        ESP01_LOG_TRACE( "RX 0x%02x '%c'\n",
                         ( unsigned )byte,
                         ( byte >= 32U && byte <= 126U ) ? ( char )byte : '.' );

        if( windowLen < ESP01_AT_RSP_MAX_LEN )
        {
            window[ windowLen++ ] = ( char )byte;
            window[ windowLen ] = '\0';
        }
        else
        {
            memmove( window, window + 1U, ESP01_AT_RSP_MAX_LEN - 1U );
            window[ ESP01_AT_RSP_MAX_LEN - 1U ] = ( char )byte;
            window[ ESP01_AT_RSP_MAX_LEN ] = '\0';
        }

        for( uint32_t i = 0U; i < failureTokenCount; i++ )
        {
            if( ( ppFailureTokens != NULL ) &&
                ( ppFailureTokens[ i ] != NULL ) &&
                ( strstr( window, ppFailureTokens[ i ] ) != NULL ) )
            {
                ESP01_LOG_ERROR( "AT response failure token: %s\n",
                                 ppFailureTokens[ i ] );
                at_drain_rx_until_quiet( "after failure token",
                                         ESP01_AT_POST_FAIL_QUIET_MS,
                                         ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
                return ESP01_ERR_AT;
            }
        }

        for( uint32_t i = 0U; i < successTokenCount; i++ )
        {
            if( ( ppSuccessTokens != NULL ) &&
                ( ppSuccessTokens[ i ] != NULL ) &&
                ( strstr( window, ppSuccessTokens[ i ] ) != NULL ) )
            {
                ESP01_LOG_DEBUG( "AT response success token: %s\n",
                                 ppSuccessTokens[ i ] );
                return ESP01_SUCCESS;
            }
        }
    }

    return ESP01_ERR_TIMEOUT;
}

static ESP01_Status_t at_wait_token( const char *pToken, uint32_t timeoutMs )
{
    const char *successTokens[] = { pToken };

    return at_wait_result( successTokens,
                           1U,
                           NULL,
                           0U,
                           timeoutMs );
}

static bool scan_line_matches_ssid( const char *pLine, const char *pSsid )
{
    char quotedSsid[ 40 ];

    if( ( pLine == NULL ) || ( pSsid == NULL ) || ( pSsid[ 0 ] == '\0' ) )
    {
        return false;
    }

    snprintf( quotedSsid, sizeof( quotedSsid ), "\"%s\"", pSsid );
    return ( strstr( pLine, quotedSsid ) != NULL );
}

static ESP01_Status_t at_wait_network_scan( const char *pTargetSsid,
                                            bool       *pTargetSeen,
                                            uint32_t    timeoutMs )
{
    char line[ ESP01_SCAN_LINE_MAX_LEN ];
    uint16_t lineLen = 0U;
    uint32_t apCount = 0U;
    uint8_t byte;
    uint32_t deadline = HAL_GetTick() + timeoutMs;
    bool targetSeen = false;

    memset( line, 0, sizeof( line ) );

    while( HAL_GetTick() < deadline )
    {
        if( !ring_pop( &esp01RxRing, &byte ) )
        {
            HAL_Delay( 1U );
            continue;
        }

        ESP01_LOG_TRACE( "RX 0x%02x '%c'\n",
                         ( unsigned )byte,
                         ( byte >= 32U && byte <= 126U ) ? ( char )byte : '.' );

        if( byte == '\r' )
        {
            continue;
        }

        if( byte != '\n' )
        {
            if( lineLen < ( uint16_t )( sizeof( line ) - 1U ) )
            {
                line[ lineLen++ ] = ( char )byte;
                line[ lineLen ] = '\0';
            }
            continue;
        }

        if( lineLen == 0U )
        {
            continue;
        }

        if( strcmp( line, "OK" ) == 0 )
        {
            if( pTargetSeen != NULL )
            {
                *pTargetSeen = targetSeen;
            }

            if( ( pTargetSsid != NULL ) && ( pTargetSsid[ 0 ] != '\0' ) )
            {
                ESP01_LOG_INFO( "WiFi scan complete: %lu AP(s), target \"%s\" %s\n",
                                ( unsigned long )apCount,
                                pTargetSsid,
                                targetSeen ? "visible" : "not seen" );
            }
            else
            {
                ESP01_LOG_INFO( "WiFi scan complete: %lu AP(s)\n",
                                ( unsigned long )apCount );
            }

            return ESP01_SUCCESS;
        }

        if( ( strcmp( line, "ERROR" ) == 0 ) ||
            ( strcmp( line, "FAIL" ) == 0 ) ||
            ( strstr( line, "busy" ) != NULL ) )
        {
            ESP01_LOG_ERROR( "WiFi scan failed: %s\n", line );
            at_drain_rx_until_quiet( "after failed scan",
                                     ESP01_AT_POST_FAIL_QUIET_MS,
                                     ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
            return ESP01_ERR_AT;
        }

        if( strstr( line, "+CWLAP:" ) != NULL )
        {
            apCount++;

            if( scan_line_matches_ssid( line, pTargetSsid ) )
            {
                targetSeen = true;
                ESP01_LOG_INFO( "AP %lu TARGET %s\n",
                                ( unsigned long )apCount,
                                line );
            }
            else
            {
                ESP01_LOG_INFO( "AP %lu %s\n",
                                ( unsigned long )apCount,
                                line );
            }
        }
        else
        {
            ESP01_LOG_DEBUG( "scan response: %s\n", line );
        }

        lineLen = 0U;
        line[ 0 ] = '\0';
    }

    if( pTargetSeen != NULL )
    {
        *pTargetSeen = targetSeen;
    }

    ESP01_LOG_ERROR( "WiFi scan timeout after %lu AP(s)\n",
                     ( unsigned long )apCount );
    at_drain_rx_until_quiet( "after scan timeout",
                             ESP01_AT_POST_FAIL_QUIET_MS,
                             ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
    return ESP01_ERR_TIMEOUT;
}

static ESP01_Status_t at_wait_wifi_join( uint32_t timeoutMs )
{
    char line[ ESP01_SCAN_LINE_MAX_LEN ];
    uint16_t lineLen = 0U;
    uint8_t byte;
    uint32_t deadline = HAL_GetTick() + timeoutMs;
    bool associated = false;
    bool joinFailed = false;

    memset( line, 0, sizeof( line ) );

    while( HAL_GetTick() < deadline )
    {
        if( !ring_pop( &esp01RxRing, &byte ) )
        {
            HAL_Delay( 1U );
            continue;
        }

        ESP01_LOG_TRACE( "RX 0x%02x '%c'\n",
                         ( unsigned )byte,
                         ( byte >= 32U && byte <= 126U ) ? ( char )byte : '.' );

        if( byte == '\r' )
        {
            continue;
        }

        if( byte != '\n' )
        {
            if( lineLen < ( uint16_t )( sizeof( line ) - 1U ) )
            {
                line[ lineLen++ ] = ( char )byte;
                line[ lineLen ] = '\0';
            }
            continue;
        }

        if( lineLen == 0U )
        {
            continue;
        }

        if( strstr( line, "WIFI CONNECTED" ) != NULL )
        {
            associated = true;
            ESP01_LOG_INFO( "WiFi associated, waiting for IP address\n" );
        }
        else if( ( strstr( line, "WIFI GOT IP" ) != NULL ) ||
                 ( strstr( line, "GOT IP" ) != NULL ) )
        {
            ESP01_LOG_INFO( "WiFi got IP\n" );
            return ESP01_SUCCESS;
        }
        else if( ( strcmp( line, "FAIL" ) == 0 ) ||
                 ( strcmp( line, "ERROR" ) == 0 ) ||
                 ( strstr( line, "busy" ) != NULL ) )
        {
            ESP01_LOG_ERROR( "WiFi join failed: %s\n", line );
            at_drain_rx_until_quiet( "after failed join",
                                     ESP01_AT_POST_FAIL_QUIET_MS,
                                     ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
            return ESP01_ERR_AT;
        }
        else if( strstr( line, "+CWJAP:" ) != NULL )
        {
            ESP01_LOG_ERROR( "WiFi join failed: %s\n", line );
            joinFailed = true;
        }
        else if( strstr( line, "WIFI DISCONNECT" ) != NULL )
        {
            ESP01_LOG_DEBUG( "WiFi join response: %s\n", line );
        }
        else if( strcmp( line, "OK" ) == 0 )
        {
            ESP01_LOG_DEBUG( "WiFi join OK before IP%s\n",
                             associated ? " after association" : "" );
        }
        else
        {
            ESP01_LOG_DEBUG( "WiFi join response: %s\n", line );
        }

        lineLen = 0U;
        line[ 0 ] = '\0';
    }

    if( joinFailed )
    {
        ESP01_LOG_ERROR( "WiFi join failed before IP address\n" );
        at_drain_rx_until_quiet( "after failed join",
                                 ESP01_AT_POST_FAIL_QUIET_MS,
                                 ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
        return ESP01_ERR_AT;
    }

    ESP01_LOG_ERROR( "WiFi join timeout waiting for IP\n" );
    at_drain_rx_until_quiet( "after join timeout",
                             ESP01_AT_POST_FAIL_QUIET_MS,
                             ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
    return ESP01_ERR_TIMEOUT;
}

static ESP01_Status_t at_wait_wifi_query( bool *pConnected, uint32_t timeoutMs )
{
    char line[ ESP01_SCAN_LINE_MAX_LEN ];
    uint16_t lineLen = 0U;
    uint8_t byte;
    uint32_t deadline = HAL_GetTick() + timeoutMs;
    bool connected = false;

    memset( line, 0, sizeof( line ) );

    while( HAL_GetTick() < deadline )
    {
        if( !ring_pop( &esp01RxRing, &byte ) )
        {
            HAL_Delay( 1U );
            continue;
        }

        ESP01_LOG_TRACE( "RX 0x%02x '%c'\n",
                         ( unsigned )byte,
                         ( byte >= 32U && byte <= 126U ) ? ( char )byte : '.' );

        if( byte == '\r' )
        {
            continue;
        }

        if( byte != '\n' )
        {
            if( lineLen < ( uint16_t )( sizeof( line ) - 1U ) )
            {
                line[ lineLen++ ] = ( char )byte;
                line[ lineLen ] = '\0';
            }
            continue;
        }

        if( lineLen == 0U )
        {
            continue;
        }

        if( strcmp( line, "OK" ) == 0 )
        {
            if( pConnected != NULL )
            {
                *pConnected = connected;
            }

            ESP01_LOG_INFO( "WiFi association query: %s\n",
                            connected ? "connected" : "not connected" );
            return ESP01_SUCCESS;
        }

        if( ( strcmp( line, "ERROR" ) == 0 ) ||
            ( strcmp( line, "FAIL" ) == 0 ) ||
            ( strstr( line, "busy" ) != NULL ) )
        {
            ESP01_LOG_ERROR( "WiFi association query failed: %s\n", line );
            at_drain_rx_until_quiet( "after failed query",
                                     ESP01_AT_POST_FAIL_QUIET_MS,
                                     ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
            return ESP01_ERR_AT;
        }

        if( strstr( line, "+CWJAP:" ) != NULL )
        {
            connected = ( strchr( line, '"' ) != NULL );
            ESP01_LOG_DEBUG( "WiFi association response: %s\n", line );
        }
        else if( strstr( line, "No AP" ) != NULL )
        {
            connected = false;
            ESP01_LOG_DEBUG( "WiFi association response: %s\n", line );
        }
        else
        {
            ESP01_LOG_DEBUG( "WiFi association response: %s\n", line );
        }

        lineLen = 0U;
        line[ 0 ] = '\0';
    }

    at_drain_rx_until_quiet( "after query timeout",
                             ESP01_AT_POST_FAIL_QUIET_MS,
                             ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS );
    return ESP01_ERR_TIMEOUT;
}

/**
 * @brief Send an AT command and wait for "OK".
 *
 * Also treats "ERROR" as an explicit failure so we do not spin until timeout.
 */
static ESP01_Status_t at_cmd_ok( UART_HandleTypeDef *pUart,
                                  const char         *pCmd,
                                  uint32_t            timeoutMs )
{
    static const char *successTokens[] = { "OK" };
    static const char *failureTokens[] = { "ERROR", "FAIL" };
    ESP01_Status_t ret;

    ret = at_send_control_cmd( pUart, pCmd );
    if( ret != ESP01_SUCCESS )
    {
        return ret;
    }

    /* at_wait_token scans for "OK"; "ERROR" arriving first will cause a
     * timeout — acceptable for the low command frequency used here. */
    return at_wait_result( successTokens,
                           sizeof( successTokens ) / sizeof( successTokens[ 0 ] ),
                           failureTokens,
                           sizeof( failureTokens ) / sizeof( failureTokens[ 0 ] ),
                           timeoutMs );
}

static ESP01_Status_t uart_prepare_rx( UART_HandleTypeDef *pUart )
{
    if( HAL_UART_AbortReceive( pUart ) == HAL_TIMEOUT )
    {
        return ESP01_ERR_UART;
    }

    __HAL_UART_CLEAR_FLAG( pUart,
                           UART_CLEAR_OREF |
                           UART_CLEAR_NEF |
                           UART_CLEAR_PEF |
                           UART_CLEAR_FEF |
                           UART_CLEAR_IDLEF |
                           UART_CLEAR_RTOF );
    __HAL_UART_SEND_REQ( pUart, UART_RXDATA_FLUSH_REQUEST );
    pUart->ErrorCode = HAL_UART_ERROR_NONE;

    return ESP01_SUCCESS;
}

static void log_at_command( const char *pCmd )
{
    if( pCmd == NULL )
    {
        return;
    }

    if( strcmp( pCmd, "AT+CWJAP?" ) == 0 )
    {
        ESP01_LOG_DEBUG( "AT TX: AT+CWJAP?\n" );
    }
    else if( strncmp( pCmd, "AT+CWJAP", 8U ) == 0 )
    {
        ESP01_LOG_DEBUG( "AT TX: AT+CWJAP=<ssid>,<password hidden>\n" );
    }
    else
    {
        ESP01_LOG_DEBUG( "AT TX: %s\n", pCmd );
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * ISR-context callback
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Override HAL_UARTEx_RxEventCallback() in your application and
 *        forward here.
 *
 * The DMA peripheral writes into dmaRxBuf[] until IDLE line or buffer-full.
 * Size is the number of bytes received in this DMA window. We copy those bytes
 * into the software ring-buffer and then re-arm DMA for the next window.
 *
 * We also scan for the ESP01 "CLOSED" URC to detect server-initiated
 * disconnections without requiring active polling.
 */
void ESP01_UART_RxEventCallback( UART_HandleTypeDef *pUart, uint16_t Size )
{
    /* Ignore events from other UART peripherals. */
    if( pUart->Instance != ESP01_UART_HANDLE.Instance )
    {
        return;
    }

    /* Drain received DMA bytes into the ring-buffer. */
    uint16_t bytesToCopy = Size;

    if( bytesToCopy > ESP01_RX_DMA_BUF_SIZE )
    {
        bytesToCopy = ESP01_RX_DMA_BUF_SIZE;
    }

    if( bytesToCopy > 0U )
    {
        SCB_InvalidateDCache_by_Addr( ( uint32_t * )dmaRxBuf, ESP01_RX_DMA_BUF_SIZE );
    }

    for( uint16_t i = 0U; i < bytesToCopy; i++ )
    {
        ring_push( &esp01RxRing, dmaRxBuf[ i ] );
    }

    /* ── Detect "CLOSED" URC (server closed the connection) ─────────────────── */
    /* Scan the last few bytes in the ring for the token. */
    if( pActiveCtx != NULL && pActiveCtx->connected )
    {
        /* A lightweight heuristic: check if "CLOSED" is in the last 8 bytes.   *
         * Full token search is avoided here to keep ISR latency short.          */
        static const char closedToken[] = "CLOSED";
        uint16_t avail = ring_available( &esp01RxRing );

        if( avail >= ( sizeof( closedToken ) - 1U ) )
        {
            /* Peek without consuming — walk backward from head. */
            uint16_t checkLen = sizeof( closedToken ) - 1U;
            uint16_t startIdx = ( uint16_t )( ( esp01RxRing.head
                                                - checkLen
                                                + ESP01_RX_RING_SIZE )
                                              % ESP01_RX_RING_SIZE );
            bool match = true;

            for( uint16_t i = 0U; i < checkLen; i++ )
            {
                uint16_t idx = ( startIdx + i ) % ESP01_RX_RING_SIZE;

                if( esp01RxRing.buf[ idx ] != ( uint8_t )closedToken[ i ] )
                {
                    match = false;
                    break;
                }
            }

            if( match )
            {
                pActiveCtx->linkClosed = true;
            }
        }
    }

    /* Re-arm DMA for the next idle-delimited burst. */
    if( HAL_UARTEx_ReceiveToIdle_DMA( pUart, dmaRxBuf, ESP01_RX_DMA_BUF_SIZE ) == HAL_OK )
    {
        /* Suppress the half-transfer DMA interrupt to avoid spurious callbacks. */
        __HAL_DMA_DISABLE_IT( pUart->hdmarx, DMA_IT_HT );
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────── */

ESP01_Status_t ESP01_Init( NetworkContext_t   *pCtx,
                            UART_HandleTypeDef *pUart )
{
    if( ( pCtx == NULL ) || ( pUart == NULL ) )
    {
        return ESP01_ERR_PARAM;
    }

    /* Initialise context. */
    memset( pCtx, 0, sizeof( *pCtx ) );
    pCtx->pUart      = pUart;
    pCtx->connected  = false;
    pCtx->linkClosed = false;

    pActiveCtx = pCtx;

    /* Initialise ring-buffer. */
    memset( &esp01RxRing, 0, sizeof( esp01RxRing ) );

    /* Start DMA reception. */
    if( pUart->hdmarx == NULL )
    {
        return ESP01_ERR_UART;
    }

    if( uart_prepare_rx( pUart ) != ESP01_SUCCESS )
    {
        return ESP01_ERR_UART;
    }

    if( HAL_UARTEx_ReceiveToIdle_DMA( pUart, dmaRxBuf, ESP01_RX_DMA_BUF_SIZE ) != HAL_OK )
    {
        return ESP01_ERR_UART;
    }

    __HAL_DMA_DISABLE_IT( pUart->hdmarx, DMA_IT_HT );

    /* Basic module configuration sequence. */
    HAL_Delay( 500U );                                    /* module boot guard  */
    ESP01_Status_t ret = at_cmd_ok( pUart, "AT", ESP01_AT_TIMEOUT_MS );
    if( ret != ESP01_SUCCESS )
    {
        return ret;
    }

    ret = at_cmd_ok( pUart, "ATE0", ESP01_AT_TIMEOUT_MS );
    if( ret != ESP01_SUCCESS )
    {
        return ret;
    }

    ret = at_cmd_ok( pUart, "AT+CWMODE=1", ESP01_AT_TIMEOUT_MS );
    if( ret != ESP01_SUCCESS )
    {
        return ret;
    }

    ret = at_cmd_ok( pUart, "AT+CWAUTOCONN=0", ESP01_AT_TIMEOUT_MS );
    if( ret != ESP01_SUCCESS )
    {
        ESP01_LOG_INFO( "AT+CWAUTOCONN=0 failed, continuing with state-machine controlled joins\n" );
    }

    ret = at_cmd_ok( pUart, "AT+CIPMUX=0", ESP01_AT_TIMEOUT_MS );
    if( ret != ESP01_SUCCESS )
    {
        return ret;
    }

    return ESP01_SUCCESS;
}

ESP01_Status_t ESP01_WifiConnect( const char *pSsid, const char *pPassword )
{
    char cmd[ ESP01_AT_CMD_MAX_LEN ];

    if( ( pSsid == NULL ) || ( pPassword == NULL ) || ( pActiveCtx == NULL ) )
    {
        return ESP01_ERR_PARAM;
    }

    snprintf( cmd, sizeof( cmd ),
              "AT+CWJAP=\"%s\",\"%s\"",
              pSsid, pPassword );

    /* "WIFI GOT IP" can take several seconds. */
    ESP01_Status_t ret = at_send_control_cmd( pActiveCtx->pUart, cmd );
    if( ret != ESP01_SUCCESS ) return ret;

    ret = at_wait_wifi_join( ESP01_WIFI_JOIN_TIMEOUT_MS );
    if( ret != ESP01_SUCCESS ) return ret;

    /* Consume trailing "OK". */
    at_wait_token( "OK", ESP01_AT_TIMEOUT_MS );

    return ESP01_SUCCESS;
}

ESP01_Status_t ESP01_IsWifiConnected( bool *pConnected )
{
    ESP01_Status_t ret;

    if( pConnected != NULL )
    {
        *pConnected = false;
    }

    if( ( pActiveCtx == NULL ) || ( pConnected == NULL ) )
    {
        return ESP01_ERR_PARAM;
    }

    ret = at_send_control_cmd( pActiveCtx->pUart, "AT+CWJAP?" );
    if( ret != ESP01_SUCCESS )
    {
        return ret;
    }

    return at_wait_wifi_query( pConnected, ESP01_AT_TIMEOUT_MS );
}

ESP01_Status_t ESP01_LogAvailableNetworks( const char *pTargetSsid,
                                            bool       *pTargetSeen )
{
    ESP01_Status_t ret;

    if( pTargetSeen != NULL )
    {
        *pTargetSeen = false;
    }

    if( pActiveCtx == NULL )
    {
        return ESP01_ERR_PARAM;
    }

    ESP01_LOG_INFO( "Scanning WiFi networks%s%s%s\n",
                    ( pTargetSsid != NULL && pTargetSsid[ 0 ] != '\0' ) ? " for \"" : "",
                    ( pTargetSsid != NULL && pTargetSsid[ 0 ] != '\0' ) ? pTargetSsid : "",
                    ( pTargetSsid != NULL && pTargetSsid[ 0 ] != '\0' ) ? "\"" : "" );

    ret = at_send_control_cmd( pActiveCtx->pUart, "AT+CWLAP" );
    if( ret != ESP01_SUCCESS )
    {
        return ret;
    }

    return at_wait_network_scan( pTargetSsid,
                                 pTargetSeen,
                                 ESP01_WIFI_SCAN_TIMEOUT_MS );
}

ESP01_Status_t ESP01_Connect( NetworkContext_t *pCtx,
                               const char       *pHost,
                               uint16_t          port )
{
    char cmd[ ESP01_AT_CMD_MAX_LEN ];
    static const char *successTokens[] =
    {
        "CONNECT",
        "Linked",
        "ALREADY CONNECTED",
        "OK"
    };
    static const char *failureTokens[] =
    {
        "DNS Fail",
        "ERROR",
        "FAIL",
        "CLOSED",
        "UNLINK"
    };

    if( ( pCtx == NULL ) || ( pHost == NULL ) )
    {
        return ESP01_ERR_PARAM;
    }

    snprintf( cmd, sizeof( cmd ),
              "AT+CIPSTART=\"TCP\",\"%s\",%u",
              pHost, ( unsigned )port );

    ESP01_Status_t ret = at_send_control_cmd( pCtx->pUart, cmd );
    if( ret != ESP01_SUCCESS ) return ret;

    ret = at_wait_result( successTokens,
                          sizeof( successTokens ) / sizeof( successTokens[ 0 ] ),
                          failureTokens,
                          sizeof( failureTokens ) / sizeof( failureTokens[ 0 ] ),
                          10000U );
    if( ret != ESP01_SUCCESS ) return ret;

    /* Consume trailing "OK". */
    at_wait_token( "OK", ESP01_AT_TIMEOUT_MS );

    /* Start the new session with an empty RX buffer so leftover bytes from the
     * previous connection (e.g. a stale PINGRESP) are not read as the CONNACK. */
    esp01_rx_flush();

    pCtx->connected  = true;
    pCtx->linkClosed = false;

    return ESP01_SUCCESS;
}

ESP01_Status_t ESP01_Disconnect( NetworkContext_t *pCtx )
{
    if( pCtx == NULL )
    {
        return ESP01_ERR_PARAM;
    }

    pCtx->connected = false;

    /* Best-effort: if it fails the TCP stack will time out on the broker side. */
    at_cmd_ok( pCtx->pUart, "AT+CIPCLOSE", ESP01_AT_TIMEOUT_MS );

    return ESP01_SUCCESS;
}

void ESP01_FillTransportInterface( TransportInterface_t *pTransport,
                                   NetworkContext_t     *pCtx )
{
    if( ( pTransport == NULL ) || ( pCtx == NULL ) )
    {
        return;
    }

    pTransport->recv           = esp01_recv;
    pTransport->send           = esp01_send;
    pTransport->writev         = esp01_writev;
    pTransport->pNetworkContext = pCtx;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * coreMQTT transport callbacks
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief TransportRecv_t implementation.
 *
 * The ESP01 prefixes every incoming TCP segment with the unsolicited result
 * code (URC):
 *
 *     \r\n+IPD,<length>:<payload bytes>\r\n
 *
 * This function maintains a small state-machine to strip that framing so that
 * coreMQTT sees a clean byte stream identical to what the broker sent.
 *
 * State machine:
 *
 *   IDLE   ─── '+' seen ──────────────────► HEADER
 *   HEADER ─── 'IPD,' matched, digits read ► PAYLOAD
 *   PAYLOAD ── N bytes delivered ────────► IDLE
 *
 * Bytes that arrive outside an IPD block (AT echo, "OK" lines, etc.) are
 * discarded unless the stream is already in PAYLOAD state.
 */
int32_t esp01_recv( NetworkContext_t *pNetworkContext,
                    void             *pBuffer,
                    size_t            bytesToRecv )
{
    /* State-machine state is kept across calls (static). */
    typedef enum { ST_IDLE, ST_HEADER, ST_PAYLOAD } RecvState_t;

    static RecvState_t state         = ST_IDLE;
    static char        hdrBuf[ 16 ]  = { 0 };  /* "+IPD,NNNNN:" staging area */
    static uint8_t     hdrLen        = 0U;
    static int32_t     payloadRemain = 0;

    /* ── Disconnection check ─────────────────────────────────────────────────── */
    if( ( pNetworkContext == NULL ) || !pNetworkContext->connected )
    {
        return -1;
    }

    if( pNetworkContext->linkClosed )
    {
        pNetworkContext->connected = false;
        return -1;   /* signal disconnect to coreMQTT */
    }

    /* Drop any half-parsed +IPD framing carried over from a previous session
     * (esp01_rx_flush() requested a reset on (re)connect). */
    if( esp01RecvResetReq )
    {
        state         = ST_IDLE;
        hdrLen        = 0U;
        payloadRemain = 0;
        esp01RecvResetReq = false;
    }

    /* ── No data available — return 0 so coreMQTT can retry ─────────────────── */
    if( ring_available( &esp01RxRing ) == 0U )
    {
        return 0;
    }

    uint8_t  *pOut    = ( uint8_t * )pBuffer;
    int32_t   copied  = 0;
    uint8_t   byte;

    while( ( uint32_t )copied < ( uint32_t )bytesToRecv )
    {
        if( !ring_pop( &esp01RxRing, &byte ) )
        {
            break;  /* ring empty — return what we have so far */
        }

        switch( state )
        {
            /* ── IDLE: scan for the start of an +IPD header ─────────────────── */
            case ST_IDLE:
                if( byte == '+' )
                {
                    hdrBuf[ 0 ] = '+';
                    hdrLen      = 1U;
                    state       = ST_HEADER;
                }
                /* All other bytes (CR, LF, "OK", etc.) are silently dropped.   */
                break;

            /* ── HEADER: accumulate "+IPD,<n>:" ─────────────────────────────── */
            case ST_HEADER:
                if( hdrLen < ( uint8_t )( sizeof( hdrBuf ) - 1U ) )
                {
                    hdrBuf[ hdrLen++ ] = ( char )byte;
                }

                if( byte == ':' )
                {
                    /* Full header received — parse the payload length. */
                    hdrBuf[ hdrLen ] = '\0';

                    /* hdrBuf now looks like "+IPD,1234:" */
                    const char *pComma = strchr( hdrBuf, ',' );

                    if( pComma != NULL )
                    {
                        payloadRemain = ( int32_t )strtol( pComma + 1, NULL, 10 );
                    }
                    else
                    {
                        /* Malformed header — reset and keep scanning. */
                        payloadRemain = 0;
                    }

                    hdrLen = 0U;
                    state  = ( payloadRemain > 0 ) ? ST_PAYLOAD : ST_IDLE;
                }
                else if( hdrLen >= ( uint8_t )( sizeof( hdrBuf ) - 1U ) )
                {
                    /* Header too long — framing error; reset. */
                    hdrLen = 0U;
                    state  = ST_IDLE;
                }
                break;

            /* ── PAYLOAD: copy payload bytes straight into the caller's buffer ─ */
            case ST_PAYLOAD:
                pOut[ copied++ ] = byte;
                payloadRemain--;

                if( payloadRemain <= 0 )
                {
                    state = ST_IDLE;    /* payload exhausted; expect next +IPD  */

                    /* Return immediately so coreMQTT can process what it got.   */
                    return copied;
                }
                break;

            default:
                state = ST_IDLE;
                break;
        }
    }

    return copied;
}

/**
 * @brief TransportSend_t implementation.
 *
 * Sends data over the active TCP connection using the AT+CIPSEND flow:
 *
 *   Host ─► "AT+CIPSEND=<n>\r\n"
 *   ESP01 ◄─ ">"               (prompt: module ready for raw data)
 *   Host ─► <n raw bytes>
 *   ESP01 ◄─ "SEND OK"         (TCP layer has accepted the data)
 *
 * If the TX buffer inside the ESP01 is full, the module may return 0 bytes
 * accepted; in that case the function returns 0 so coreMQTT retries.
 */
int32_t esp01_send( NetworkContext_t *pNetworkContext,
                    const void       *pBuffer,
                    size_t            bytesToSend )
{
    if( ( pNetworkContext == NULL ) || ( pBuffer == NULL ) )
    {
        return -1;
    }

    if( !pNetworkContext->connected || pNetworkContext->linkClosed )
    {
        return -1;
    }

    if( bytesToSend == 0U )
    {
        return 0;
    }

    char cmd[ ESP01_AT_CMD_MAX_LEN ];
    ESP01_Status_t ret;

    /* Step 1 — announce the number of bytes we want to send. */
    snprintf( cmd, sizeof( cmd ), "AT+CIPSEND=%u", ( unsigned )bytesToSend );

    ret = at_send_cmd( pNetworkContext->pUart, cmd );
    if( ret != ESP01_SUCCESS )
    {
        return -1;
    }

    /* Step 2 — wait for the ">" prompt that signals the module is ready. */
    ret = at_wait_token( ">", ESP01_CIPSEND_PROMPT_TIMEOUT_MS );
    if( ret != ESP01_SUCCESS )
    {
        /* Module not ready; return 0 so the caller can retry. */
        return 0;
    }

    /* Step 3 — transmit the raw payload bytes (no CR/LF added). */
    HAL_StatusTypeDef halStatus;

    halStatus = HAL_UART_Transmit( pNetworkContext->pUart,
                                   ( const uint8_t * )pBuffer,
                                   ( uint16_t )bytesToSend,
                                   ESP01_HAL_TX_TIMEOUT_MS );

    if( halStatus != HAL_OK )
    {
        return -1;
    }

    /* Step 4 — wait for "SEND OK" confirmation from the ESP01. */
    ret = at_wait_token( "SEND OK", ESP01_SEND_OK_TIMEOUT_MS );

    if( ret == ESP01_SUCCESS )
    {
        return ( int32_t )bytesToSend;
    }

    /* Check if the module reported a hard error. */
    return ESP01_ERR_SEND;
}

int32_t esp01_writev( NetworkContext_t     *pNetworkContext,
                      TransportOutVector_t *pIoVec,
                      size_t                ioVecCount )
{
    size_t total = 0U;

    if( ( pNetworkContext == NULL ) || ( pIoVec == NULL ) )
    {
        return -1;
    }

    for( size_t i = 0U; i < ioVecCount; i++ )
    {
        if( pIoVec[ i ].iov_len == 0U )
        {
            continue;
        }

        if( pIoVec[ i ].iov_base == NULL )
        {
            return -1;
        }

        if( ( pIoVec[ i ].iov_len > ESP01_TX_COALESCE_BUF_SIZE ) ||
            ( total > ( ESP01_TX_COALESCE_BUF_SIZE - pIoVec[ i ].iov_len ) ) )
        {
            ESP01_LOG_ERROR( "MQTT packet too large for coalesced CIPSEND (%lu > %u)\n",
                             ( unsigned long )( total + pIoVec[ i ].iov_len ),
                             ( unsigned )ESP01_TX_COALESCE_BUF_SIZE );
            return -1;
        }

        memcpy( &txCoalesceBuf[ total ], pIoVec[ i ].iov_base, pIoVec[ i ].iov_len );
        total += pIoVec[ i ].iov_len;
    }

    if( total == 0U )
    {
        return 0;
    }

    if( total > ( size_t )INT32_MAX )
    {
        return -1;
    }

    ESP01_LOG_DEBUG( "MQTT writev coalesced %lu vector byte(s)\n",
                     ( unsigned long )total );

    return esp01_send( pNetworkContext, txCoalesceBuf, total );
}
