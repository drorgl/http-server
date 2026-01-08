#ifndef TEST_HTTP_METHODS_H
#define TEST_HTTP_METHODS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test PUT method implementation and behavior
 *
 * Tests that PUT requests with and without bodies work correctly:
 * - PUT requests with body data are handled properly
 * - PUT requests return appropriate status codes
 * - PUT method validation and Content-Length handling
 */
void given_server_with_put_handler_when_client_sends_put_request_then_server_handles_correctly(void);

/**
 * @brief Test DELETE method implementation
 *
 * Tests that DELETE requests work correctly:
 * - DELETE requests are processed without body expectations
 * - DELETE method returns appropriate status codes
 * - DELETE method validation
 */
void given_server_with_delete_handler_when_client_sends_delete_request_then_server_handles_correctly(void);

/**
 * @brief Test HEAD method implementation
 *
 * Tests that HEAD requests return headers but no body:
 * - HEAD requests return same headers as GET
 * - HEAD requests return empty or no body
 * - HEAD method validation and response format
 */
void given_server_with_head_handler_when_client_sends_head_request_then_server_returns_headers_only(void);

/**
 * @brief Test PUT method not allowed responses
 *
 * Tests that servers return 405 Method Not Allowed when PUT is used on GET-only endpoints:
 * - PUT requests to URI with only GET handler return 405
 * - Proper status text is returned
 */
void given_server_with_get_only_handler_when_client_sends_put_then_405_method_not_allowed(void);

/**
 * @brief Test DELETE method not allowed responses
 *
 * Tests that servers return 405 Method Not Allowed when DELETE is used on GET-only endpoints:
 * - DELETE requests to URI with only GET handler return 405
 * - Proper status text is returned
 */
void given_server_with_get_only_handler_when_client_sends_delete_then_405_method_not_allowed(void);

/**
 * @brief Test HEAD method not allowed responses
 *
 * Tests that servers return 405 Method Not Allowed when HEAD is used on GET-only endpoints:
 * - HEAD requests to URI with only GET handler return 405
 * - Proper status text is returned
 */
void given_server_with_get_only_handler_when_client_sends_head_then_405_method_not_allowed(void);

/**
 * @brief Main test function for HTTP methods testing
 *
 * Runs all HTTP method tests in sequence. This should be called from the main test runner.
 */
int test_http_methods(void);

#ifdef __cplusplus
}
#endif

#endif /* TEST_HTTP_METHODS_H */
