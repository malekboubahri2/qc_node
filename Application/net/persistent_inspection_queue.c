#include "persistent_inspection_queue.h"
#include "stm32h7b3i_discovery_ospi.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"

/* Persistent storage */
#include "persistence/config_store.h"

/* Defines for persistent inspection queue storage */
/* Reserved by PersistentInspectionQueueSection in the linker script. */
#define PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE  0U
#define PERSISTENT_INSPECTION_QUEUE_MEMORY_BASE    0x90000000UL
#define PERSISTENT_INSPECTION_QUEUE_BASE           0x93F00000UL
#define PERSISTENT_INSPECTION_QUEUE_SIZE           0x000E0000UL
#define PERSISTENT_INSPECTION_QUEUE_FLASH_OFFSET \
    (PERSISTENT_INSPECTION_QUEUE_BASE - PERSISTENT_INSPECTION_QUEUE_MEMORY_BASE)
#define PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE    0x1000UL
#define PERSISTENT_INSPECTION_QUEUE_READY_TIMEOUT_MS 2000U

/* Define a simple circular buffer for the persistent queue */
/* In a more sophisticated implementation, we would use a proper filesystem */
/* For simplicity, we'll store messages sequentially and track read/write positions */

typedef struct
{
    uint32_t write_pos;  /* Position to write next message */
    uint32_t read_pos;   /* Position to read next message */
    uint32_t count;      /* Number of messages stored */
    uint32_t crc32;      /* CRC32 of the control structure */
} persistent_inspection_queue_control_t;

/* Size of each inspection message in the persistent storage */
#define PERSISTENT_INSPECTION_MSG_SIZE   sizeof(persistent_inspection_msg_t)

/* Control/data locations */
#define PERSISTENT_INSPECTION_QUEUE_CONTROL_OFFSET   0x00000000UL
#define PERSISTENT_INSPECTION_QUEUE_DATA_OFFSET      0x00001000UL  /* Keep control in its own erase sector */

/* Maximum number of messages we can store */
#define MAX_PERSISTENT_INSPECTION_MESSAGES \
    ((PERSISTENT_INSPECTION_QUEUE_SIZE - PERSISTENT_INSPECTION_QUEUE_DATA_OFFSET) / PERSISTENT_INSPECTION_MSG_SIZE)

static uint8_t s_queueSectorBuf[PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE];

static bool queue_ospi_is_memory_mapped(void)
{
    return Ospi_Nor_Ctx[PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE].IsInitialized == OSPI_ACCESS_MMP;
}

static uint32_t queue_indirect_addr(uint32_t queue_offset)
{
    return PERSISTENT_INSPECTION_QUEUE_FLASH_OFFSET + queue_offset;
}

static void queue_invalidate_mapped_range(uint32_t queue_offset, uint32_t len)
{
#if (__DCACHE_PRESENT == 1U)
    if (len == 0U) {
        return;
    }

    uintptr_t start = (uintptr_t)(PERSISTENT_INSPECTION_QUEUE_BASE + queue_offset);
    uintptr_t end = start + len;
    start &= ~(uintptr_t)31U;
    end = (end + 31U) & ~(uintptr_t)31U;
    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)queue_offset;
    (void)len;
#endif
}

static void queue_suspend_scheduler_if_running(bool *pSuspended)
{
    *pSuspended = false;

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskSuspendAll();
        *pSuspended = true;
    }
}

static void queue_resume_scheduler_if_suspended(bool suspended)
{
    if (suspended) {
        (void)xTaskResumeAll();
    }
}

static int queue_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    for (;;) {
        int32_t status = BSP_OSPI_NOR_GetStatus(PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE);
        if (status == BSP_ERROR_NONE) {
            return 0;
        }

        if (status != BSP_ERROR_BUSY) {
            return -1;
        }

        if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) {
            return -1;
        }

        HAL_Delay(1U);
    }
}

static int queue_enter_indirect_mode(bool *pRestoreMemoryMap)
{
    *pRestoreMemoryMap = queue_ospi_is_memory_mapped();

    if (!*pRestoreMemoryMap) {
        return 0;
    }

    if (BSP_OSPI_NOR_DisableMemoryMappedMode(PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE) != BSP_ERROR_NONE) {
        printf("persistent_inspection_queue: failed to disable OSPI memory-mapped mode\n");
        return -1;
    }

    return 0;
}

static int queue_restore_memory_mapped_mode(bool restoreMemoryMap)
{
    if (!restoreMemoryMap) {
        return 0;
    }

    if (BSP_OSPI_NOR_EnableMemoryMappedMode(PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE) != BSP_ERROR_NONE) {
        printf("persistent_inspection_queue: failed to restore OSPI memory-mapped mode\n");
        return -1;
    }

    return 0;
}

