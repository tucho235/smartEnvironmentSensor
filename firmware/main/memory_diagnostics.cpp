#include "memory_diagnostics.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void memory_diagnostics_log(const char *tag, const char *checkpoint)
{
    ESP_LOGI(tag,
             "%s memory: free_heap=%lu min_free_heap=%lu task_stack_free=%lu",
             checkpoint,
             static_cast<unsigned long>(esp_get_free_heap_size()),
             static_cast<unsigned long>(esp_get_minimum_free_heap_size()),
             static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
}
