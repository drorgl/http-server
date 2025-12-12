#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <http_server_middleware.h>

// Mock URI match wildcard function for testing
static bool mock_uri_match_wildcard(const char *uri_template, const char *uri_to_match, size_t match_upto) {
    // Simple wildcard implementation for test purposes
    if (strcmp(uri_template, uri_to_match) == 0) {
        return true; // Exact match
    }

    // Simple wildcard: "/api/*" matches "/api/anything"
    size_t template_len = strlen(uri_template);
    if (template_len > 1 && uri_template[template_len - 1] == '*') {
        size_t prefix_len = template_len - 1;
        if (strncmp(uri_template, uri_to_match, prefix_len) == 0) {
            return true;
        }
    }

    return false;
}

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
            .enabled = true,
            .uri_match_wildcard = mock_uri_match_wildcard
        },
        {
            .func = test_middleware_with_context,
            .context = (void*)"exact_match",
            .free_ctx = NULL,
            .priority = 2,  // Ignored
            .uri_pattern = "/api/data",  // Should match
            .method_filter = HTTP_ANY,
            .enabled = true,
            .uri_match_wildcard = mock_uri_match_wildcard
        },
        {
            .func = test_middleware_with_context,
            .context = (void*)"no_match",
            .free_ctx = NULL,
            .priority = 3,  // Ignored
            .uri_pattern = "/other/*",  // Should NOT match
            .method_filter = HTTP_ANY,
            .enabled = true,
            .uri_match_wildcard = mock_uri_match_wildcard
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

int test_framework() {
    // UNITY_BEGIN();
    UnitySetTestFile(__FILE__);

    RUN_TEST(test_middleware_basic_wrapper);
    RUN_TEST(test_middleware_execution_order);
    RUN_TEST(test_short_circuit_behavior);
    RUN_TEST(test_middleware_disabled);
    RUN_TEST(test_middleware_uri_pattern_filtering);
    RUN_TEST(test_middleware_null_inputs);

    // return UNITY_END();
    return 0;
}
