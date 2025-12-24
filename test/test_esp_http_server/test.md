# ESP HTTP Server Test Documentation

## Overview

This document describes the organization and structure of the ESP HTTP Server tests. The tests have been categorized into logical groups to improve maintainability, readability, and ease of execution.

## Recent Updates

**HTTP Methods Tests Added** - December 2025
- New test category for HTTP Methods compliance (RFC 1945)
- Addresses critical gap identified in standards.md for PUT/DELETE/HEAD methods
- Tests PUT requests with body data, DELETE requests, HEAD requests returning headers only
- Validates 405 Method Not Allowed responses for unsupported methods
- Extends test client infrastructure to support all HTTP methods

**Authentication Tests Added** - December 2025
- New test category for HTTP Authentication (RFC 9110 Part 11)
- Covers WWW-Authenticate, Authorization, and Authentication-Info headers
- Tests Basic authentication, malformed headers, and multiple auth schemes

**Security Tests Added** - December 2025
- New test category for HTTP Security compliance (RFC 9112 Section 11)
- Addresses critical gap identified in standards.md for security vulnerabilities
- Tests response splitting, header injection, CRLF injection, and request smuggling prevention

## Test Organization Philosophy

The tests are organized by functionality area to:
- Make it easier to find relevant tests for specific features
- Allow running specific test categories independently
- Improve code maintainability and reduce file sizes
- Follow the single responsibility principle for test files

## Test Categories

### 1. Server Lifecycle Tests (`test_server_lifecycle.cpp`)

**Purpose**: Tests for server initialization, configuration, and shutdown functionality.

**Tests Included**:
- `given_valid_httpd_config_when_httpd_start_is_called_then_returns_success` - Verifies server starts with valid config
- `given_null_handle_when_httpd_start_is_called_then_returns_invalid_arg` - Tests null handle error handling
- `given_null_config_when_httpd_start_is_called_then_returns_invalid_arg` - Tests null config error handling
- `given_started_server_when_httpd_stop_is_called_then_server_stops` - Verifies proper server shutdown
- `given_null_handle_when_httpd_stop_is_called_then_returns_invalid_arg` - Tests null handle error in stop
- `given_started_server_when_calling_httpd_stop_multiple_times_then_handles_gracefully` - Tests multiple stop calls
- `given_zero_port_when_httpd_start_is_called_then_assigns_random_port_and_returns_success` - Tests random port assignment

**What They Test**: Server startup validation, proper resource cleanup, error handling for invalid parameters, and configuration edge cases.

### 2. URI Handler Management Tests (`test_uri_handlers.cpp`)

**Purpose**: Tests for registering, unregistering, and managing URI handlers.

**Tests Included**:
- `given_server_started_when_registering_valid_uri_handler_then_returns_success` - Tests handler registration
- `given_null_handler_when_registering_uri_handler_then_returns_invalid_arg` - Tests null handler error
- `given_registered_uri_handler_when_unregistering_same_handler_then_returns_success` - Tests handler unregistration
- `given_server_with_max_handlers_when_exceeding_limit_then_handlers_full_error` - Tests handler limit enforcement
- `given_duplicate_handler_registration_when_attempting_then_returns_handler_exists_error` - Tests duplicate prevention
- `given_multiple_handlers_for_same_uri_when_unregistering_uri_then_all_handlers_are_removed` - Tests bulk unregistration

**What They Test**: Handler lifecycle management, duplicate prevention, limit enforcement, and proper cleanup.

### 3. Request Processing Tests (`test_request_processing.cpp`)

**Purpose**: Tests for parsing HTTP requests, URL queries, headers, and cookies.

**Tests Included**:
- `given_valid_request_when_calling_httpd_req_get_url_query_len_then_returns_query_length` - Tests query length retrieval
- `given_various_url_queries_when_calling_httpd_req_get_url_query_len_then_returns_correct_length` - Tests various query scenarios
- `given_valid_request_when_calling_httpd_req_get_hdr_value_len_then_returns_header_length` - Tests header length retrieval
- `given_query_string_when_calling_httpd_query_key_value_then_parses_correctly` - Tests query parameter parsing
- `given_edge_case_query_string_when_calling_httpd_query_key_value_then_parses_correctly` - Tests query parsing edge cases
- `given_various_url_queries_when_calling_httpd_req_get_url_query_str_then_returns_correct_string` - Tests query string extraction
- `test_httpd_req_get_cookie_val_success` - Tests successful cookie value retrieval
- `test_httpd_req_get_cookie_val_not_found` - Tests cookie not found scenario
- `test_httpd_req_get_cookie_val_no_cookie_header` - Tests missing cookie header
- `test_httpd_req_get_cookie_val_empty_cookie_header` - Tests empty cookie header
- `test_httpd_req_get_cookie_val_buffer_truncation` - Tests buffer overflow handling
- `test_httpd_req_get_cookie_val_invalid_args` - Tests invalid argument handling

