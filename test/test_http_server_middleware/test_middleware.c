#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <http_server_middleware.h>

// Test context to track middleware execution
static int middleware_call_count = 0;
static char middleware_log[256] = {0};

// Helper function to reset test context
static void reset_test_context(void) {
    middleware_call_count = 0;
    memset(middleware_log, 0, sizeof(middleware_log));
}

// Sample middleware functions for testing
static esp_err_t test_middleware_count_calls(httpd_req_t *req, const httpd_uri_t *uri, void *ctx) {
    middleware_call_count++;
    sprintf(middleware_log + strlen(middleware_log), "called_%d ", middleware_call_count);
    return ESP_OK;
}

static esp_err_t test_middleware_short_circuit(httpd_req_t *req, const httpd_uri_t *uri, void *ctx) {
    middleware_call_count++;
    sprintf(middleware_log + strlen(middleware_log), "short_circuit ");
    return ESP_FAIL; // Short-circuit
}

static esp_err_t test_middleware_with_context(httpd_req_t *req, const httpd_uri_t *uri, void *ctx) {
    const char *context_str = (const char *)ctx;
    middleware_call_count++;
    sprintf(middleware_log + strlen(middleware_log), "%s ", context_str);
    return ESP_OK;
}

// Original handler for testing
static esp_err_t original_handler(httpd_req_t *req) {
    sprintf(middleware_log + strlen(middleware_log), "original ");
    return ESP_OK;
}

// Test mocking: Create mock httpd_req_t with minimal structure
static httpd_req_t create_mock_req(const char *uri_str, httpd_method_t method) {
    httpd_req_t req;
    memset(&req, 0, sizeof(httpd_req_t));
    strcpy((char *)req.uri, uri_str);
    req.method = method;
    req.user_ctx = NULL;

    return req;
}

// Mock httpd_req_get_hdr_value_str and httpd_resp_set_hdr for CORS testing
static int mock_get_hdr_call_count = 0;
static char mock_origin_header[128] = "";
static char mock_request_headers[32][128]; // Store up to 32 headers
static int mock_header_count = 0;
static char mock_response_headers[32][128];
static char mock_response_values[32][128];
static char mock_response_body[1024] = "";
static int mock_response_status = 0;

// Mock implementations for CORS testing
esp_err_t mock_httpd_req_get_hdr_value_str(httpd_req_t *req, const char *hdr_name, char *val, size_t val_size) {
    mock_get_hdr_call_count++;
    if (strcmp(hdr_name, "Origin") == 0) {
        if (strlen(mock_origin_header) > 0) {
            strncpy(val, mock_origin_header, val_size - 1);
            val[val_size - 1] = '\0';
            return ESP_OK;
        }
    }
    return ESP_FAIL; // Header not found
}

esp_err_t mock_httpd_resp_set_hdr(httpd_req_t *req, const char *hdr_name, const char *hdr_value) {
    if (mock_header_count < 32) {
        strcpy(mock_response_headers[mock_header_count], hdr_name);
        strcpy(mock_response_values[mock_header_count], hdr_value);
        mock_header_count++;
    }
    return ESP_OK;
}

esp_err_t mock_httpd_resp_set_status(httpd_req_t *req, const char *status) {
    mock_response_status = atoi(status);
    return ESP_OK;
}

esp_err_t mock_httpd_resp_send(httpd_req_t *req, const char *buf, size_t buf_len) {
    if (buf) {
        strncpy(mock_response_body, buf, sizeof(mock_response_body) - 1);
    }
    return ESP_OK;
}

void reset_cors_mocks() {
    mock_get_hdr_call_count = 0;
    memset(mock_origin_header, 0, sizeof(mock_origin_header));
    mock_header_count = 0;
    memset(mock_response_headers, 0, sizeof(mock_response_headers));
    memset(mock_response_values, 0, sizeof(mock_response_values));
    memset(mock_response_body, 0, sizeof(mock_response_body));
    mock_response_status = 0;
}

