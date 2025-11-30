#include "generic_event_group.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

event_group_handle_t event_group_create(void) {
    return xEventGroupCreate();
}

void event_group_delete(event_group_handle_t event_group) {
    vEventGroupDelete(event_group);
}

event_group_bits_t event_group_set_bits(event_group_handle_t event_group, const event_group_bits_t bits_to_set) {
    return xEventGroupSetBits(event_group, bits_to_set);
}

event_group_bits_t event_group_wait_bits(event_group_handle_t event_group, const event_group_bits_t bits_to_wait_for, const bool clear_on_exit, const bool wait_for_all_bits, const uint32_t ticks_to_wait) {
    return xEventGroupWaitBits(event_group, bits_to_wait_for, clear_on_exit, wait_for_all_bits, ticks_to_wait);
}
#endif