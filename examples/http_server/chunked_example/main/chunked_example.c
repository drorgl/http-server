/*
 * SPDX-FileCopyrightText: Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "http_server.h"
#include "httpd_chunked.h"

httpd_handle_t server = NULL;

static esp_err_t chunked_upload_handler(httpd_req_t *req)
{
    httpd_chunked_ctx_t ctx = {0};
    esp_err_t err = httpd_parse_chunked_request(req, &ctx);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid chunked data");
        return ESP_FAIL;
    }

    char buf[512];
    size_t bytes_read;
    int total = 0;
    while (1) {
        err = httpd_read_chunk(req, &ctx, buf, sizeof(buf), &bytes_read);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Chunk read error");
            return ESP_FAIL;
        }
        total += bytes_read;
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "Uploaded %d bytes via chunked", total);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t chunked_response_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_start_chunked_response(req, "text/plain", NULL);

    const char *chunks[] = {"First ", "second ", "third ", "chunk!"};
    for (int i = 0; i < 4; i++) {
        httpd_send_chunk(req, chunks[i], strlen(chunks[i]), NULL);
        vTaskDelay(pdMS_TO_TICKS(100)); // Simulate streaming
    }

    httpd_end_chunked_response(req, NULL);
    return ESP_OK;
}

void app_main(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&server, &config);

    httpd_uri_t upload = {
        .uri = "/upload",
        .method = HTTP_POST,
        .handler = chunked_upload_handler,
    };
    httpd_register_uri_handler(server, &upload);

    httpd_uri_t resp = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = chunked_response_handler,
    };
    httpd_register_uri_handler(server, &resp);

    printf("Chunked example server started\n");
    printf("Upload chunked: POST /upload\n");
    printf("Stream chunked: GET /stream\n");
}
