#include "app_log.h"
#include "stm32h7xx_hal.h"
#include <stdarg.h>
#include <stdio.h>

/* The debug console UART (same one newlib's _write uses in main.c). */
extern UART_HandleTypeDef huart1;
#define APP_LOG_UART        huart1
#define APP_LOG_TX_TIMEOUT  50U
#define APP_LOG_LINE_MAX    208

static app_log_level_t s_levels[APP_LAYER_COUNT];
static bool            s_init = false;

static const char *const k_layer_name[APP_LAYER_COUNT] = {
    "sys", "net", "mqtt", "cfg", "store", "ui"
};
static const char k_level_char[] = { '-', 'E', 'W', 'I', 'D', 'T' };

static void raw_out(const char *s, int n)
{
    if (n > 0) {
        HAL_UART_Transmit(&APP_LOG_UART, (uint8_t *)s, (uint16_t)n, APP_LOG_TX_TIMEOUT);
    }
}

void app_log_init(void)
{
    for (int i = 0; i < APP_LAYER_COUNT; i++) {
        s_levels[i] = APP_LOG_INFO;
    }
    s_init = true;
}

void app_log_set_level(app_log_layer_t layer, app_log_level_t level)
{
    if (!s_init) {
        app_log_init();
    }
    if ((unsigned)layer < APP_LAYER_COUNT) {
        s_levels[layer] = level;
    }
}

void app_log_set_all(app_log_level_t level)
{
    for (int i = 0; i < APP_LAYER_COUNT; i++) {
        s_levels[i] = level;
    }
    s_init = true;
}

app_log_level_t app_log_get_level(app_log_layer_t layer)
{
    if (!s_init) {
        app_log_init();
    }
    return ((unsigned)layer < APP_LAYER_COUNT) ? s_levels[layer] : APP_LOG_NONE;
}

bool app_log_enabled(app_log_layer_t layer, app_log_level_t level)
{
    if (!s_init) {
        app_log_init();
    }
    if ((unsigned)layer >= APP_LAYER_COUNT) {
        return false;
    }
    return (level != APP_LOG_NONE) && ((int)level <= (int)s_levels[layer]);
}

static int format_prefix(char *buf, size_t sz, app_log_layer_t layer,
                         app_log_level_t level)
{
    char lc = ((unsigned)level <= APP_LOG_TRACE) ? k_level_char[level] : '?';
    const char *ln = ((unsigned)layer < APP_LAYER_COUNT) ? k_layer_name[layer]
                                                         : "?";
    int n = snprintf(buf, sz, "[%c][%-5s] ", lc, ln);
    if (n < 0) {
        return 0;
    }
    return (n > (int)sz) ? (int)sz : n;
}

void app_log_emit(app_log_layer_t layer, app_log_level_t level,
                  const char *fmt, ...)
{
    if (!app_log_enabled(layer, level)) {
        return;
    }

    char line[APP_LOG_LINE_MAX];
    int n = format_prefix(line, sizeof(line), layer, level);

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
    va_end(ap);

    int total = (m > 0) ? n + m : n;
    if (total > (int)sizeof(line) - 2) {
        total = (int)sizeof(line) - 2;
    }
    line[total++] = '\r';
    line[total++] = '\n';
    raw_out(line, total);
}

void app_log_begin(app_log_layer_t layer, app_log_level_t level)
{
    char pfx[16];
    int n = format_prefix(pfx, sizeof(pfx), layer, level);
    raw_out(pfx, n);
}

void app_log_cont(const char *fmt, ...)
{
    char body[APP_LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(body) - 1) {
        n = (int)sizeof(body) - 1;
    }
    raw_out(body, n);
}

void app_log_end(void)
{
    raw_out("\r\n", 2);
}
