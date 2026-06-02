/**
 * @file esp01_transport.h
 * @brief coreMQTT transport interface for STM32 + ESP01 (AT commands over UART).
 *
 * Architecture
 * ─────────────
 *  STM32 ──UART──► ESP01 ──WiFi──► MQTT Broker
 *
 * The ESP01 is driven exclusively through Hayes-style AT commands sent over a
 * UART peripheral managed by the STM32 HAL.  Incoming TCP payload is delivered
 * by the module prefixed with the unsolicited "+IPD,<len>:" token; this driver
 * strips that framing and exposes a plain byte-stream to coreMQTT via the
 * standard TransportRecv_t / TransportSend_t callbacks.
 *
 * Receive side
 * ─────────────
 *  A DMA receive buffer (ESP01_RX_DMA_BUF_SIZE bytes) is armed with
 *  HAL_UARTEx_ReceiveToIdle_DMA().  On each IDLE/full event the ISR calls
 *  ESP01_UART_RxEventCallback() which drains the received bytes into the
 *  software ring-buffer (esp01RxRing).  esp01_recv() reads from that
 *  ring-buffer, skips any "+IPD,N:" headers, and returns pure payload bytes to
 *  the coreMQTT engine.
 *
 * Send side
 * ──────────
 *  esp01_send() issues "AT+CIPSEND=<n>", waits for the ">" prompt, then
 *  pushes the raw bytes with HAL_UART_Transmit().  The module returns "SEND OK"
 *  when the TCP layer has accepted the data.
 *
 * Usage (minimal)
 * ────────────────
 *  1. Call ESP01_Init() once after HAL / RTOS init.
 *  2. Call ESP01_Connect() to open the TCP connection to the broker.
 *  3. Fill a TransportInterface_t (see esp01_transport.c for helpers) and pass
 *     it to MQTT_Init().
 *  4. Forward every UART RX event to ESP01_UART_RxEventCallback() from your
 *     HAL_UARTEx_RxEventCallback() override.
 *  5. Call ESP01_Disconnect() when done.
 */

#ifndef ESP01_TRANSPORT_H
#define ESP01_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Pull in the HAL for the target MCU family. Adjust the path for your project. */
#include "stm32h7xx_hal.h"          /* ← change to your STM32 family           */

#include "transport_interface.h"    /* coreMQTT transport types                 */

typedef enum
{
    ESP01_LOG_LEVEL_NONE  = 0,
    ESP01_LOG_LEVEL_ERROR = 1,
    ESP01_LOG_LEVEL_INFO  = 2,
    ESP01_LOG_LEVEL_DEBUG = 3,
    ESP01_LOG_LEVEL_TRACE = 4,
} ESP01_LogLevel_t;

#ifndef ESP01_LOG_LEVEL
#if defined(DEBUG)
#define ESP01_LOG_LEVEL ESP01_LOG_LEVEL_DEBUG
#else
#define ESP01_LOG_LEVEL ESP01_LOG_LEVEL_INFO
#endif
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Compile-time configuration – override in your project's compiler flags or a
 * dedicated esp01_transport_config.h that is included before this header.
 * ───────────────────────────────────────────────────────────────────────────── */

extern UART_HandleTypeDef huart2;
/** UART handle used to talk to the ESP01 module. */
#ifndef ESP01_UART_HANDLE
#define ESP01_UART_HANDLE           huart2
#endif

/** Baud rate for the STM32–ESP01 UART link. Must match the module firmware. */
#ifndef ESP01_UART_BAUDRATE
#define ESP01_UART_BAUDRATE         115200U
#endif

/**
 * @brief Size of the DMA reception buffer (bytes).
 *
 * The DMA peripheral writes here until IDLE/full; the ISR callback drains the
 * bytes into esp01RxRing and re-arms DMA. Must be a power-of-two >= 64.
 */
#ifndef ESP01_RX_DMA_BUF_SIZE
#define ESP01_RX_DMA_BUF_SIZE       256U
#endif

/**
 * @brief Size of the software ring-buffer that bridges the ISR and the
 *        esp01_recv() transport callback (bytes).  Must be a power-of-two.
 */
#ifndef ESP01_RX_RING_SIZE
#define ESP01_RX_RING_SIZE          4096U
#endif

/** Maximum length of a single AT command string (including CR/LF). */
#ifndef ESP01_AT_CMD_MAX_LEN
#define ESP01_AT_CMD_MAX_LEN        128U
#endif

/** Maximum length of an AT response line. */
#ifndef ESP01_AT_RSP_MAX_LEN
#define ESP01_AT_RSP_MAX_LEN        128U
#endif

/** Timeout (ms) waiting for a generic AT "OK" response. */
#ifndef ESP01_AT_TIMEOUT_MS
#define ESP01_AT_TIMEOUT_MS         3000U
#endif

/** Timeout (ms) for AT+CWLAP WiFi scans. */
#ifndef ESP01_WIFI_SCAN_TIMEOUT_MS
#define ESP01_WIFI_SCAN_TIMEOUT_MS  12000U
#endif

