#ifndef INSPECTION_QUEUE_H
#define INSPECTION_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file inspection_queue.h
 * @brief Queue for passing inspection events from UI to MQTT task.
 */

/**
 * @brief Inspection message to be sent via MQTT.
 * 
 * This matches the format described in the documentation for schema_version 3.
 */
typedef struct
{
    uint8_t  schema_version; /* = 3 (ADR-014) */
    char     outcome[8];     /* "DEFECT" or "OK" */
    int      product_id;
    int      operator_id;
    int      defect_type_id; /* -1 if OK */
    char     note[128];
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

#ifdef __cplusplus
}
#endif

#endif /* INSPECTION_QUEUE_H */
