#include "inspection_queue.h"
#include <string.h>
#include <stdio.h>

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

// Queue handle and storage
static QueueHandle_t s_inspectionQueue = NULL;
/* Buffers parts while offline. Messages stay here until the broker confirms,
 * so a transient disconnect never touches Octo-SPI; sized for a comfortable
 * offline burst. */
#define INSPECTION_QUEUE_LENGTH   32
#define INSPECTION_QUEUE_ITEM_SIZE sizeof(inspection_msg_t)

// Storage area for the queue items (if using static allocation)
// Alternatively, we could use dynamic allocation
static uint8_t s_queueStorage[INSPECTION_QUEUE_LENGTH * INSPECTION_QUEUE_ITEM_SIZE];
static StaticQueue_t s_queueBuffer;

int inspection_queue_init(void)
{
    // Create the queue
    s_inspectionQueue = xQueueCreateStatic(
        INSPECTION_QUEUE_LENGTH,
        INSPECTION_QUEUE_ITEM_SIZE,
        s_queueStorage,
        &s_queueBuffer);
    
    if (s_inspectionQueue == NULL)
    {
        printf("inspection_queue: failed to create queue\n");
        return -1;
    }
    
    printf("inspection_queue: initialized (length=%d)\n", INSPECTION_QUEUE_LENGTH);
    return 0;
}

int inspection_queue_send(const inspection_msg_t* msg)
{
    if (!msg || !s_inspectionQueue)
        return -1;
    
    // Send to queue (non-blocking)
    if (xQueueSend(s_inspectionQueue, msg, 0) != pdTRUE)
    {
        printf("inspection_queue: send failed (queue full)\n");
        return -1;
    }
    
    printf("inspection_queue: sent inspection (product=%d, pmp=%d, inj=%d)\n",
           msg->product_id, msg->pmp_count, msg->inj_count);
    return 0;
}

int inspection_queue_receive(inspection_msg_t* msg, uint32_t timeout_ms)
{
    if (!msg || !s_inspectionQueue)
        return -1;
    
    // Receive from queue
    if (xQueueReceive(s_inspectionQueue, msg, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    {
        // Queue empty or timeout
        return -1;
    }
    
    printf("inspection_queue: received inspection (product=%d, pmp=%d, inj=%d)\n",
           msg->product_id, msg->pmp_count, msg->inj_count);
    return 0;
}

int inspection_queue_peek(inspection_msg_t* msg)
{
    if (!msg || !s_inspectionQueue)
        return -1;

    /* Copy the front item without removing it (non-blocking). */
    if (xQueuePeek(s_inspectionQueue, msg, 0) != pdTRUE)
        return -1;

    return 0;
}