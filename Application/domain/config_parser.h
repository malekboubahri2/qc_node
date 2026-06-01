#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file config_parser.h
 * @brief Decode the qc/config JSON payloads into the domain storage modules.
 *
 * Pure C, no HAL — host-testable. Writes results into operator_list and
 * defect_config; callers are responsible for any concurrency guard (the live
 * MQTT path locks the UI bridge; boot-from-flash runs before the scheduler).
 *
 * Matches the server publisher exactly (server/app/mqtt/publisher.py):
 *   operators: { "operators": [ {id, name, pin_hash} ] }
 *   products:  { "products":  [ {id, name,
 *                  "categories": { "PMP": [..], "INJECTION": [..] } } ] }
 *               where each defect type is {id, label, is_other_fallback,
 *               display_order}.
 */

/** @return 0 on success (operator_list updated), negative on parse error. */
int config_parser_apply_operators(const char *json, size_t len);

/** @return 0 on success (defect_config updated), negative on parse error. */
int config_parser_apply_products(const char *json, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_PARSER_H */