// Helper to find header in response
static const char* get_response_header(const char *hdr_name) {
    for (int i = 0; i < mock_header_count; i++) {
        if (strcmp(mock_response_headers[i], hdr_name) == 0) {
            return mock_response_values[i];
        }
    }
    return NULL;
}

// CORS-specific test handler that simulates receiving CORS headers
static esp_err_t cors_test_handler(httpd_req_t *req) {
    // This handler simulates what an application might do - process the request
    // In a real scenario, CORS headers would be set by the middleware before reaching here
    sprintf(middleware_log + strlen(middleware_log), "cors_handler_called ");
    return ESP_OK;
}

// Override the httpd_req_get_hdr_value_str function for CORS testing
#define httpd_req_get_hdr_value_str mock_httpd_req_get_hdr_value_str
#define httpd_resp_set_hdr mock_httpd_resp_set_hdr
#define httpd_resp_set_status mock_httpd_resp_set_status
#define httpd_resp_send_err mock_httpd_resp_send_err
#define httpd_resp_send mock_httpd_resp_send

// Mock error sending function
esp_err_t mock_httpd_resp_send_err(httpd_req_t *req, unsigned short status, const char *message) {
    mock_response_status = status;
    strcpy(mock_response_body, message);
    return ESP_OK;
}

void setUp() {
    reset_test_context();
}

void tearDown() {
    // Cleanup after tests if needed
}

/* Test 1: Basic wrapper creation and execution */
void test_middleware_basic_wrapper(void) {
    // Arrange
    reset_test_context();

    httpd_uri_t original_uri = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = original_handler,
        .user_ctx = NULL
    };

    // Simple middleware config
    httpd_middleware_config_t config = {
        .func = test_middleware_count_calls,
        .context = NULL,
        .free_ctx = NULL,
        .priority = 1,
        .uri_pattern = NULL,
        .method_filter = HTTP_ANY,
        .enabled = true
    };

    // Act
    httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&original_uri, &config, 1);
    TEST_ASSERT_NOT_NULL(wrapped_uri);

    // Create mock request and execute wrapped handler
    httpd_req_t req = create_mock_req("/test", HTTP_GET);

    // Mock the user_ctx to point to the context (simulating what httpd_register_uri_handler would do)
    wrapped_handler_ctx_t test_ctx = {
        .original_uri = &original_uri,
        .configs = &config,
        .num_configs = 1
    };
    req.user_ctx = &test_ctx;

    esp_err_t result = wrapped_uri->handler(&req);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("called_1 original ", middleware_log);
    TEST_ASSERT_EQUAL(1, middleware_call_count);

    // Cleanup
    free(wrapped_uri);
}

/* Test 2: Multiple middleware execution order (registration order) */
void test_middleware_execution_order(void) {
    // Arrange
    reset_test_context();

    httpd_uri_t original_uri = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = original_handler,
        .user_ctx = NULL
    };

    httpd_middleware_config_t configs[3] = {
        {
            .func = test_middleware_with_context,
            .context = (void*)"first",
            .free_ctx = NULL,
            .priority = 10,  // This field is ignored - execution order is registration order
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = true
        },
        {
            .func = test_middleware_with_context,
            .context = (void*)"second",
            .free_ctx = NULL,
            .priority = 1,   // This field is ignored - execution order is registration order
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = true
        },
        {
            .func = test_middleware_with_context,
            .context = (void*)"third",
            .free_ctx = NULL,
            .priority = 100, // This field is ignored - execution order is registration order
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = true
        }
    };

    // Act
    httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&original_uri, configs, 3);
    TEST_ASSERT_NOT_NULL(wrapped_uri);

    wrapped_handler_ctx_t test_ctx = {
        .original_uri = &original_uri,
        .configs = configs,
        .num_configs = 3
    };
    httpd_req_t req = create_mock_req("/test", HTTP_GET);
    req.user_ctx = &test_ctx;

    esp_err_t result = wrapped_uri->handler(&req);

    // Assert - middleware execute in registration order (first, second, third)
    // Priority values are ignored - order is determined by array position
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("first second third original ", middleware_log);

    // Cleanup
    free(wrapped_uri);
}

