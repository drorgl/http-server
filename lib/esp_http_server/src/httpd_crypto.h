#pragma once
#include <stdint.h>
#include <stddef.h>
#include "port/events.h"

esp_err_t httpd_crypto_sha1(const uint8_t *data, size_t data_len, uint8_t *hash);