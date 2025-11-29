# ESP HTTP Server Test Documentation

## Overview

This document describes the organization and structure of the ESP HTTP Server tests. The tests have been categorized into logical groups to improve maintainability, readability, and ease of execution.

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
- `given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection` - Tests connection closing

**What They Test**: WebSocket protocol implementation, handshake process, text/binary frame handling, and connection management.

### 6. Client Management Tests (`test_client_management.cpp`)

**Purpose**: Tests for client connection management, limits, and concurrency.

**Tests Included**:
- `given_valid_server_when_calling_httpd_get_client_list_then_returns_client_fds` - Verifies client list retrieval functionality.
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
- `given_request_with_less_content_length_when_sent_then_server_handles_correctly` - Tests content length validation
- `given_request_with_more_content_length_when_sent_then_server_handles_correctly` - Tests content length validation

**What They Test**: HTTP error codes, malformed request handling, content length validation, and custom error response generation.

### 8. Utility Tests (`test_utilities.cpp`)

**Purpose**: Tests for utility functions and context management.

**Tests Included**:
- `given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches` - Tests URI pattern matching
- `given_custom_uri_match_fn_when_request_matches_then_handler_invoked` - Tests custom URI matching
- `given_request_with_multiple_headers_when_calling_httpd_req_get_hdr_value_str_then_returns_correct_values` - Tests header extraction
- `given_headers_with_last_header_no_crlf_when_get_header_then_returns_correct_value` - Tests header parsing edge cases
- `given_valid_request_with_body_when_calling_httpd_req_recv_then_receives_data` - Tests request receiving
- `given_valid_request_when_calling_httpd_send_then_sends_data` - Tests data sending
- `given_server_with_uri_handler_when_client_connects_then_handler_is_invoked` - Tests end-to-end flow
- `dummy` - Placeholder test.

**What They Test**: URI pattern matching, context management, custom matching functions, header parsing, request receiving, data sending, and end-to-end flow testing.

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
- `given_valid_global_context_when_setting_and_getting_then_context_preserved` - Tests global context
- `given_valid_session_context_when_setting_and_getting_then_context_preserved` - Tests session context

### 5. WebSocket Tests (`test_websocket.cpp`)
**Test Functions:**
- `given_server_with_ws_handler_when_client_sends_upgrade_request_then_handshake_succeeds` - Tests WebSocket upgrade
- `given_ws_connection_when_sending_and_receiving_data_then_frames_are_exchanged_correctly` - Tests data frame exchange
- `given_ws_connection_when_client_sends_close_frame_then_server_responds_with_close_and_closes_connection` - Tests connection closing

### 6. Client Management Tests (`test_client_management.cpp`)
**Test Functions:**
- `given_valid_server_when_calling_httpd_get_client_list_then_returns_client_fds` - Verifies client list retrieval functionality.
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
- `given_valid_uris_when_calling_httpd_uri_match_wildcard_then_correctly_matches` - Tests URI pattern matching
- `given_custom_uri_match_fn_when_request_matches_then_handler_invoked` - Tests custom URI matching
- `given_server_with_uri_handler_when_client_connects_then_handler_is_invoked` - Tests end-to-end flow
- `given_request_with_multiple_headers_when_calling_httpd_req_get_hdr_value_str_then_returns_correct_values` - Tests header extraction
- `given_headers_with_last_header_no_crlf_when_get_header_then_returns_correct_value` - Tests header parsing edge cases
- `given_valid_request_with_body_when_calling_httpd_req_recv_then_receives_data` - Tests request receiving
- `given_valid_request_when_calling_httpd_send_then_sends_data` - Tests data sending
- `dummy` - Placeholder test.

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
