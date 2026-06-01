#include "config_store.h"
#include "stm32h7b3i_discovery_ospi.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define CONFIG_STORE_SECTOR_SIZE          0x1000U
#define CONFIG_STORE_OSPI_READY_TIMEOUT_MS 2000U

static uint8_t s_sectorBuf[CONFIG_STORE_SECTOR_SIZE];

/* Simple CRC32 for metadata validation */
uint32_t config_store_crc32_compute(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

static bool ospi_is_memory_mapped(void)
{
    return Ospi_Nor_Ctx[CONFIG_STORE_OSPI_INSTANCE].IsInitialized == OSPI_ACCESS_MMP;
}

static uint32_t indirect_addr(uint32_t store_offset)
{
    return CONFIG_STORE_FLASH_OFFSET + store_offset;
}

static void invalidate_mapped_range(uint32_t store_offset, uint32_t len)
{
#if (__DCACHE_PRESENT == 1U)
    if (len == 0U) {
        return;
    }

    uintptr_t start = (uintptr_t)(CONFIG_STORE_BASE + store_offset);
    uintptr_t end = start + len;
    start &= ~(uintptr_t)31U;
    end = (end + 31U) & ~(uintptr_t)31U;
    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)store_offset;
    (void)len;
#endif
}

static void suspend_scheduler_if_running(bool *pSuspended)
{
    *pSuspended = false;

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskSuspendAll();
        *pSuspended = true;
    }
}

static void resume_scheduler_if_suspended(bool suspended)
{
    if (suspended) {
        (void)xTaskResumeAll();
    }
}

static int wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    for (;;) {
        int32_t status = BSP_OSPI_NOR_GetStatus(CONFIG_STORE_OSPI_INSTANCE);
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

static int enter_indirect_mode(bool *pRestoreMemoryMap)
{
    *pRestoreMemoryMap = ospi_is_memory_mapped();

    if (!*pRestoreMemoryMap) {
        return 0;
    }

    if (BSP_OSPI_NOR_DisableMemoryMappedMode(CONFIG_STORE_OSPI_INSTANCE) != BSP_ERROR_NONE) {
        printf("config_store: failed to disable OSPI memory-mapped mode\n");
        return -1;
    }

    return 0;
}

static int restore_memory_mapped_mode(bool restoreMemoryMap)
{
    if (!restoreMemoryMap) {
        return 0;
    }

    if (BSP_OSPI_NOR_EnableMemoryMappedMode(CONFIG_STORE_OSPI_INSTANCE) != BSP_ERROR_NONE) {
        printf("config_store: failed to restore OSPI memory-mapped mode\n");
        return -1;
    }

    return 0;
}

static int read_region(uint32_t store_offset, uint8_t *buf, uint32_t len)
{
    if ((buf == NULL) || ((store_offset + len) > CONFIG_STORE_REGION_SIZE)) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    if (ospi_is_memory_mapped()) {
        invalidate_mapped_range(store_offset, len);
        memcpy(buf, (const void *)(CONFIG_STORE_BASE + store_offset), len);
        return 0;
    }

    int32_t ret = BSP_OSPI_NOR_Read(CONFIG_STORE_OSPI_INSTANCE,
                                    buf,
                                    indirect_addr(store_offset),
                                    len);
    return (ret == BSP_ERROR_NONE) ? 0 : -1;
}

static bool all_erased(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (buf[i] != 0xFFU) {
            return false;
        }
    }

    return true;
}

static bool can_program_without_erase(const uint8_t *current,
                                      const uint8_t *desired,
                                      uint32_t       len)
{
    for (uint32_t i = 0; i < len; i++) {
        if ((current[i] & desired[i]) != desired[i]) {
            return false;
        }
    }

    return true;
}

static int program_non_ff_locked(uint32_t store_offset,
                                 const uint8_t *buf,
                                 uint32_t len)
{
    uint32_t pos = 0U;

    while (pos < len) {
        while ((pos < len) && (buf[pos] == 0xFFU)) {
            pos++;
        }

        if (pos >= len) {
            break;
        }

        uint32_t start = pos;
        while ((pos < len) && (buf[pos] != 0xFFU)) {
            pos++;
        }

        int32_t ret = BSP_OSPI_NOR_Write(CONFIG_STORE_OSPI_INSTANCE,
                                         (uint8_t *)&buf[start],
                                         indirect_addr(store_offset + start),
                                         pos - start);
        if (ret != BSP_ERROR_NONE) {
            return -1;
        }
    }

    return 0;
}