/** Timeout (ms) for AT+CWJAP to finish association and DHCP. */
#ifndef ESP01_WIFI_JOIN_TIMEOUT_MS
#define ESP01_WIFI_JOIN_TIMEOUT_MS  30000U
#endif

/** Required idle time on the UART before issuing a control AT command. */
#ifndef ESP01_AT_PRE_CMD_QUIET_MS
#define ESP01_AT_PRE_CMD_QUIET_MS   200U
#endif

/** Maximum time spent draining stale AT/URC text before a control command. */
#ifndef ESP01_AT_PRE_CMD_DRAIN_TIMEOUT_MS
#define ESP01_AT_PRE_CMD_DRAIN_TIMEOUT_MS  2000U
#endif

/** Required idle time after a failed AT transaction before retrying. */
#ifndef ESP01_AT_POST_FAIL_QUIET_MS
#define ESP01_AT_POST_FAIL_QUIET_MS  250U
#endif

/** Maximum time spent draining trailing failure text before returning. */
#ifndef ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS
#define ESP01_AT_POST_FAIL_DRAIN_TIMEOUT_MS  1500U
#endif

/** Timeout (ms) waiting for the ">" CIPSEND prompt. */
#ifndef ESP01_CIPSEND_PROMPT_TIMEOUT_MS
#define ESP01_CIPSEND_PROMPT_TIMEOUT_MS   2000U
#endif

/** Timeout (ms) waiting for "SEND OK" after pushing payload data. */
#ifndef ESP01_SEND_OK_TIMEOUT_MS
#define ESP01_SEND_OK_TIMEOUT_MS    5000U
#endif

/** Timeout (ms) for HAL_UART_Transmit() calls. */
#ifndef ESP01_HAL_TX_TIMEOUT_MS
#define ESP01_HAL_TX_TIMEOUT_MS     1000U
#endif

/** Maximum MQTT packet bytes coalesced into one AT+CIPSEND transaction. */
#ifndef ESP01_TX_COALESCE_BUF_SIZE
#define ESP01_TX_COALESCE_BUF_SIZE  2048U
#endif

/* ─────────────────────────────────────────────────────────────────────────────
 * Public types
 * ───────────────────────────────────────────────────────────────────────────── */

/** Return codes used by all ESP01 functions. */
typedef enum
{
    ESP01_SUCCESS        =  0,  /**< Operation completed successfully.           */
    ESP01_ERR_TIMEOUT    = -1,  /**< Timed out waiting for a module response.    */
    ESP01_ERR_AT         = -2,  /**< Module returned "ERROR" to an AT command.   */
    ESP01_ERR_PARAM      = -3,  /**< Invalid function argument.                  */
    ESP01_ERR_OVERFLOW   = -4,  /**< Ring-buffer overflowed; data was lost.      */
    ESP01_ERR_SEND       = -5,  /**< CIPSEND sequence failed.                    */
    ESP01_ERR_DISCONNECT = -6,  /**< Network link was detected as closed.        */
    ESP01_ERR_UART       = -7,  /**< UART/DMA setup or transfer failed.          */
} ESP01_Status_t;

/**
 * @brief coreMQTT NetworkContext implementation.
 *
 * A pointer to this struct is stored in TransportInterface_t.pNetworkContext
 * and forwarded to esp01_recv() / esp01_send() on every call.
 */
