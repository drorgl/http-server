# ESP HTTP Server Test Architecture and Guidelines

## Architecture Overview

The test suite uses a modular, category-based architecture where tests are organized by HTTP server functionality areas. Each category resides in separate `.cpp/.h` files with independent runners that can be executed individually or as part of the full suite.

### Key Components
- **Individual Category Runners**: Each category exports a `int test_category(void)` function that executes all tests in that category using `RUN_TEST()` macros
- **Global Runner**: `test_esp_http_server.cpp`'s `test_esp_http_server()` function aggregates all category runners and returns the sum of their results
- **Unity Framework**: Uses Unity test framework with BDD-style naming conventions and appropriate assertion macros

## Test Categories

Categories are organized by functional areas to improve maintainability and enable independent execution. Choose appropriate categories based on functionality:

| Category | File | Purpose |
|----------|------|---------|
| Server Lifecycle | `test_server_lifecycle.cpp` | Server init, configuration, start/stop operations |
| URI Handler Management | `test_uri_handlers.cpp` | Handler registration, unregistration, limits |
| Request Processing | `test_request_processing.cpp` | HTTP request parsing, headers, queries, cookies |
| Response Handling | `test_response_handling.cpp` | HTTP response generation, chunked encoding |
| WebSocket Core | `test_websocket.cpp` | WebSocket handshake, frames, connection management |
| WebSocket Extensions | `test_websocket_extensions*.cpp` | WebSocket extensions and E2E testing |
| Client Management | `test_client_management.cpp` | Connection limits, LRU, session tracking, callbacks |
| Error Handling | `test_error_handling.cpp` | HTTP error codes, validation, custom handlers |
| Utilities | `test_utilities.cpp` | General utility functions, context management |
| Async Operations | `test_async_*.cpp` | Asynchronous requests, WebSocket, work queues |
| Authentication | `test_authentication.cpp` | HTTP auth headers, Basic authentication (RFC 9110/7617) |
| Security | `test_security.cpp` | Security vulnerabilities, injections (RFC 9112) |
| Transfer Encoding | `test_chunked_*.cpp` | Chunked request/response parsing (RFC 9112) |
| Specialized Cases | Various `test_*.cpp` | Empty headers, leftover data, session context |

### Category Selection Guidelines
- Choose existing categories first when functionality clearly maps to a category
- Create new categories only for major functional areas not covered
- Keep categories focused on single responsibilities
- Prefer integration testing over direct internal function testing (use public APIs)

## Runner Execution Hierarchy

```
test_esp_http_server()                    # Global runner (test_esp_http_server.cpp)
├── test_server_lifecycle()             # Server lifecycle tests
├── test_uri_handlers()                  # URI handler management tests
├── test_request_processing()            # Request processing tests
├── test_response_handling()             # Response handling tests
├── test_websocket()                     # Core WebSocket tests
├── test_client_management()             # Client management tests
├── test_error_handling()               # Error handling tests
├── test_utilities()                     # Utility function tests
├── test_async_requests()               # Async request tests
├── test_async_websocket()              # Async WebSocket tests
├── test_async_work_queue()             # Async work queue tests
├── test_empty_header()                 # Empty header handling tests
├── test_leftover_data()                # Leftover data handling tests
├── test_session_context()              # Session context management tests
├── test_authentication()               # Authentication tests
├── test_security()                     # Security tests
├── test_http_methods()                 # HTTP method support tests
├── test_chunked_response()             # Chunked response tests
├── test_chunked_request_parsing()      # Chunked request parsing tests
└── test_websocket_extensions_*()       # WebSocket extension tests
```

## Implementation Guidelines

### BDD Naming Convention
Tests follow `given_[setup]_when_[action]_then_[expected_result]` naming pattern:

```cpp
void given_valid_httpd_config_when_httpd_start_is_called_then_returns_success(void) {
    // Arrange
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t handle = NULL;

    // Act
    esp_err_t ret = httpd_start(&handle, &config);

    // Assert
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_NOT_NULL(handle);

    // Cleanup
    httpd_stop(handle);
}
```

### Unity Framework Usage
- **Single Responsibility**: One test function per specific behavior/scenario
- **Assertions**: Use `TEST_ASSERT_*` macros for clear failure messages (avoid `TEST_ASSERT_TRUE/FALSE`)
- **Runner Pattern**: Each category `.cpp` file includes a runner function with `RUN_TEST()` calls returning 0
- **File Separation**: Test implementations in `.cpp`, runner declarations in matching `.h` files
- **No Extern Setup/Teardown**: Avoid declaring extern setup/teardown functions in `test_esp_http_server.cpp`. Each category should manage its own test initialization and cleanup using the manual per-test setup/teardown pattern. This maintains test isolation and prevents cross-category dependencies.

## Setup and Teardown

The test suite uses a distributed approach to setup and teardown where initialization and cleanup occurs within individual test functions rather than relying on Unity's global setUp/tearDown mechanism. This approach provides better test isolation and resource management.

### Global SetUp/Teardown - Platform Only

The global `setUp(void)` and `tearDown(void)` functions in `test_esp_http_server.cpp` are used for platform-specific initialization only:

```cpp
void setUp(void) {
    #ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);  // Windows socket initialization
    #endif
}

void tearDown(void) {
    #ifdef _WIN32
        WSACleanup();  // Windows socket cleanup
    #endif
}
```

**Purpose:**
- Cross-platform networking setup (Windows MINGW64 only)
- Global state that affects all tests
- Not used for HTTP server or test fixture management