static int update_sector_range(uint32_t sector_offset,
                               uint32_t sector_rel,
                               const uint8_t *buf,
                               uint32_t len)
{
    bool schedulerSuspended;
    bool restoreMemoryMap = false;
    int ret = 0;

    if ((buf == NULL) ||
        ((sector_offset % CONFIG_STORE_SECTOR_SIZE) != 0U) ||
        ((sector_rel + len) > CONFIG_STORE_SECTOR_SIZE)) {
        return -1;
    }

    suspend_scheduler_if_running(&schedulerSuspended);

    if (read_region(sector_offset, s_sectorBuf, sizeof(s_sectorBuf)) != 0) {
        resume_scheduler_if_suspended(schedulerSuspended);
        return -1;
    }

    if (memcmp(&s_sectorBuf[sector_rel], buf, len) == 0) {
        resume_scheduler_if_suspended(schedulerSuspended);
        return 0;
    }

    if (enter_indirect_mode(&restoreMemoryMap) != 0) {
        resume_scheduler_if_suspended(schedulerSuspended);
        return -1;
    }

    if (can_program_without_erase(&s_sectorBuf[sector_rel], buf, len)) {
        ret = program_non_ff_locked(sector_offset + sector_rel, buf, len);
        if (ret != 0) {
            printf("config_store: program failed at offset 0x%lx\n",
                   (unsigned long)(sector_offset + sector_rel));
        }
    } else {
        memcpy(&s_sectorBuf[sector_rel], buf, len);

        int32_t bsp_ret = BSP_OSPI_NOR_Erase_Block(CONFIG_STORE_OSPI_INSTANCE,
                                                   indirect_addr(sector_offset),
                                                   BSP_OSPI_NOR_ERASE_4K);
        if ((bsp_ret != BSP_ERROR_NONE) ||
            (wait_ready(CONFIG_STORE_OSPI_READY_TIMEOUT_MS) != 0)) {
            printf("config_store: erase failed at offset 0x%lx\n",
                   (unsigned long)sector_offset);
            ret = -1;
        } else {
            ret = program_non_ff_locked(sector_offset, s_sectorBuf, sizeof(s_sectorBuf));
            if (ret != 0) {
                printf("config_store: program failed at offset 0x%lx\n",
                       (unsigned long)sector_offset);
            }
        }
    }

    if (restore_memory_mapped_mode(restoreMemoryMap) != 0) {
        ret = -1;
    } else if (ret == 0) {
        invalidate_mapped_range(sector_offset, CONFIG_STORE_SECTOR_SIZE);
    }

    resume_scheduler_if_suspended(schedulerSuspended);
    return ret;
}

static int write_region(uint32_t store_offset,
                        uint32_t region_size,
                        const uint8_t *buf,
                        uint32_t len)
{
    uint32_t pos = 0U;

    if ((buf == NULL) ||
        (len > region_size) ||
        ((store_offset + region_size) > CONFIG_STORE_REGION_SIZE)) {
        return -1;
    }

    while (pos < len) {
        uint32_t abs_offset = store_offset + pos;
        uint32_t sector_offset = abs_offset & ~(CONFIG_STORE_SECTOR_SIZE - 1U);
        uint32_t sector_rel = abs_offset - sector_offset;
        uint32_t chunk = CONFIG_STORE_SECTOR_SIZE - sector_rel;

        if (chunk > (len - pos)) {
            chunk = len - pos;
        }

        if (update_sector_range(sector_offset, sector_rel, &buf[pos], chunk) != 0) {
            return -1;
        }

        pos += chunk;
    }

    return 0;
}