struct NetworkContext
{
    UART_HandleTypeDef *pUart;      /**< HAL UART handle for the ESP01 link.    */
    bool                connected;  /**< True while the TCP session is open.     */
    volatile bool       linkClosed; /**< Set by ISR on "CLOSED" URC from ESP01. */
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the ESP01 driver.
 *
 * Resets the ring-buffer, enables UART DMA reception, and sends the basic
 * configuration sequence to the module (ATE0 / AT+CWMODE=1 / AT+CIPMUX=0).
 * Call this once after your HAL_Init() and before any other ESP01 function.
 *
 * @param[out] pCtx     NetworkContext to initialise.
 * @param[in]  pUart    Pointer to an already-initialised HAL UART handle.
 *
 * @return ESP01_SUCCESS or a negative error code.
 */
ESP01_Status_t ESP01_Init( NetworkContext_t *pCtx,
                            UART_HandleTypeDef *pUart );

/**
 * @brief Join a WiFi network.
 *
 * Issues AT+CWJAP and waits for "WIFI GOT IP".
 *
 * @param[in] pSsid      NUL-terminated SSID string.
 * @param[in] pPassword  NUL-terminated password string.
 *
 * @return ESP01_SUCCESS or a negative error code.
 */
ESP01_Status_t ESP01_WifiConnect( const char *pSsid,
                                   const char *pPassword );

/**
 * @brief Query whether the ESP01 station is currently associated to an AP.
 *
 * Issues AT+CWJAP? and parses the response without changing the WiFi state.
 * Only call this while no MQTT/TCP payload traffic is active, because it
 * consumes AT response bytes from the shared UART ring.
 *
 * @param[out] pConnected  Set true when the module reports a joined AP.
 *
 * @return ESP01_SUCCESS or a negative error code.
 */
ESP01_Status_t ESP01_IsWifiConnected( bool *pConnected );

/**
 * @brief Scan and log visible WiFi networks.
 *
 * Issues AT+CWLAP, logs every returned +CWLAP line at INFO level, and reports
 * whether pTargetSsid was found. Hidden SSIDs may not appear even when usable.
 *
 * @param[in]  pTargetSsid   Optional SSID to search for; pass NULL to only list.
 * @param[out] pTargetSeen   Optional result flag set true when pTargetSsid appears.
 *
 * @return ESP01_SUCCESS or a negative error code.
 */
ESP01_Status_t ESP01_LogAvailableNetworks( const char *pTargetSsid,
                                            bool       *pTargetSeen );

/**
 * @brief Open a TCP connection to the MQTT broker.
 *
 * Issues AT+CIPSTART and waits for "CONNECT".
 *
 * @param[in] pCtx      Pointer to an initialised NetworkContext.
 * @param[in] pHost     NUL-terminated hostname or dotted-decimal IP.
 * @param[in] port      Destination TCP port (typically 1883).
 *
 * @return ESP01_SUCCESS or a negative error code.
 */
ESP01_Status_t ESP01_Connect( NetworkContext_t *pCtx,
                               const char *pHost,
                               uint16_t    port );

/**
 * @brief Close the current TCP connection.
 *
 * Issues AT+CIPCLOSE.  Safe to call even if the link is already closed.
 *
 * @param[in] pCtx   Pointer to an initialised NetworkContext.
 *
 * @return ESP01_SUCCESS or a negative error code.
 */
ESP01_Status_t ESP01_Disconnect( NetworkContext_t *pCtx );

/**
 * @brief Populate a TransportInterface_t ready for MQTT_Init().
 *
 * Convenience wrapper that sets recv, send, writev, and pNetworkContext.
 *
 * @param[out] pTransport   Transport struct to fill.
 * @param[in]  pCtx         Initialised NetworkContext.
 */
void ESP01_FillTransportInterface( TransportInterface_t *pTransport,
                                   NetworkContext_t     *pCtx );

/* ─────────────────────────────────────────────────────────────────────────────
 * ISR-context callback — call from HAL_UARTEx_RxEventCallback()
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief Must be called from your HAL_UARTEx_RxEventCallback() override.
 *
 * Copies newly-arrived DMA bytes into the software ring-buffer and re-arms
 * the DMA for the next reception window.  Also scans for the "CLOSED" URC so
 * that a clean server disconnect is detected without polling.
 *
 * @param[in] pUart  The UART handle that fired the event (checked against the
 *                   configured ESP01 UART; other handles are ignored).
 * @param[in] Size   Number of bytes received since the last event (as provided
 *                   by the HAL callback).
 */
void ESP01_UART_RxEventCallback( UART_HandleTypeDef *pUart, uint16_t Size );

/* ─────────────────────────────────────────────────────────────────────────────
 * coreMQTT transport callbacks (passed via TransportInterface_t)
 * ───────────────────────────────────────────────────────────────────────────── */

/**
 * @brief TransportRecv_t implementation.
 *
 * Reads up to bytesToRecv bytes of TCP payload from the ring-buffer, stripping
 * any "+IPD,<n>:" framing injected by the ESP01 module.
 * This function NEVER blocks; it returns 0 if no payload is currently buffered.
 *
 * @param[in]  pNetworkContext  Pointer to the driver's NetworkContext.
 * @param[out] pBuffer         Destination buffer.
 * @param[in]  bytesToRecv     Maximum bytes to copy.
 *
 * @return Number of bytes written into pBuffer (≥ 0), or a negative value on
 *         a detected network disconnection.
 */
int32_t esp01_recv( NetworkContext_t *pNetworkContext,
                    void             *pBuffer,
                    size_t            bytesToRecv );

/**
 * @brief TransportSend_t implementation.
 *
 * Transmits bytesToSend bytes over the active TCP connection using the
 * AT+CIPSEND sequence.  Blocks until the module acknowledges "SEND OK" or
 * a timeout occurs.
 *
 * @param[in] pNetworkContext  Pointer to the driver's NetworkContext.
 * @param[in] pBuffer          Source buffer.
 * @param[in] bytesToSend      Number of bytes to transmit.
 *
 * @return Number of bytes sent (≥ 0), or a negative value on error.
 */
int32_t esp01_send( NetworkContext_t *pNetworkContext,
                    const void       *pBuffer,
                    size_t            bytesToSend );

/**
 * @brief TransportWritev_t implementation.
 *
 * Coalesces coreMQTT scatter-gather vectors into one AT+CIPSEND transaction.
 * This avoids a burst of tiny CIPSEND commands for SUBSCRIBE/PUBLISH packets.
 */
int32_t esp01_writev( NetworkContext_t       *pNetworkContext,
                      TransportOutVector_t   *pIoVec,
                      size_t                  ioVecCount );

#ifdef __cplusplus
}
#endif

#endif /* ESP01_TRANSPORT_H */
