#ifndef UI_DATA_BRIDGE_H
#define UI_DATA_BRIDGE_H

#include "domain/defect_config.h"
#include "domain/operator_list.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_bridge_set_operators(const operator_entry_t *list, int count);
void ui_bridge_set_products(const product_entry_t *list, int count);
void ui_bridge_set_defect_types(int product_id, int category,
                                const defect_type_t *list, int count);

#ifdef __cplusplus
}
#endif

#endif /* UI_DATA_BRIDGE_H */