static int queue_read(uint32_t queue_offset, uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || ((queue_offset + len) > PERSISTENT_INSPECTION_QUEUE_SIZE)) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    if (queue_ospi_is_memory_mapped()) {
        queue_invalidate_mapped_range(queue_offset, len);
        memcpy(buf, (const void *)(PERSISTENT_INSPECTION_QUEUE_BASE + queue_offset), len);
        return 0;
    }

    if (BSP_OSPI_NOR_Read(PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE,
                          buf,
                          queue_indirect_addr(queue_offset),
                          len) != BSP_ERROR_NONE) {
        return -1;
    }

    return 0;
}

static int queue_program_bytes(uint32_t queue_offset, const uint8_t *buf, uint32_t len)
{
    bool suspended = false;
    bool restoreMemoryMap = false;
    int result = -1;

    if ((buf == NULL) || ((queue_offset + len) > PERSISTENT_INSPECTION_QUEUE_SIZE)) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    queue_suspend_scheduler_if_running(&suspended);

    if (queue_enter_indirect_mode(&restoreMemoryMap) != 0) {
        goto done;
    }

    if ((queue_wait_ready(PERSISTENT_INSPECTION_QUEUE_READY_TIMEOUT_MS) == 0) &&
        (BSP_OSPI_NOR_Write(PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE,
                            (uint8_t *)buf,
                            queue_indirect_addr(queue_offset),
                            len) == BSP_ERROR_NONE) &&
        (queue_wait_ready(PERSISTENT_INSPECTION_QUEUE_READY_TIMEOUT_MS) == 0)) {
        result = 0;
    }

done:
    if (queue_restore_memory_mapped_mode(restoreMemoryMap) != 0) {
        result = -1;
    }
    queue_resume_scheduler_if_suspended(suspended);

    if (result == 0) {
        queue_invalidate_mapped_range(queue_offset, len);
    }

    return result;
}

static int queue_erase_sector(uint32_t sector_offset)
{
    bool suspended = false;
    bool restoreMemoryMap = false;
    int result = -1;

    if (((sector_offset % PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE) != 0U) ||
        (sector_offset >= PERSISTENT_INSPECTION_QUEUE_SIZE)) {
        return -1;
    }

    queue_suspend_scheduler_if_running(&suspended);

    if (queue_enter_indirect_mode(&restoreMemoryMap) != 0) {
        goto done;
    }

    if ((queue_wait_ready(PERSISTENT_INSPECTION_QUEUE_READY_TIMEOUT_MS) == 0) &&
        (BSP_OSPI_NOR_Erase_Block(PERSISTENT_INSPECTION_QUEUE_OSPI_INSTANCE,
                                  queue_indirect_addr(sector_offset),
                                  BSP_OSPI_NOR_ERASE_4K) == BSP_ERROR_NONE) &&
        (queue_wait_ready(PERSISTENT_INSPECTION_QUEUE_READY_TIMEOUT_MS) == 0)) {
        result = 0;
    }

done:
    if (queue_restore_memory_mapped_mode(restoreMemoryMap) != 0) {
        result = -1;
    }
    queue_resume_scheduler_if_suspended(suspended);

    if (result == 0) {
        queue_invalidate_mapped_range(sector_offset, PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE);
    }

    return result;
}

static bool queue_can_program_without_erase(const uint8_t *current,
                                            const uint8_t *desired,
                                            uint32_t len)
{
    for (uint32_t i = 0U; i < len; i++) {
        if (((~current[i]) & desired[i]) != 0U) {
            return false;
        }
    }

    return true;
}

static bool queue_range_is_erased(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0U; i < len; i++) {
        if (buf[i] != 0xFFU) {
            return false;
        }
    }

    return true;
}

static int queue_update_sector_range(uint32_t sector_offset,
                                     uint32_t sector_rel,
                                     const uint8_t *data,
                                     uint32_t len)
{
    if ((sector_offset + PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE) > PERSISTENT_INSPECTION_QUEUE_SIZE) {
        return -1;
    }

    if ((sector_rel + len) > PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE) {
        return -1;
    }

    if (queue_read(sector_offset, s_queueSectorBuf, sizeof(s_queueSectorBuf)) != 0) {
        return -1;
    }

    if (memcmp(&s_queueSectorBuf[sector_rel], data, len) == 0) {
        return 0;
    }

    if (queue_can_program_without_erase(&s_queueSectorBuf[sector_rel], data, len)) {
        return queue_program_bytes(sector_offset + sector_rel, data, len);
    }

    memcpy(&s_queueSectorBuf[sector_rel], data, len);

    if (queue_erase_sector(sector_offset) != 0) {
        printf("persistent_inspection_queue: erase failed at offset 0x%lx\n",
               (unsigned long)sector_offset);
        return -1;
    }

    uint32_t pos = 0U;
    while (pos < PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE) {
        while ((pos < PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE) &&
               (s_queueSectorBuf[pos] == 0xFFU)) {
            pos++;
        }

        uint32_t run_start = pos;
        while ((pos < PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE) &&
               (s_queueSectorBuf[pos] != 0xFFU)) {
            pos++;
        }

        if ((pos > run_start) &&
            (queue_program_bytes(sector_offset + run_start,
                                 &s_queueSectorBuf[run_start],
                                 pos - run_start) != 0)) {
            printf("persistent_inspection_queue: program failed at offset 0x%lx\n",
                   (unsigned long)(sector_offset + run_start));
            return -1;
        }
    }

    return 0;
}

