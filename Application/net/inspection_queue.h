#ifndef INSPECTION_QUEUE_H
#define INSPECTION_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file inspection_queue.h
 * @brief Queue for passing a full part inspection from the UI to the MQTT task.
 */

/* Max selectable defect types per category (12 user-defined + 1 "Autre"). */
#define INSPECTION_MAX_DEFECTS 13

/**
 * @brief One full part inspection (ADR per-part model, schema_version 4).
 *
 * Carries both categories' results for a single part: the selected
 * defect_type_ids for PMP and for INJECTION. An empty list for a category
 * means the part passed (OK) for that category. The server expands this into
 * inspection_logs rows with the correct category.
 */
typedef struct
{
    uint8_t  schema_version;                  /* = 4 */
    int      product_id;
    int      operator_id;
    int      pmp_defects[INSPECTION_MAX_DEFECTS];  /* defect_type_ids; empty = OK */
    int      pmp_count;
    int      inj_defects[INSPECTION_MAX_DEFECTS];
    int      inj_count;
    char     note[128];                        /* "Autre — préciser" free text */
    uint32_t logged_at_utc;                    /* UTC epoch at inspection time; 0 if clock not synced */
} inspection_msg_t;

/**
 * @brief Initialize the inspection queue.
 * 
 * @return 0 on success, negative on error.
 */
int inspection_queue_init(void);

/**
 * @brief Send an inspection message to the MQTT task.
 * 
 * This function should be called by the UI (via Model) when an inspection
 * needs to be logged.
 * 
 * @param msg   Inspection message to send
 * 
 * @return 0 on success, negative on error (e.g., queue full).
 */
int inspection_queue_send(const inspection_msg_t* msg);

/**
 * @brief Receive an inspection message from the queue (called by MQTT task).
 * 
 * @param msg   Pointer to store the received message
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking)
 * 
 * @return 0 on success, negative on error (e.g., timeout, empty queue).
 */
int inspection_queue_receive(inspection_msg_t* msg, uint32_t timeout_ms);

/**
 * @brief Copy the front inspection without removing it (non-blocking).
 *
 * Lets the MQTT task publish a message and only remove it (via
 * inspection_queue_receive) after the broker confirms, so a failed publish
 * keeps the message queued in RAM for the next retry instead of losing it or
 * spilling it to Octo-SPI.
 *
 * @param msg   Pointer to store the peeked message.
 *
 * @return 0 on success, negative if the queue is empty.
 */
int inspection_queue_peek(inspection_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif /* INSPECTION_QUEUE_H */
