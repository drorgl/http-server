#ifndef EVENT_GROUP_H
#define EVENT_GROUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void* event_group_handle_t;
typedef uint32_t event_group_bits_t;

event_group_handle_t event_group_create(void);
void event_group_delete(event_group_handle_t event_group);
event_group_bits_t event_group_set_bits(event_group_handle_t event_group, const event_group_bits_t bits_to_set);
event_group_bits_t event_group_wait_bits(event_group_handle_t event_group, const event_group_bits_t bits_to_wait_for, const bool clear_on_exit, const bool wait_for_all_bits, const uint32_t ticks_to_wait);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_GROUP_H */
