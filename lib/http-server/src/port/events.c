#include "events.h"

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void* event_data, size_t event_data_size, uint32_t ticks_to_wait)
{
    //TODO: implement
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t code)
{
    return "not implemented!";
}