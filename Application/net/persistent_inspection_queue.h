#ifndef PERSISTENT_INSPECTION_QUEUE_H
#define PERSISTENT_INSPECTION_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file persistent_inspection_queue.h
 * @brief Persistent queue for storing inspection events when MQTT is disconnected.
 */

/* Max selectable defect types per category (12 user-defined + 1 "Autre"). */
#ifndef INSPECTION_MAX_DEFECTS
#define INSPECTION_MAX_DEFECTS 13
#endif

/**
 * @brief One full part inspection stored persistently (schema_version 4).
 *        Mirrors inspection_msg_t plus a creation timestamp.
 */
typedef struct
{
    uint8_t  schema_version; /* = 4 */
    int      product_id;
    int      operator_id;
    int      pmp_defects[INSPECTION_MAX_DEFECTS];
    int      pmp_count;
    int      inj_defects[INSPECTION_MAX_DEFECTS];
    int      inj_count;
    char     note[128];
    uint32_t timestamp;      /* Time when inspection was created */
} persistent_inspection_msg_t;

/**
 * @brief Initialize the persistent inspection queue.
 * 
 * @return 0 on success, negative on error.
 */
int persistent_inspection_queue_init(void);

/**
 * @brief Store an inspection message for later transmission.
 * 
 * This function should be called by the MQTT task when it cannot send
 * an inspection due to disconnection.
 * 
 * @param msg   Inspection message to store
 * 
 * @return 0 on success, negative on error (e.g., storage full).
 */
int persistent_inspection_queue_store(const persistent_inspection_msg_t* msg);

/**
 * @brief Retrieve and remove the oldest stored inspection message.
 * 
 * This function should be called by the MQTT task when it becomes
 * connected to transmit stored inspections.
 * 
 * @param msg   Pointer to store the retrieved message
 * 
 * @return 0 on success, negative on error (e.g., no stored messages).
 */
int persistent_inspection_queue_retrieve(persistent_inspection_msg_t* msg);

/**
 * @brief Check if there are any stored inspection messages.
 * 
 * @return true if there are stored messages, false otherwise.
 */
bool persistent_inspection_queue_has_pending(void);

/**
 * @brief Clear all stored inspection messages.
 * 
 * @return 0 on success, negative on error.
 */
int persistent_inspection_queue_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PERSISTENT_INSPECTION_QUEUE_H */