static int erase_4k_sector(uint32_t sector_offset)
{
    bool schedulerSuspended;
    bool restoreMemoryMap = false;
    int ret = 0;

    if (((sector_offset % CONFIG_STORE_SECTOR_SIZE) != 0U) ||
        ((sector_offset + CONFIG_STORE_SECTOR_SIZE) > CONFIG_STORE_REGION_SIZE)) {
        return -1;
    }

    suspend_scheduler_if_running(&schedulerSuspended);

    if (read_region(sector_offset, s_sectorBuf, sizeof(s_sectorBuf)) != 0) {
        resume_scheduler_if_suspended(schedulerSuspended);
        return -1;
    }

    if (all_erased(s_sectorBuf, sizeof(s_sectorBuf))) {
        resume_scheduler_if_suspended(schedulerSuspended);
        return 0;
    }

    if (enter_indirect_mode(&restoreMemoryMap) != 0) {
        resume_scheduler_if_suspended(schedulerSuspended);
        return -1;
    }

    int32_t bsp_ret = BSP_OSPI_NOR_Erase_Block(CONFIG_STORE_OSPI_INSTANCE,
                                               indirect_addr(sector_offset),
                                               BSP_OSPI_NOR_ERASE_4K);
    if ((bsp_ret != BSP_ERROR_NONE) ||
        (wait_ready(CONFIG_STORE_OSPI_READY_TIMEOUT_MS) != 0)) {
        printf("config_store: erase failed at offset 0x%lx\n",
               (unsigned long)sector_offset);
        ret = -1;
    }

    if (restore_memory_mapped_mode(restoreMemoryMap) != 0) {
        ret = -1;
    } else if (ret == 0) {
        invalidate_mapped_range(sector_offset, CONFIG_STORE_SECTOR_SIZE);
    }

    resume_scheduler_if_suspended(schedulerSuspended);
    return ret;
}

static int erase_sectors(uint32_t store_offset, uint32_t num_sectors)
{
    for (uint32_t i = 0U; i < num_sectors; i++) {
        if (erase_4k_sector(store_offset + (i * CONFIG_STORE_SECTOR_SIZE)) != 0) {
            return -1;
        }
    }

    return 0;
}

static int read_meta_internal(config_store_meta_t *meta, bool log_errors)
{
    if (meta == NULL) {
        return -1;
    }

    if (read_region(CONFIG_STORE_META_OFFSET, (uint8_t *)meta, sizeof(*meta)) != 0) {
        if (log_errors) {
            printf("config_store: read_meta failed\n");
        }
        return -1;
    }

    uint32_t stored_crc = meta->crc32;
    meta->crc32 = 0U;
    uint32_t computed_crc = config_store_crc32_compute((const uint8_t *)meta, sizeof(*meta));
    meta->crc32 = stored_crc;

    if (stored_crc != computed_crc) {
        if (log_errors) {
            printf("config_store: CRC mismatch (stored=0x%08lx, computed=0x%08lx)\n",
                   stored_crc,
                   computed_crc);
        }
        return -1;
    }

    return 0;
}

int config_store_init(void)
{
    printf("config_store: initialised at 0x%08lx (flash offset 0x%08lx)\n",
           (unsigned long)CONFIG_STORE_BASE,
           (unsigned long)CONFIG_STORE_FLASH_OFFSET);
    return 0;
}

int config_store_read_credentials(uint8_t *buf, uint16_t *len)
{
    if ((buf == NULL) || (len == NULL)) {
        return -1;
    }

    uint16_t max_len = *len;
    if (max_len > CONFIG_STORE_CREDENTIALS_SIZE) {
        max_len = CONFIG_STORE_CREDENTIALS_SIZE;
    }

    if (read_region(CONFIG_STORE_CREDENTIALS_OFFSET, buf, max_len) != 0) {
        printf("config_store: read_credentials failed\n");
        return -1;
    }

    *len = max_len;
    return 0;
}

int config_store_write_credentials(const uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len > CONFIG_STORE_CREDENTIALS_SIZE)) {
        return -1;
    }

    config_store_credentials_t creds;
    memset(&creds, 0, sizeof(creds));

    if (len > (sizeof(creds) - sizeof(creds.crc32))) {
        len = sizeof(creds) - sizeof(creds.crc32);
    }

    memcpy(&creds, buf, len);
    creds.crc32 = 0U;
    creds.crc32 = config_store_crc32_compute((const uint8_t *)&creds,
                                             sizeof(creds) - sizeof(creds.crc32));

    if (write_region(CONFIG_STORE_CREDENTIALS_OFFSET,
                     CONFIG_STORE_CREDENTIALS_SIZE,
                     (const uint8_t *)&creds,
                     sizeof(creds)) != 0) {
        printf("config_store: write_credentials failed\n");
        return -1;
    }

    printf("config_store: wrote credentials (%u input bytes)\n", len);
    return 0;
}

