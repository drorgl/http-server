#include "event_group.h"
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    event_group_bits_t bits;
} event_group_t;

event_group_handle_t event_group_create(void) {
    event_group_t* group = (event_group_t*)malloc(sizeof(event_group_t));
    if (!group) {
        return NULL;
    }

    pthread_mutex_init(&group->mutex, NULL);
    pthread_cond_init(&group->cond, NULL);
    group->bits = 0;
    return (event_group_handle_t)group;
}

void event_group_delete(event_group_handle_t event_group) {
    event_group_t* group = (event_group_t*)event_group;
    pthread_mutex_destroy(&group->mutex);
    pthread_cond_destroy(&group->cond);
    free(group);
}

event_group_bits_t event_group_set_bits(event_group_handle_t event_group, const event_group_bits_t bits_to_set) {
    event_group_t* group = (event_group_t*)event_group;
    pthread_mutex_lock(&group->mutex);
    group->bits |= bits_to_set;
    pthread_cond_broadcast(&group->cond);
    pthread_mutex_unlock(&group->mutex);
    return group->bits;
}

event_group_bits_t event_group_wait_bits(event_group_handle_t event_group, const event_group_bits_t bits_to_wait_for, const bool clear_on_exit, const bool wait_for_all_bits, const uint32_t ticks_to_wait) {
    event_group_t* group = (event_group_t*)event_group;
    pthread_mutex_lock(&group->mutex);

    event_group_bits_t return_bits = 0;

    while (1) {
        bool condition_met = wait_for_all_bits ?
            ((group->bits & bits_to_wait_for) == bits_to_wait_for) :
            ((group->bits & bits_to_wait_for) != 0);

        if (condition_met) {
            return_bits = group->bits;
            if (clear_on_exit) {
                group->bits &= ~bits_to_wait_for;
            }
            break;
        }

        if (ticks_to_wait == 0) {
            return_bits = group->bits;
            break;
        }

        // Note: A full implementation would convert 'ticks_to_wait' to a timespec for pthread_cond_timedwait.
        // For this example, we'll use an indefinite wait.
        int wait_result = pthread_cond_wait(&group->cond, &group->mutex);
        if (wait_result != 0) {
            return_bits = group->bits;
            break;
        }
    }

    pthread_mutex_unlock(&group->mutex);
    return return_bits;
}