/* Test 3: Short-circuit behavior */
void test_short_circuit_behavior(void) {
    // Arrange
    reset_test_context();

    httpd_uri_t original_uri = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = original_handler,
        .user_ctx = NULL
    };

    httpd_middleware_config_t configs[3] = {
        {
            .func = test_middleware_count_calls,
            .context = NULL,
            .free_ctx = NULL,
            .priority = 1,
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = true
        },
        {
            .func = test_middleware_short_circuit,
            .context = NULL,
            .free_ctx = NULL,
            .priority = 2,
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = true
        },
        // Third middleware would be skipped
        {
            .func = test_middleware_count_calls,
            .context = NULL,
            .free_ctx = NULL,
            .priority = 3,
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = true
        }
    };

    // Act
    httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&original_uri, configs, 3);
    TEST_ASSERT_NOT_NULL(wrapped_uri);

    wrapped_handler_ctx_t test_ctx = {
        .original_uri = &original_uri,
        .configs = configs,
        .num_configs = 3
    };
    httpd_req_t req = create_mock_req("/test", HTTP_GET);
    req.user_ctx = &test_ctx;

    esp_err_t result = wrapped_uri->handler(&req);

    // Assert - execution stops at short-circuit, original handler never called
    TEST_ASSERT_EQUAL(ESP_FAIL, result);
    TEST_ASSERT_EQUAL_STRING("called_1 short_circuit ", middleware_log);
    TEST_ASSERT_EQUAL(2, middleware_call_count); // First + second middleware

    // Cleanup
    free(wrapped_uri);
}

/* Test 4: Disabled middleware filtering */
void test_middleware_disabled(void) {
    // Arrange
    reset_test_context();

    httpd_uri_t original_uri = {
        .uri = "/test",
        .method = HTTP_GET,
        .handler = original_handler,
        .user_ctx = NULL
    };

    httpd_middleware_config_t configs[2] = {
        {
            .func = test_middleware_count_calls,
            .context = NULL,
            .free_ctx = NULL,
            .priority = 1,
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = true    // Enabled
        },
        {
            .func = test_middleware_count_calls,
            .context = NULL,
            .free_ctx = NULL,
            .priority = 2,
            .uri_pattern = NULL,
            .method_filter = HTTP_ANY,
            .enabled = false   // Disabled - should be skipped
        }
    };

    // Act
    httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&original_uri, configs, 2);
    TEST_ASSERT_NOT_NULL(wrapped_uri);

    wrapped_handler_ctx_t test_ctx = {
        .original_uri = &original_uri,
        .configs = configs,
        .num_configs = 2
    };
    httpd_req_t req = create_mock_req("/test", HTTP_GET);
    req.user_ctx = &test_ctx;

    esp_err_t result = wrapped_uri->handler(&req);

    // Assert - only enabled middleware runs
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("called_1 original ", middleware_log);
    TEST_ASSERT_EQUAL(1, middleware_call_count);

    // Cleanup
    free(wrapped_uri);
}