### Per-Test Setup/Teardown - Manual Pattern

Setup and teardown is handled manually within each test function. Each test performs its own initialization and cleanup:

```cpp
void given_valid_httpd_config_when_httpd_start_is_called_then_returns_success(void)
{
    // Setup: Manually initialize resources
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;  // Use unique port to avoid conflicts
    httpd_handle_t handle = NULL;
    esp_err_t ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Test logic...

    // Teardown: Manually cleanup resources
    httpd_stop(handle);  // Always cleanup, runs even if test fails above
}
```

**Key Patterns:**
- **Server Lifecycle**: Each test calls `httpd_start()`/`httpd_stop()` explicitly
- **Unique Ports**: Tests use different port numbers (8080, 8081, etc.) to avoid conflicts
- **Resource Management**: Tests handle their own memory allocation (`malloc`/`else`), socket creation, etc.
- **Cleanup Assurance**: Teardown occurs at end of test function, ensuring cleanup even if assertions fail

### Alternative Pattern: Test Fixtures with Static Functions

For complex test scenarios, category files may use static helper functions for setup/teardown logic that can be reused across multiple tests:

```cpp
// Static helper functions for test fixture management
static httpd_handle_t test_server_create(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    httpd_handle_t handle = NULL;
    esp_err_t ret = httpd_start(&handle, &config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);  // Fail test if server creation fails
    return handle;
}

static void test_server_destroy(httpd_handle_t handle) {
    if (handle != NULL) {
        httpd_stop(handle);
    }
}

void given_condition_when_testing_feature_then_expect_result(void) {
    // Setup using helper function
    httpd_handle_t server = test_server_create(8080);

    // Test logic...

    // Teardown using helper function
    test_server_destroy(server);
}
```

**When to Use Static Helpers:**
- Complex server configurations reused across multiple tests
- Mock object setup and teardown patterns
- Network/socket initialization that doesn't vary between tests
- Resource allocation that follows consistent patterns

### Implementation Guidelines

- **No Category-Level Setup/Teardown**: Individual category `.cpp` files do not implement Unity's `setUp()`/`tearDown()` functions
- **Test Isolation**: Each test must manage its own complete lifecycle to ensure independence
- **Port Management**: Use sequential unique ports (8080, 8081, 8082, etc.) across tests to prevent conflicts
- **Resource Safety**: Always allocate and free resources within the same test function
- **Platform Awareness**: Global setup handles platform differences; tests focus on functional behavior
- **Cleanup Reliability**: Place teardown code at function end so it executes regardless of test failure

### Integration with Test Categories

Categories organize tests by functional area but each test remains self-contained:

```
test_server_lifecycle.cpp        # Tests that handle their own httpd_start()/httpd_stop()
test_request_processing.cpp      # Tests that manage their own resources
test_client_management.cpp       # Tests using unique ports and manual lifecycle management

# ALL tests rely on global setUp() for Windows networking only
```

### Category Runner Design

Each test category follows this design pattern:

**File Structure**:
```
test_category.h        # Runner function declaration
test_category.cpp      # Test implementations and runner definition
```

**Runner Function Signature**:
```cpp
int test_category(void) {
    UnitySetTestFile(__FILE__);  // Sets source file for error reporting

    // Execute individual tests
    RUN_TEST(test_function_name_1);
    RUN_TEST(test_function_name_2);
    // ...

    return 0;  // Always return 0 on success
}
```

**Individual Test Functions**:
```cpp
void given_specific_setup_when_action_performed_then_expected_result(void) {
    // Arrange - Set up test prerequisites
    // Act - Perform the action being tested
    // Assert - Verify expected outcomes
    // Cleanup - Release resources if needed
}
```

**Error Handling**: The global runner aggregates return values from all category runners. If any individual test fails, Unity handles the failure reporting and the process exits with non-zero code.

### Cross-Platform Testing
Tests run on Windows (MINGW64), Linux, and ESP32 platforms:
- Use cross-platform abstractions from `lib/http-server/src/port`
- Avoid platform-specific dependencies (no Windows Sockets, FreeRTOS-specific code)
- Use appropriate socket/networking abstractions for testing

## Adding New Categories

### When to Create New Categories
- Major functional area not covered by existing categories
- Maintains single responsibility principle
- Follows existing naming patterns
- Requires independence from other categories

### Workflow
1. **Create implementation file** (`test_new_feature.cpp`):
   ```cpp
   // Include Unity and necessary headers first
   // Use BDD-style test function names
   // Implement runner function: int test_new_feature(void)
   // Use RUN_TEST() for each test function
   // Return 0 on success
   ```

2. **Create header file** (`test_new_feature.h`):
   ```cpp
   #ifndef _TEST_NEW_FEATURE_H_
   #define _TEST_NEW_FEATURE_H_
   int test_new_feature(void);
   #endif
   ```

3. **Update global runner** (`test_esp_http_server.cpp`):
   - Add `#include "test_new_feature.h"`
   - Add call to `test_new_feature()` in `test_esp_http_server()` function

4. **Update documentation**:
   - Add entry to category table above
   - Update runner execution hierarchy diagram

## Execution

### Full Test Suite
```bash
pio test -e native -vvv
```

### Individual Categories
Modify `test_esp_http_server.cpp` to comment out unwanted category runners, then run:
```bash
pio test -e native -vvv
```

### Maintenance Notes
- Follow strict AGENTS.md rules: Never test private functions directly
- Use public APIs to test installed functionality
- Never delete failed tests - comment them out with explanation
- Debug with `pio test -e native -vvv` for full verbosity
