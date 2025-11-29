/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <unistd.h>
#include <stdint.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_SUCCESS ESP_OK
#define OS_FAIL    ESP_FAIL

typedef pthread_t TaskHandle_t;

typedef TaskHandle_t othread_t;

static inline int httpd_os_thread_create(othread_t *thread,
                                 const char *name, uint16_t stacksize, int prio,
                                 void * (*thread_routine)(void *arg), void *arg,
                                 uint8_t core_id, uint32_t caps)
{
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setstacksize(&thread_attr, stacksize);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
    int ret = pthread_create((pthread_t *)thread, &thread_attr, thread_routine, arg);
    if (ret == 0) {
        return OS_SUCCESS;
    }
    return OS_FAIL;
}

/* Only self delete is supported */
static inline void httpd_os_thread_delete(void)
{
    int x;
    pthread_exit((void *)&x);
}

static inline void httpd_os_thread_sleep(int msecs)
{
    usleep(msecs * 1000);
}

static inline othread_t httpd_os_thread_handle(void)
{
    return (othread_t)pthread_self();
}

#ifdef __cplusplus
}
#endif