static int queue_write_region(uint32_t queue_offset,
                              uint32_t region_size,
                              const uint8_t *buf,
                              uint32_t len)
{
    uint32_t pos = 0U;

    if ((buf == NULL) ||
        (len > region_size) ||
        ((queue_offset + region_size) > PERSISTENT_INSPECTION_QUEUE_SIZE)) {
        return -1;
    }

    while (pos < len) {
        uint32_t absolute = queue_offset + pos;
        uint32_t sector_offset = absolute & ~(PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE - 1U);
        uint32_t sector_rel = absolute - sector_offset;
        uint32_t chunk = PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE - sector_rel;

        if (chunk > (len - pos)) {
            chunk = len - pos;
        }

        if (queue_update_sector_range(sector_offset, sector_rel, &buf[pos], chunk) != 0) {
            return -1;
        }

        pos += chunk;
    }

    return 0;
}

static int queue_erase_sector_if_needed(uint32_t sector_offset)
{
    if (queue_read(sector_offset, s_queueSectorBuf, sizeof(s_queueSectorBuf)) != 0) {
        return -1;
    }

    if (queue_range_is_erased(s_queueSectorBuf, sizeof(s_queueSectorBuf))) {
        return 0;
    }

    return queue_erase_sector(sector_offset);
}

static int persistent_inspection_queue_init_control(persistent_inspection_queue_control_t *ctrl)
{
    if (!ctrl) return -1;
    
    /* Initialize control structure */
    ctrl->write_pos = 0;
    ctrl->read_pos = 0;
    ctrl->count = 0;
    ctrl->crc32 = 0;
    
    /* Calculate CRC */
    ctrl->crc32 = config_store_crc32_compute((const uint8_t *)ctrl,
                                             sizeof(persistent_inspection_queue_control_t) - sizeof(ctrl->crc32));
    
    return 0;
}

static int persistent_inspection_queue_read_control(persistent_inspection_queue_control_t *ctrl)
{
    if (!ctrl) return -1;
    
    uint8_t buf[sizeof(persistent_inspection_queue_control_t)];
    
    if (queue_read(PERSISTENT_INSPECTION_QUEUE_CONTROL_OFFSET, buf, sizeof(buf)) != 0) {
        return -1;
    }
    
    memcpy(ctrl, buf, sizeof(*ctrl));
    
    /* Validate CRC */
    uint32_t stored_crc = ctrl->crc32;
    ctrl->crc32 = 0;
    uint32_t computed_crc = config_store_crc32_compute((const uint8_t *)ctrl,
                                                       sizeof(persistent_inspection_queue_control_t) - sizeof(ctrl->crc32));
    ctrl->crc32 = stored_crc;
    
    if (stored_crc != computed_crc) {
        printf("persistent_inspection_queue: CRC mismatch in control\n");
        return -1;
    }

    if ((ctrl->write_pos >= MAX_PERSISTENT_INSPECTION_MESSAGES) ||
        (ctrl->read_pos >= MAX_PERSISTENT_INSPECTION_MESSAGES) ||
        (ctrl->count > MAX_PERSISTENT_INSPECTION_MESSAGES)) {
        printf("persistent_inspection_queue: invalid control values\n");
        return -1;
    }
    
    return 0;
}

static int persistent_inspection_queue_write_control(const persistent_inspection_queue_control_t *ctrl)
{
    if (!ctrl) return -1;
    
    /* Prepare a copy with CRC */
    persistent_inspection_queue_control_t c = *ctrl;
    c.crc32 = 0;
    c.crc32 = config_store_crc32_compute((const uint8_t *)&c,
                                         sizeof(persistent_inspection_queue_control_t) - sizeof(c.crc32));
    
    if (queue_write_region(PERSISTENT_INSPECTION_QUEUE_CONTROL_OFFSET,
                           PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE,
                           (const uint8_t *)&c,
                           sizeof(c)) != 0) {
        return -1;
    }
    
    return 0;
}