**What They Test**: Request data extraction, URL parsing, header processing, query parameter handling, and cookie value retrieval.

### 4. Response Handling Tests (`test_response_handling.cpp`)

**Purpose**: Tests for sending HTTP responses, including chunked and custom responses.

**Tests Included**:
- `given_valid_request_when_calling_httpd_resp_send_then_response_is_sent` - Tests basic response sending
- `given_server_with_resp_send_handler_when_client_requests_then_receives_response` - Tests end-to-end response flow
- `given_server_with_custom_response_handler_when_client_requests_then_receives_custom_response` - Tests custom headers/status
- `given_server_with_chunked_handler_when_client_requests_then_receives_chunked_response` - Tests chunked encoding
- `given_server_with_large_response_handler_when_client_requests_then_receives_large_response` - Tests large response handling
- `given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches` - Tests URI pattern matching
- `given_valid_global_context_when_setting_and_getting_then_context_preserved` - Tests global context
- `given_valid_session_context_when_setting_and_getting_then_context_preserved` - Tests session context

**What They Test**: Response generation, custom headers, status codes, chunked transfer encoding, large data transfers, URI pattern matching, and context management.

### 5. WebSocket Tests (`test_websocket.cpp`)

**Purpose**: Tests for WebSocket upgrade handshake and data frame exchange.

**Tests Included**:
- `given_server_with_ws_handler_when_client_sends_upgrade_request_then_handshake_succeeds` - Tests WebSocket upgrade
- `given_ws_connection_when_sending_and_receiving_data_then_frames_are_exchanged_correctly` - Tests data frame exchange
- `given_ws_connection_when_sending_frame_with_16bit_length_then_succeeds` - Tests 16-bit length frames
- `given_ws_connection_when_sending_frame_with_64bit_length_then_succeeds` - Tests 64-bit length frames
- `given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection` - Tests connection closing
- `given_websocket_and_http_clients_when_calling_httpd_ws_get_fd_info_then_returns_correct_client_type` - Tests client type identification
- `given_server_with_long_subprotocol_when_client_requests_ws_upgrade_then_handshake_fails` - Tests subprotocol length validation
- `given_ws_connection_when_idle_then_keep_alive_maintains_connection` - Tests WebSocket keep-alive
- `given_ws_connection_when_client_sends_ping_then_server_responds_with_pong` - Tests ping/pong functionality

**What They Test**: WebSocket protocol implementation, handshake process, text/binary frame handling, connection management, and control frames.

### 6. Client Management Tests (`test_client_management.cpp`)

**Purpose**: Tests for client connection management, limits, and concurrency.

**Tests Included**:
- `given_valid_server_when_calling_httpd_get_client_list_then_returns_client_fds` - Verifies client list retrieval functionality
- `given_server_with_lru_enabled_when_max_sockets_exceeded_then_oldest_session_is_closed` - Tests LRU mechanism
- `given_server_with_multiple_clients_when_rapid_connections_then_server_handles_gracefully` - Tests rapid connection handling
- `given_server_with_open_close_callbacks_when_client_connects_and_disconnects_then_callbacks_are_invoked` - Tests connection callbacks

**What They Test**: Connection limits, LRU eviction, client tracking, callback invocation, and concurrent connection handling.

### 7. Error Handling Tests (`test_error_handling.cpp`)

**Purpose**: Tests for HTTP error scenarios and custom error handling.

**Tests Included**:
- `given_server_without_uri_handler_when_client_requests_unregistered_uri_then_404_not_found_is_returned` - Tests 404 handling
- `given_registered_uri_handler_for_get_when_post_request_then_405_method_not_allowed` - Tests 405 handling
- `given_server_running_when_request_without_version_is_sent_then_505_version_unsupported_is_returned` - Tests 505 handling
- `given_server_running_when_long_uri_request_is_sent_then_414_uri_too_long_is_returned` - Tests 414 handling
- `given_server_running_when_long_header_request_is_sent_then_431_req_hdr_fields_too_large_is_returned` - Tests 431 handling
- `given_server_with_custom_error_handler_when_error_occurs_then_handler_is_invoked` - Tests custom error handlers
- `given_request_with_less_content_length_when_sent_then_server_handles_correctly` - Tests content length validation (less data)
- `given_request_with_more_content_length_when_sent_then_server_handles_correctly` - Tests content length validation (more data)

