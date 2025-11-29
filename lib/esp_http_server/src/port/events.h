/*
 * SPDX-FileCopyrightText: 2018-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG         0x102
#define ESP_FAIL        -1      /*!< Generic esp_err_t code indicating failure */
#define ESP_ERR_INVALID_SIZE        0x104   /*!< Invalid size */
#define ESP_ERR_INVALID_VERSION     0x10A   /*!< Version was invalid */
#define ESP_ERR_INVALID_STATE       0x103   /*!< Invalid state */
#define ESP_ERR_NOT_FOUND           0x105   /*!< Requested resource not found */
#define ESP_ERR_NO_MEM              0x101   /*!< Out of memory */

// Defines for declaring and defining event base
#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t const id = #id

// Event loop library types
typedef const char*  esp_event_base_t; /**< unique pointer to a subsystem that exposes events */
typedef void*        esp_event_loop_handle_t; /**< a number that identifies an event with respect to a base */
typedef void (*esp_event_handler_t)(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data); /**< function called when an event is posted to the queue */
typedef void*        esp_event_handler_instance_t; /**< context identifying an instance of a registered event handler */

// Defines for registering/unregistering event handlers
#define ESP_EVENT_ANY_BASE     NULL             /**< register handler for any event base */
#define ESP_EVENT_ANY_ID       -1               /**< register handler for any event id */

esp_err_t esp_event_post(esp_event_base_t event_base, int32_t event_id,
                         const void* event_data, size_t event_data_size, uint32_t ticks_to_wait);

const char *esp_err_to_name(esp_err_t code);

#ifdef __cplusplus
}

#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define __COMPILER_PRAGMA__(string) _Pragma(#string)
#define _COMPILER_PRAGMA_(string) __COMPILER_PRAGMA__(string)

#if __clang__
#define ESP_COMPILER_DIAGNOSTIC_PUSH_IGNORE(warning) \
    __COMPILER_PRAGMA__(clang diagnostic push) \
    __COMPILER_PRAGMA__(clang diagnostic ignored "-Wunknown-warning-option") \
    __COMPILER_PRAGMA__(clang diagnostic ignored warning)
#define ESP_COMPILER_DIAGNOSTIC_POP(warning) \
    __COMPILER_PRAGMA__(clang diagnostic pop)
#elif __GNUC__
#define ESP_COMPILER_DIAGNOSTIC_PUSH_IGNORE(warning) \
    __COMPILER_PRAGMA__(GCC diagnostic push) \
    __COMPILER_PRAGMA__(GCC diagnostic ignored "-Wpragmas") \
    __COMPILER_PRAGMA__(GCC diagnostic ignored warning)
#define ESP_COMPILER_DIAGNOSTIC_POP(warning) \
    __COMPILER_PRAGMA__(GCC diagnostic pop)
#else
#define ESP_COMPILER_DIAGNOSTIC_PUSH_IGNORE(warning)
#define ESP_COMPILER_DIAGNOSTIC_POP(warning)
#endif