int config_store_clear_credentials(void)
{
    return erase_4k_sector(CONFIG_STORE_CREDENTIALS_OFFSET);
}

int config_store_read_meta(config_store_meta_t *meta)
{
    return read_meta_internal(meta, true);
}

int config_store_write_meta(const config_store_meta_t *meta)
{
    if (meta == NULL) {
        return -1;
    }

    config_store_meta_t m = *meta;
    m.crc32 = 0U;
    m.crc32 = config_store_crc32_compute((const uint8_t *)&m, sizeof(m));

    if (write_region(CONFIG_STORE_META_OFFSET,
                     CONFIG_STORE_META_SIZE,
                     (const uint8_t *)&m,
                     sizeof(m)) != 0) {
        printf("config_store: write_meta failed\n");
        return -1;
    }

    return 0;
}

int config_store_read_operators(uint8_t *buf, uint16_t *len)
{
    if ((buf == NULL) || (len == NULL)) {
        return -1;
    }

    config_store_meta_t meta;
    if ((read_meta_internal(&meta, false) != 0) ||
        (meta.operators_size == 0U) ||
        (meta.operators_size > CONFIG_STORE_OPERATORS_SIZE)) {
        *len = 0U;
        return 0;
    }

    uint16_t read_len = meta.operators_size;
    if (read_len > *len) {
        read_len = *len;
    }

    if (read_region(CONFIG_STORE_OPERATORS_OFFSET, buf, read_len) != 0) {
        printf("config_store: read_operators failed\n");
        return -1;
    }

    *len = read_len;
    return 0;
}

int config_store_write_operators(const uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len > CONFIG_STORE_OPERATORS_SIZE)) {
        return -1;
    }

    if (write_region(CONFIG_STORE_OPERATORS_OFFSET,
                     CONFIG_STORE_OPERATORS_SIZE,
                     buf,
                     len) != 0) {
        printf("config_store: write_operators failed\n");
        return -1;
    }

    printf("config_store: wrote %u bytes of operator config\n", len);
    return 0;
}

int config_store_read_products(uint8_t *buf, uint16_t *len)
{
    if ((buf == NULL) || (len == NULL)) {
        return -1;
    }

    config_store_meta_t meta;
    if ((read_meta_internal(&meta, false) != 0) ||
        (meta.products_size == 0U) ||
        (meta.products_size > CONFIG_STORE_PRODUCTS_SIZE)) {
        *len = 0U;
        return 0;
    }

    uint16_t read_len = meta.products_size;
    if (read_len > *len) {
        read_len = *len;
    }

    if (read_region(CONFIG_STORE_PRODUCTS_OFFSET, buf, read_len) != 0) {
        printf("config_store: read_products failed\n");
        return -1;
    }

    *len = read_len;
    return 0;
}

int config_store_write_products(const uint8_t *buf, uint16_t len)
{
    if ((buf == NULL) || (len > CONFIG_STORE_PRODUCTS_SIZE)) {
        return -1;
    }

    if (write_region(CONFIG_STORE_PRODUCTS_OFFSET,
                     CONFIG_STORE_PRODUCTS_SIZE,
                     buf,
                     len) != 0) {
        printf("config_store: write_products failed\n");
        return -1;
    }

    printf("config_store: wrote %u bytes of product config\n", len);
    return 0;
}

int config_store_clear_all(void)
{
    if (erase_4k_sector(CONFIG_STORE_META_OFFSET) != 0) {
        return -1;
    }

    if (erase_4k_sector(CONFIG_STORE_OPERATORS_OFFSET) != 0) {
        return -1;
    }

    uint32_t num_sectors = (CONFIG_STORE_PRODUCTS_SIZE + (CONFIG_STORE_SECTOR_SIZE - 1U)) /
                           CONFIG_STORE_SECTOR_SIZE;
    if (erase_sectors(CONFIG_STORE_PRODUCTS_OFFSET, num_sectors) != 0) {
        return -1;
    }

    if (config_store_clear_credentials() != 0) {
        return -1;
    }

    printf("config_store: cleared all\n");
    return 0;
}