**What They Test**: HTTP error codes, malformed request handling, content length validation, and custom error response generation.

### 8. Utility Tests (`test_utilities.cpp`)

**Purpose**: Tests for utility functions and context management.

**Tests Included**:
- `given_request_with_multiple_headers_when_calling_httpd_req_get_hdr_value_str_then_returns_correct_values` - Tests header extraction
- `given_headers_with_last_header_no_crlf_when_get_header_then_returns_correct_value` - Tests header parsing edge cases
- `given_valid_request_with_body_when_calling_httpd_req_recv_then_receives_data` - Tests request receiving
- `given_valid_request_when_calling_httpd_send_then_sends_data` - Tests data sending
- `given_server_with_custom_uri_match_fn_when_request_matches_then_handler_invoked` - Tests custom URI matching
- `given_server_with_uri_handler_when_client_connects_then_handler_is_invoked` - Tests end-to-end flow
- `dummy` - Placeholder test.

**What They Test**: URI pattern matching, context management, custom matching functions, header parsing, request receiving, data sending, and end-to-end flow testing.

### 9. Async Requests Tests (`test_async_requests.cpp`)

**Purpose**: Tests for asynchronous request handling.

**Tests Included**:
- `given_server_with_async_handler_when_client_requests_then_receives_response` - Tests async request processing

**What They Test**: Asynchronous request processing and response.

### 10. Async WebSocket Tests (`test_async_websocket.cpp`)

**Purpose**: Tests for asynchronous WebSocket functionality.

**Tests Included**:
- `given_ws_connection_when_sending_sync_from_another_task_then_succeeds` - Tests synchronous WebSocket sending from another task
- `given_ws_connection_when_sending_async_from_another_task_then_succeeds` - Tests asynchronous WebSocket sending from another task
- `given_closed_ws_connection_when_sending_sync_then_fails` - Tests failed synchronous sending on closed connection
- `given_closed_ws_connection_when_sending_async_then_callback_receives_error` - Tests error callback for async sending on closed connection

**What They Test**: Asynchronous WebSocket data sending and error handling.

### 11. Async Work Queue Tests (`test_async_work_queue.cpp`)

**Purpose**: Tests for the asynchronous work queue.

**Tests Included**:
- `given_server_with_async_work_queue_handler_when_client_gets_then_receives_two_responses` - Tests async work queue functionality

**What They Test**: Asynchronous work queue functionality.

### 12. Empty Header Tests (`test_empty_header.cpp`)

**Purpose**: Tests for handling requests with empty headers.

**Tests Included**:
- `given_server_with_empty_header_handler_when_client_sends_request_with_empty_header_then_it_is_handled_correctly` - Tests correct parsing of empty headers

**What They Test**: Correct parsing of empty headers.

### 13. Leftover Data Tests (`test_leftover_data.cpp`)

**Purpose**: Tests for handling leftover data in requests.

**Tests Included**:
- `given_server_with_leftover_data_handler_when_client_posts_then_server_handles_it_gracefully` - Tests server robustness when handling requests with unread body data

**What They Test**: Server robustness when handling requests with unread body data.

### 14. Session Context Tests (`test_session_context.cpp`)

**Purpose**: Tests for session context management.

**Tests Included**:
- `given_server_with_session_handler_when_client_posts_then_context_is_maintained` - Tests session context creation, persistence, and cleanup

**What They Test**: Session context creation, persistence, and cleanup.

### 15. HTTP Methods Tests (`test_http_methods.cpp`)

**Purpose**: Tests for HTTP method handling and RFC 1945 compliance for PUT, DELETE, and HEAD methods.

**Tests Included**:
- `given_server_with_put_handler_when_client_sends_put_request_then_server_handles_correctly` - Tests PUT request with body data
- `given_server_with_delete_handler_when_client_sends_delete_request_then_server_handles_correctly` - Tests DELETE request without body
- `given_server_with_head_handler_when_client_sends_head_request_then_server_returns_headers_only` - Tests HEAD request headers-only response
- `given_server_with_get_only_handler_when_client_sends_put_delete_head_then_405_method_not_allowed` - Tests method validation for unsupported methods

**RFC 1945 Coverage**:
- **PUT method** - Idempotent resource updates with request body
- **DELETE method** - Resource deletion without request body
- **HEAD method** - Headers-only responses identical to GET
- **405 Method Not Allowed** - Proper error responses for unsupported methods

**What They Test**: Complete HTTP method implementation validation, ensuring servers can handle all standard HTTP methods correctly and return appropriate error responses for unsupported method combinations.

**Implementation Details**: Extends test client infrastructure to support PUT/DELETE/HEAD methods, addresses critical testing gap identified in standards.md for RFC 1945 compliance.

### 16. Authentication Tests (`test_authentication.cpp`)

**Purpose**: Tests for HTTP Authentication headers, response codes, and proper Basic authentication implementation as per RFC 9110 and RFC 7617.

**Tests Included**:
- `given_protected_resource_when_no_auth_header_then_401_unauthorized_returned` - Tests WWW-Authenticate header and 401 response for missing auth
- `given_basic_auth_credentials_when_valid_then_access_granted` - Tests valid Basic auth with proper base64 decoding and credential validation
- `given_basic_auth_credentials_when_invalid_then_access_denied` - Tests invalid Basic auth credentials with proper base64 decoding
- `given_authentication_info_when_successful_then_header_included` - Tests Authentication-Info header inclusion
- `given_multiple_auth_schemes_when_offered_then_client_can_choose` - Tests multiple authentication schemes in WWW-Authenticate
- `given_malformed_auth_header_when_provided_then_400_bad_request` - Tests malformed Authorization headers
- `given_invalid_base64_auth_when_provided_then_access_denied` - Tests Authorization headers with invalid base64 encoding
- `given_wrong_scheme_auth_when_provided_then_access_denied` - Tests unsupported authentication schemes

**RFC 9110 Coverage**:
- **WWW-Authenticate header field** (Section 11.6.1) - Authentication challenges
- **Authorization header field** (Section 11.6.2) - Client credentials
- **Authentication-Info header field** (Section 11.6.3) - Post-authentication information
- **Authentication scheme extensibility** (Section 16.4) - Multiple auth schemes

**RFC 7617 Coverage** (Basic Authentication):
- **Basic authentication scheme** - Proper base64 decoding of credentials
- **Username:password validation** - Correct parsing and verification
- **Malformed credentials handling** - Invalid base64, wrong schemes, etc.

**What They Test**: Complete HTTP Basic authentication implementation with proper RFC 7617 base64 decoding, credential validation, error handling, and RFC 9110 header support.

**Implementation Details**: Uses the `validate_basic_auth()` helper function to properly decode base64 credentials and validate username:password pairs against expected values, providing true RFC 7617 and RFC 9110 compliance.

### 17. Security Tests (`test_security.cpp`)

**Purpose**: Tests for critical HTTP security vulnerabilities and RFC 9112 compliance.

**Tests Included**:
- `test_response_splitting_prevention_in_custom_headers` - Tests that CRLF in header values don't create response splitting attacks
- `test_response_splitting_prevention_in_custom_status` - Tests that custom status lines can't inject HTTP headers
- `test_crlf_injection_protection_in_header_values` - Tests that Location and other headers can't be injected via CRLF
- `test_header_injection_attack_prevention_in_error_messages` - Tests that custom error messages can't inject headers
- `test_header_field_name_injection_prevention` - Tests that header field names can't contain injection characters
- `test_request_smuggling_content_length_mismatch` - Tests Content-Length validation to prevent request smuggling

**RFC 9112 Coverage**:
- **Response splitting attack prevention** (Section 11.1) - CRLF injection in headers
- **Request smuggling attack prevention** (Section 11.2) - Content-Length mismatches
- **Header validation and sanitization** (Section 5.1-5.2) - Malformed header handling
- **CRLF injection protection** - General input sanitization security

**Security Vulnerabilities Tested**:
- **HTTP Response Splitting**: Tests prevent injection of additional HTTP headers in response
- **Header Injection**: Tests prevent malicious header field injection
- **CRLF Injection**: Tests protect against carriage return/line feed injection attacks
- **Request Smuggling**: Tests prevent HTTP request smuggling through Content-Length mismatches

**What They Test**: Critical security vulnerabilities that could lead to serious attacks like cache poisoning, request smuggling, and XSS. Each test includes helper functions to detect response splitting patterns and validates that attacks don't succeed.

**Implementation Details**: Uses `response_splitting_safe()` helper function to detect if patterns indicative of successful response splitting attacks appear in responses. Tests both direct header injection vectors and indirect injection through user-controlled data.

## Dependencies

Each test file requires the following common dependencies:
- `unity.h` - Unity test framework
- `esp_http_server.h` - HTTP server API
- `http_test_client.h` - Test client for integration tests
- Platform-specific socket headers
- Standard C library headers

### Run Specific Test Categories
Individual test categories can be run by modifying the main test file to include only the desired test functions.

### Adding New Tests
When adding new tests:
1. Determine the appropriate category based on functionality
2. Add the test to the corresponding test file
3. Update this documentation to include the new test
4. Ensure the test follows the naming convention: `given_[setup]_when_[action]_then_[expected_result]`

## Test Naming Convention

All tests follow the Given-When-Then naming pattern:
- **Given**: Describes the initial setup/conditions
- **When**: Describes the action being tested
- **Then**: Describes the expected outcome

Example: `given_valid_config_when_server_started_then_returns_success`

## Integration with Test Framework

The tests use the Unity test framework and are designed to work with PlatformIO's test runner. Each test file contains:
- Unity test functions
- Proper setup/teardown in `setUp()` and `tearDown()` functions
- Comprehensive assertions using `TEST_ASSERT_*` macros
- Integration tests using `http_test_client` for end-to-end validation

## Test Categories and Descriptions

### 1. Server Lifecycle Tests (`test_server_lifecycle.cpp`)
**Test Functions:**
- `given_valid_httpd_config_when_httpd_start_is_called_then_returns_success` - Verifies server starts with valid config
- `given_null_handle_when_httpd_start_is_called_then_returns_invalid_arg` - Tests null handle error handling
- `given_null_config_when_httpd_start_is_called_then_returns_invalid_arg` - Tests null config error handling
- `given_started_server_when_httpd_stop_is_called_then_server_stops` - Verifies proper server shutdown
- `given_null_handle_when_httpd_stop_is_called_then_returns_invalid_arg` - Tests null handle error in stop
- `given_started_server_when_calling_httpd_stop_multiple_times_then_handles_gracefully` - Tests multiple stop calls
- `given_zero_port_when_httpd_start_is_called_then_assigns_random_port_and_returns_success` - Tests random port assignment

### 2. URI Handler Management Tests (`test_uri_handlers.cpp`)
**Test Functions:**
- `given_server_started_when_registering_valid_uri_handler_then_returns_success` - Tests handler registration
- `given_null_handler_when_registering_uri_handler_then_returns_invalid_arg` - Tests null handler error
- `given_registered_uri_handler_when_unregistering_same_handler_then_returns_success` - Tests handler unregistration
- `given_server_with_max_handlers_when_exceeding_limit_then_handlers_full_error` - Tests handler limit enforcement
- `given_duplicate_handler_registration_when_attempting_then_returns_handler_exists_error` - Tests duplicate prevention
- `given_multiple_handlers_for_same_uri_when_unregistering_uri_then_all_handlers_are_removed` - Tests bulk unregistration

### 3. Request Processing Tests (`test_request_processing.cpp`)
**Test Functions:**
- `given_valid_request_when_calling_httpd_req_get_url_query_len_then_returns_query_length` - Tests query length retrieval
- `given_various_url_queries_when_calling_httpd_req_get_url_query_len_then_returns_correct_length` - Tests various query scenarios
- `given_valid_request_when_calling_httpd_req_get_hdr_value_len_then_returns_header_length` - Tests header length retrieval
- `given_query_string_when_calling_httpd_query_key_value_then_parses_correctly` - Tests query parameter parsing
- `given_edge_case_query_string_when_calling_httpd_query_key_value_then_parses_correctly` - Tests query parsing edge cases
- `given_various_url_queries_when_calling_httpd_req_get_url_query_str_then_returns_correct_string` - Tests query string extraction
- `test_httpd_req_get_cookie_val_success` - Tests successful cookie value retrieval
- `test_httpd_req_get_cookie_val_not_found` - Tests cookie not found scenario
- `test_httpd_req_get_cookie_val_no_cookie_header` - Tests missing cookie header
- `test_httpd_req_get_cookie_val_empty_cookie_header` - Tests empty cookie header
- `test_httpd_req_get_cookie_val_buffer_truncation` - Tests buffer overflow handling
- `test_httpd_req_get_cookie_val_invalid_args` - Tests invalid argument handling

### 4. Response Handling Tests (`test_response_handling.cpp`)
**Test Functions:**
- `given_valid_request_when_calling_httpd_resp_send_then_response_is_sent` - Tests basic response sending
- `given_server_with_resp_send_handler_when_client_requests_then_receives_response` - Tests end-to-end response flow
- `given_server_with_custom_response_handler_when_client_requests_then_receives_custom_response` - Tests custom headers/status
- `given_server_with_chunked_handler_when_client_requests_then_receives_chunked_response` - Tests chunked encoding
- `given_server_with_large_response_handler_when_client_requests_then_receives_large_response` - Tests large response handling
- `given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches` - Tests URI pattern matching
- `given_valid_global_context_when_setting_and_getting_then_context_preserved` - Tests global context
- `given_valid_session_context_when_setting_and_getting_then_context_preserved` - Tests session context

### 5. WebSocket Tests (`test_websocket.cpp`)
**Test Functions:**
- `given_server_with_ws_handler_when_client_sends_upgrade_request_then_handshake_succeeds` - Tests WebSocket upgrade
- `given_ws_connection_when_sending_and_receiving_data_then_frames_are_exchanged_correctly` - Tests data frame exchange
- `given_ws_connection_when_sending_frame_with_16bit_length_then_succeeds` - Tests 16-bit length frames
- `given_ws_connection_when_sending_frame_with_64bit_length_then_succeeds` - Tests 64-bit length frames
- `given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection` - Tests connection closing
- `given_websocket_and_http_clients_when_calling_httpd_ws_get_fd_info_then_returns_correct_client_type` - Tests client type identification
- `given_server_with_long_subprotocol_when_client_requests_ws_upgrade_then_handshake_fails` - Tests subprotocol length validation
- `given_ws_connection_when_idle_then_keep_alive_maintains_connection` - Tests WebSocket keep-alive
- `given_ws_connection_when_client_sends_ping_then_server_responds_with_pong` - Tests ping/pong functionality

### 6. Client Management Tests (`test_client_management.cpp`)
**Test Functions:**
- `given_valid_server_when_calling_httpd_get_client_list_then_returns_client_fds` - Verifies client list retrieval functionality
- `given_server_with_lru_enabled_when_max_sockets_exceeded_then_oldest_session_is_closed` - Tests LRU mechanism
- `given_server_with_multiple_clients_when_rapid_connections_then_server_handles_gracefully` - Tests rapid connection handling
- `given_server_with_open_close_callbacks_when_client_connects_and_disconnects_then_callbacks_are_invoked` - Tests connection callbacks

### 7. Error Handling Tests (`test_error_handling.cpp`)
**Test Functions:**
- `given_server_without_uri_handler_when_client_requests_unregistered_uri_then_404_not_found_is_returned` - Tests 404 handling
- `given_registered_uri_handler_for_get_when_post_request_then_405_method_not_allowed` - Tests 405 handling
- `given_server_running_when_request_without_version_is_sent_then_505_version_unsupported_is_returned` - Tests 505 handling
- `given_server_running_when_long_uri_request_is_sent_then_414_uri_too_long_is_returned` - Tests 414 handling
- `given_server_running_when_long_header_request_is_sent_then_431_req_hdr_fields_too_large_is_returned` - Tests 431 handling
- `given_server_with_custom_error_handler_when_error_occurs_then_handler_is_invoked` - Tests custom error handlers
- `given_request_with_less_content_length_when_sent_then_server_handles_correctly` - Tests content length validation (less data)
- `given_request_with_more_content_length_when_sent_then_server_handles_correctly` - Tests content length validation (more data)

### 8. Utility Tests (`test_utilities.cpp`)
**Test Functions:**
- `given_request_with_multiple_headers_when_calling_httpd_req_get_hdr_value_str_then_returns_correct_values` - Tests header extraction
- `given_headers_with_last_header_no_crlf_when_get_header_then_returns_correct_value` - Tests header parsing edge cases
- `given_valid_request_with_body_when_calling_httpd_req_recv_then_receives_data` - Tests request receiving
- `given_valid_request_when_calling_httpd_send_then_sends_data` - Tests data sending
- `given_server_with_custom_uri_match_fn_when_request_matches_then_handler_invoked` - Tests custom URI matching
- `given_server_with_uri_handler_when_client_connects_then_handler_is_invoked` - Tests end-to-end flow
- `dummy` - Placeholder test.

### 9. Async Requests Tests (`test_async_requests.cpp`)
**Test Functions:**
- `given_server_with_async_handler_when_client_requests_then_receives_response` - Tests async request processing

### 10. Async WebSocket Tests (`test_async_websocket.cpp`)
**Test Functions:**
- `given_ws_connection_when_sending_sync_from_another_task_then_succeeds` - Tests synchronous WebSocket sending from another task
- `given_ws_connection_when_sending_async_from_another_task_then_succeeds` - Tests asynchronous WebSocket sending from another task
- `given_closed_ws_connection_when_sending_sync_then_fails` - Tests failed synchronous sending on closed connection
- `given_closed_ws_connection_when_sending_async_then_callback_receives_error` - Tests error callback for async sending on closed connection

### 11. Async Work Queue Tests (`test_async_work_queue.cpp`)
**Test Functions:**
- `given_server_with_async_work_queue_handler_when_client_gets_then_receives_two_responses` - Tests async work queue functionality

### 12. Empty Header Tests (`test_empty_header.cpp`)
**Test Functions:**
- `given_server_with_empty_header_handler_when_client_sends_request_with_empty_header_then_it_is_handled_correctly` - Tests correct parsing of empty headers

### 13. Leftover Data Tests (`test_leftover_data.cpp`)
**Test Functions:**
- `given_server_with_leftover_data_handler_when_client_posts_then_server_handles_it_gracefully` - Tests server robustness when handling requests with unread body data

### 14. Session Context Tests (`test_session_context.cpp`)
**Test Functions:**
- `given_server_with_session_handler_when_client_posts_then_context_is_maintained` - Tests session context creation, persistence, and cleanup

### 15. HTTP Methods Tests (`test_http_methods.cpp`)
**Test Functions:**
- `given_server_with_put_handler_when_client_sends_put_request_then_server_handles_correctly` - Tests PUT request with body data
- `given_server_with_delete_handler_when_client_sends_delete_request_then_server_handles_correctly` - Tests DELETE request without body
- `given_server_with_head_handler_when_client_sends_head_request_then_server_returns_headers_only` - Tests HEAD request headers-only response
- `given_server_with_get_only_handler_when_client_sends_put_delete_head_then_405_method_not_allowed` - Tests method validation for unsupported methods

### 16. Authentication Tests (`test_authentication.cpp`)
**Test Functions:**
- `given_protected_resource_when_no_auth_header_then_401_unauthorized_returned` - Tests WWW-Authenticate header and 401 response for missing auth
- `given_basic_auth_credentials_when_valid_then_access_granted` - Tests valid Basic auth with proper base64 decoding and credential validation
- `given_basic_auth_credentials_when_invalid_then_access_denied` - Tests invalid Basic auth credentials with proper base64 decoding
- `given_authentication_info_when_successful_then_header_included` - Tests Authentication-Info header inclusion
- `given_multiple_auth_schemes_when_offered_then_client_can_choose` - Tests multiple authentication schemes in WWW-Authenticate
- `given_malformed_auth_header_when_provided_then_400_bad_request` - Tests malformed Authorization headers
- `given_invalid_base64_auth_when_provided_then_access_denied` - Tests Authorization headers with invalid base64 encoding
- `given_wrong_scheme_auth_when_provided_then_access_denied` - Tests unsupported authentication schemes

### 17. Security Tests (`test_security.cpp`)
**Test Functions:**
- `test_response_splitting_prevention_in_custom_headers` - Tests that CRLF in header values don't create response splitting attacks
- `test_response_splitting_prevention_in_custom_status` - Tests that custom status lines can't inject HTTP headers
- `test_crlf_injection_protection_in_header_values` - Tests that Location and other headers can't be injected via CRLF
- `test_header_injection_attack_prevention_in_error_messages` - Tests that custom error messages can't inject headers
- `test_header_field_name_injection_prevention` - Tests that header field names can't contain injection characters
- `test_request_smuggling_content_length_mismatch` - Tests Content-Length validation to prevent request smuggling

### 18. Conditional Requests Tests (`test_conditional.c`)
**Purpose**: Tests for HTTP conditional requests (RFC 9110 Part 13) including ETag generation, If-Match, If-None-Match, If-Modified-Since, and If-Unmodified-Since headers.

**Test Functions:**
- `test_generate_strong_etag_success` - Tests successful strong ETag generation from content
- `test_generate_strong_etag_invalid_args` - Tests ETag generation with invalid arguments
- `test_generate_weak_etag_success` - Tests successful weak ETag generation from timestamp
- `test_generate_weak_etag_invalid_args` - Tests weak ETag generation with invalid arguments
- `test_parse_etag_list_single_etag` - Tests parsing of single ETag in header
- `test_parse_etag_list_multiple_etags` - Tests parsing of multiple ETags in header
- `test_parse_etag_list_star` - Tests parsing of wildcard ETag
- `test_etag_matches_success` - Tests successful ETag matching
- `test_etag_matches_failure` - Tests failed ETag matching
- `test_etag_matches_star` - Tests wildcard ETag matching
- `test_middleware_no_conditional_headers` - Tests middleware with no conditional headers
- `test_middleware_if_match_matching_etag` - Tests If-Match with matching ETag
- `test_middleware_if_match_non_matching_etag` - Tests If-Match with non-matching ETag
- `test_middleware_if_none_match_matching_etag_get` - Tests If-None-Match with matching ETag for GET
- `test_middleware_if_none_match_matching_etag_post` - Tests If-None-Match with matching ETag for POST
- `test_middleware_if_modified_since_matching` - Tests If-Modified-Since with matching timestamp
- `test_middleware_if_unmodified_since_non_matching` - Tests If-Unmodified-Since with non-matching timestamp
- `test_parse_http_date_success` - Tests successful HTTP date parsing
- `test_parse_http_date_invalid_args` - Tests HTTP date parsing with invalid arguments
- `test_format_http_date_success` - Tests successful HTTP date formatting
- `test_format_http_date_invalid_args` - Tests HTTP date formatting with invalid arguments

**RFC 9110 Coverage**:
- **ETag header field** (Section 8.8.3) - Strong/weak validators
- **If-Match, If-None-Match** (Section 13.1.1, 13.1.2) - Entity tag preconditions
- **If-Modified-Since, If-Unmodified-Since** (Section 13.1.3, 13.1.4) - Date-based preconditions
- **Conditional request evaluation precedence** (Section 13.2.2) - Request processing order

**What They Test**: Complete HTTP conditional request implementation with proper ETag generation, header parsing, and RFC 9110 compliance. Tests cover all conditional request scenarios including 304 Not Modified and 412 Precondition Failed responses.

### 19. Transfer Encoding Tests (`test_chunked_request_parsing.cpp`, `test_chunked_response.cpp`)

**Purpose**: Tests for chunked Transfer-Encoding request parsing and response generation (RFC 9112 §7).

**Tests Included**:
- `test_chunk_size_parsing_valid` - Tests valid chunk size parsing
- `test_chunk_size_parsing_invalid` - Tests invalid chunk size parsing
- `test_chunked_read_basic` - Tests basic chunked request reading
- `test_chunked_read_large_chunk` - Tests large chunk handling
- `test_chunked_response_basic` - Tests basic chunked response sending

**RFC 9112 Coverage**:
- Chunked encoding (Section 7.1): Chunk size, extensions, trailers

**What They Test**: Chunk size parsing validation, chunked request body reading, chunked response sending, error handling for invalid chunks and oversized chunks.

**Test Functions**:
- `test_chunk_size_parsing_valid`
- `test_chunk_size_parsing_invalid`
- `test_chunked_read_basic`
- `test_chunked_read_large_chunk`
- `test_chunked_response_basic`

## TODO Section - Missing Edge Cases and Improvements

### High Priority Edge Cases to Add

1. **Concurrent Connection Handling**
   - Test multiple simultaneous client connections
   - Test connection limits under concurrent load
   - Test race conditions in handler registration/unregistration

2. **Memory Management Edge Cases**
   - Test memory allocation failures in server startup
   - Test memory leaks in long-running servers
   - Test cleanup on abnormal termination

3. **Network Error Handling**
   - Test behavior with interrupted connections
   - Test timeout handling for various operations
   - Test error recovery from network failures

4. **Security Edge Cases**
   - Test malformed HTTP requests (buffer overflows)
   - Test malicious URI patterns and injection attempts
   - Test header injection and parsing vulnerabilities

5. **Resource Exhaustion**
   - Test behavior when file descriptors are exhausted
   - Test memory pressure scenarios
   - Test handling of oversized requests/responses

### Medium Priority Improvements

1. **Performance Testing**
   - Add benchmarks for request processing
   - Test response times under various loads
   - Test memory usage patterns

2. **Platform-Specific Testing**
   - Add more comprehensive Windows-specific tests
   - Test cross-platform compatibility
   - Test embedded platform constraints

3. **Advanced WebSocket Testing**
   - Test WebSocket compression
   - Test WebSocket subprotocol negotiation
   - Test WebSocket frame fragmentation

4. **Advanced HTTP Features**
   - Test HTTP/1.1 persistent connections
   - Test HTTP headers validation
   - Test HTTP authentication mechanisms

### Low Priority Enhancements

1. **Integration Testing**
   - Add real browser integration tests
   - Test with various HTTP clients
   - Test with different HTTP libraries

2. **Documentation and Examples**
   - Add more comprehensive examples
   - Create troubleshooting guide
   - Add performance tuning guide

3. **Tooling**
   - Add automated test result analysis
   - Create test coverage reports
   - Add performance regression detection
