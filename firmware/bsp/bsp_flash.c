#include <string.h>
#include "stm32g4xx_hal.h"
#include "bsp.h"

static int f_read(void *c, uint32_t addr, uint8_t *out, size_t len) { (void)c; memcpy(out, (const void *)addr, len); return 0; }

static int f_erase(void *c, uint32_t addr) {
    (void)c;
    if (addr < FB_FLASH_BASE || addr % FB_FLASH_PAGE) return -1;
    FLASH_EraseInitTypeDef e = { .TypeErase = FLASH_TYPEERASE_PAGES, .Banks = FLASH_BANK_1,
                                 .Page = (addr - FB_FLASH_BASE) / FB_FLASH_PAGE, .NbPages = 1 };
    uint32_t err = 0;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&e, &err);
    HAL_FLASH_Lock();
    return s == HAL_OK ? 0 : -1;
}

static int f_write(void *c, uint32_t addr, const uint8_t *d, size_t len) {
    (void)c;
    if (addr % 8 || len % 8) return -1;
    HAL_FLASH_Unlock();
    for (size_t i = 0; i < len; i += 8) {
        uint64_t w; memcpy(&w, d + i, 8);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + i, w) != HAL_OK) { HAL_FLASH_Lock(); return -1; }
    }
    HAL_FLASH_Lock();
    return 0;
}

static const fb_flash_t g_flash = { f_read, f_erase, f_write, 0 };
const fb_flash_t *bsp_flash(void) { return &g_flash; }
