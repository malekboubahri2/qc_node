#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file app_log.h
 * @brief Unified, macro-based logging arbitrated by severity AND subsystem.
 *
 * Every log call names a layer (subsystem) and a level. Output is gated twice:
 *   1. Compile time — calls more verbose than APP_LOG_COMPILE_LEVEL vanish.
 *   2. Run time     — each layer has its own threshold (app_log_set_level),
 *                     so e.g. NET can be quietened to WARN while CFG stays at
 *                     DEBUG for focused debugging.
 *
 * Output format (one line per call, CRLF-terminated):
 *     [I][mqtt ] connection established
 *      │   └ layer        └ message
 *      └ level (E/W/I/D/T)
 *
 * coreMQTT, the MQTT agent and the ESP-01 driver route their own log macros
 * here so the whole system shares one format and one set of controls.
 */

typedef enum
{
    APP_LOG_NONE  = 0,
    APP_LOG_ERROR = 1,
    APP_LOG_WARN  = 2,
    APP_LOG_INFO  = 3,
    APP_LOG_DEBUG = 4,
    APP_LOG_TRACE = 5,
} app_log_level_t;

typedef enum
{
    APP_LAYER_SYS = 0,  /**< boot / main / generic */
    APP_LAYER_NET,      /**< ESP-01 transport, Wi-Fi, TCP */
    APP_LAYER_MQTT,     /**< coreMQTT + agent */
    APP_LAYER_CFG,      /**< config parser + MQTT config callbacks */
    APP_LAYER_STORE,    /**< config_store / persistence / queues */
    APP_LAYER_UI,       /**< TouchGFX model / presenters */
    APP_LAYER_COUNT
} app_log_layer_t;

/* Compile-time ceiling. Calls above this level cost nothing (removed by the
 * preprocessor). Override with -DAPP_LOG_COMPILE_LEVEL=... per build config. */
#ifndef APP_LOG_COMPILE_LEVEL
#define APP_LOG_COMPILE_LEVEL APP_LOG_DEBUG
#endif

void            app_log_init(void);
void            app_log_set_level(app_log_layer_t layer, app_log_level_t level);
void            app_log_set_all(app_log_level_t level);
app_log_level_t app_log_get_level(app_log_layer_t layer);
bool            app_log_enabled(app_log_layer_t layer, app_log_level_t level);

/* One-shot line: prefix + formatted message + CRLF. */
void app_log_emit(app_log_layer_t layer, app_log_level_t level,
                  const char *fmt, ...);

/* Three-part form for libraries whose macros wrap the message in parentheses
 * (e.g. coreMQTT's LogInfo(("...", x))). Bracket with begin/end. */
void app_log_begin(app_log_layer_t layer, app_log_level_t level);
void app_log_cont(const char *fmt, ...);
void app_log_end(void);

#define APP_LOG(layer, level, ...)                              \
    do {                                                        \
        if ((int)(level) <= (int)APP_LOG_COMPILE_LEVEL) {       \
            app_log_emit((layer), (level), __VA_ARGS__);        \
        }                                                       \
    } while (0)

#define LOG_ERR(layer, ...)   APP_LOG((layer), APP_LOG_ERROR, __VA_ARGS__)
#define LOG_WARN(layer, ...)  APP_LOG((layer), APP_LOG_WARN,  __VA_ARGS__)
#define LOG_INFO(layer, ...)  APP_LOG((layer), APP_LOG_INFO,  __VA_ARGS__)
#define LOG_DBG(layer, ...)   APP_LOG((layer), APP_LOG_DEBUG, __VA_ARGS__)
#define LOG_TRACE(layer, ...) APP_LOG((layer), APP_LOG_TRACE, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */
