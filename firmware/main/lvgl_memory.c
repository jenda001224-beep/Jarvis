#include "esp_heap_caps.h"
#include "lvgl.h"

#define LVGL_MEMORY (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *memory, size_t bytes)
{
    (void) memory;
    (void) bytes;
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    (void) pool;
}

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, LVGL_MEMORY);
}

void *lv_realloc_core(void *pointer, size_t size)
{
    return heap_caps_realloc(pointer, size, LVGL_MEMORY);
}

void lv_free_core(void *pointer)
{
    heap_caps_free(pointer);
}

void lv_mem_monitor_core(lv_mem_monitor_t *monitor)
{
    (void) monitor;
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}