/* Test 5: URI pattern filtering */
void test_middleware_uri_pattern_filtering(void) {
    // Arrange
    reset_test_context();

    httpd_uri_t original_uri = {
        .uri = "/api/data",
        .method = HTTP_GET,
        .handler = original_handler,
        .user_ctx = NULL
    };

    httpd_middleware_config_t configs[3] = {
        {
            .func = test_middleware_with_context,
            .context = (void*)"wildcard_match",
            .free_ctx = NULL,
            .priority = 1,  // Ignored
            .uri_pattern = "/api/*",  // Should match
            .method_filter = HTTP_ANY,
            .enabled = true
        },
        {
            .func = test_middleware_with_context,
            .context = (void*)"exact_match",
            .free_ctx = NULL,
            .priority = 2,  // Ignored
            .uri_pattern = "/api/data",  // Should match
            .method_filter = HTTP_ANY,
            .enabled = true
        },
        {
            .func = test_middleware_with_context,
            .context = (void*)"no_match",
            .free_ctx = NULL,
            .priority = 3,  // Ignored
            .uri_pattern = "/other/*",  // Should NOT match
            .method_filter = HTTP_ANY,
            .enabled = true
        }
    };

    // Act
    httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&original_uri, configs, 3);
    TEST_ASSERT_NOT_NULL(wrapped_uri);

    wrapped_handler_ctx_t test_ctx = {
        .original_uri = &original_uri,
        .configs = configs,
        .num_configs = 3
    };
    httpd_req_t req = create_mock_req("/api/data", HTTP_GET);
    req.user_ctx = &test_ctx;

    esp_err_t result = wrapped_uri->handler(&req);

    // Assert - only matching patterns execute middleware
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("wildcard_match exact_match original ", middleware_log);

    // Cleanup
    free(wrapped_uri);
}

/* Test 6: Error handling - null inputs */
void test_middleware_null_inputs(void) {
    // Test NULL original_uri
    httpd_uri_t *result = httpd_uri_wrap_with_middleware(NULL, NULL, 1);
    TEST_ASSERT_NULL(result);

    // Test NULL configs with count > 0
    httpd_uri_t original_uri = {0};
    result = httpd_uri_wrap_with_middleware(&original_uri, NULL, 1);
    TEST_ASSERT_NULL(result);

    // Test zero configs
    result = httpd_uri_wrap_with_middleware(&original_uri, NULL, 0);
    TEST_ASSERT_NULL(result);
}

/* Test 7: Integration test with CORS middleware */
void test_middleware_cors_integration(void) {
    reset_test_context();

    httpd_uri_t original_uri = {
        .uri = "/api/test",
        .method = HTTP_GET,
        .handler = cors_test_handler,
        .user_ctx = NULL
    };

    // CORS configuration allowing all origins
    cors_config_t cors_config = {
        .allowed_origins = "*",  // Allow any origin for testing
        .allowed_methods = "GET,POST,OPTIONS",
        .allowed_headers = "Content-Type,Authorization",
        .allow_credentials = false,
        .max_age = 3600
    };

    // Create middleware config for CORS
    httpd_middleware_config_t cors_middleware_config = {
        .func = middleware_cors,
        .context = &cors_config,
        .free_ctx = NULL,
        .priority = 1,
        .uri_pattern = "/api/*",  // Apply CORS to API endpoints
        .method_filter = HTTP_ANY,
        .enabled = true
    };

    // Wrap the handler with CORS middleware
    httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&original_uri, &cors_middleware_config, 1);
    TEST_ASSERT_NOT_NULL(wrapped_uri);

    // Note: Full CORS testing would require either:
    // 1. Running against a real ESP HTTP server with http_test_client, or
    // 2. More sophisticated mocking of HTTP functions
    // For POC, we verify the wrapper was created successfully

    // Cleanup
    free(wrapped_uri);

    printf("CORS integration test completed - wrapper creation successful\n");
}

int test_middleware() {
    UNITY_BEGIN();

    RUN_TEST(test_middleware_basic_wrapper);
    RUN_TEST(test_middleware_execution_order);
    RUN_TEST(test_short_circuit_behavior);
    RUN_TEST(test_middleware_disabled);
    RUN_TEST(test_middleware_uri_pattern_filtering);
    RUN_TEST(test_middleware_null_inputs);
    RUN_TEST(test_middleware_cors_integration);

    return UNITY_END();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering for stdout
    setvbuf(stderr, NULL, _IONBF, 0); // Disable buffering for stderr
    return test_middleware();
}

void app_main(void) {
    main();
}