int persistent_inspection_queue_init(void)
{
    persistent_inspection_queue_control_t ctrl;
    
    /* Try to read existing control structure */
    if (persistent_inspection_queue_read_control(&ctrl) == 0) {
        /* Control structure looks valid */
        printf("persistent_inspection_queue: loaded existing queue (%lu messages)\n", 
               (unsigned long)ctrl.count);
        return 0;
    }
    
    /* Control structure missing or invalid - initialize new one */
    if (persistent_inspection_queue_init_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to initialize control\n");
        return -1;
    }
    
    if (persistent_inspection_queue_write_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to write control\n");
        return -1;
    }
    
    printf("persistent_inspection_queue: initialized new queue\n");
    return 0;
}

int persistent_inspection_queue_store(const persistent_inspection_msg_t* msg)
{
    if (!msg) return -1;
    
    persistent_inspection_queue_control_t ctrl;
    if (persistent_inspection_queue_read_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to read control\n");
        return -1;
    }
    
    /* Check if queue is full */
    if (ctrl.count >= MAX_PERSISTENT_INSPECTION_MESSAGES) {
        printf("persistent_inspection_queue: queue full\n");
        return -1;
    }
    
    /* Calculate where to write the message */
    uint32_t write_offset = PERSISTENT_INSPECTION_QUEUE_DATA_OFFSET + 
                           (ctrl.write_pos * PERSISTENT_INSPECTION_MSG_SIZE);
    
    /* Write the message */
    if (queue_write_region(write_offset,
                           PERSISTENT_INSPECTION_MSG_SIZE,
                           (const uint8_t *)msg,
                           PERSISTENT_INSPECTION_MSG_SIZE) != 0) {
        printf("persistent_inspection_queue: failed to write message\n");
        return -1;
    }
    
    /* Update control structure */
    ctrl.write_pos = (ctrl.write_pos + 1) % MAX_PERSISTENT_INSPECTION_MESSAGES;
    ctrl.count++;
    
    if (persistent_inspection_queue_write_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to update control\n");
        return -1;
    }
    
    printf("persistent_inspection_queue: stored message (%lu/%lu)\n",
           (unsigned long)ctrl.count, (unsigned long)MAX_PERSISTENT_INSPECTION_MESSAGES);
    return 0;
}

int persistent_inspection_queue_retrieve(persistent_inspection_msg_t* msg)
{
    if (!msg) return -1;
    
    persistent_inspection_queue_control_t ctrl;
    if (persistent_inspection_queue_read_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to read control\n");
        return -1;
    }
    
    /* Check if queue is empty */
    if (ctrl.count == 0) {
        return -1;
    }
    
    /* Calculate where to read the message */
    uint32_t read_offset = PERSISTENT_INSPECTION_QUEUE_DATA_OFFSET + 
                          (ctrl.read_pos * PERSISTENT_INSPECTION_MSG_SIZE);
    
    /* Read the message */
    if (queue_read(read_offset, (uint8_t *)msg, PERSISTENT_INSPECTION_MSG_SIZE) != 0) {
        printf("persistent_inspection_queue: failed to read message\n");
        return -1;
    }
    
    /* Update control structure */
    ctrl.read_pos = (ctrl.read_pos + 1) % MAX_PERSISTENT_INSPECTION_MESSAGES;
    ctrl.count--;
    
    if (persistent_inspection_queue_write_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to update control\n");
        return -1;
    }
    
    printf("persistent_inspection_queue: retrieved message (%lu remaining)\n",
           (unsigned long)ctrl.count);
    return 0;
}

bool persistent_inspection_queue_has_pending(void)
{
    persistent_inspection_queue_control_t ctrl;
    if (persistent_inspection_queue_read_control(&ctrl) != 0) {
        /* If we can't read the control, assume no pending messages */
        return false;
    }
    
    return (ctrl.count > 0);
}

int persistent_inspection_queue_clear(void)
{
    persistent_inspection_queue_control_t ctrl;
    
    /* Initialize control structure to empty state */
    if (persistent_inspection_queue_init_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to initialize control\n");
        return -1;
    }
    
    /* Erase the data sectors */
    for (uint32_t sector_offset = PERSISTENT_INSPECTION_QUEUE_DATA_OFFSET;
         sector_offset < PERSISTENT_INSPECTION_QUEUE_SIZE;
         sector_offset += PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE) {
        if (queue_erase_sector_if_needed(sector_offset) != 0) {
            printf("persistent_inspection_queue: failed to erase data sector %lu\n", 
                   (unsigned long)(sector_offset / PERSISTENT_INSPECTION_QUEUE_SECTOR_SIZE));
            return -1;
        }
    }
    
    /* Write the initialized control structure */
    if (persistent_inspection_queue_write_control(&ctrl) != 0) {
        printf("persistent_inspection_queue: failed to write control\n");
        return -1;
    }
    
    printf("persistent_inspection_queue: cleared all messages\n");
    return 0;
}
