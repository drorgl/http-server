# HTTP Server Coding Standards and Best Practices

This document defines essential coding standards and best practices for the HTTP server project. For detailed information, refer to the [Development Guide](./development.md).

## Quick Reference

### Code Style
- **Indentation**: 4 spaces (no tabs)
- **Line Length**: Maximum 120 characters
- **Language**: C99 standard
- **Naming**: snake_case for C, camelCase for C++

### Naming Conventions
```c
// Functions
esp_err_t httpd_start(httpd_handle_t *handle, httpd_config_t *config);

// Variables
httpd_handle_t server_handle;
const char *content_type;

// Constants
#define HTTPD_DEFAULT_PORT 80
#define HTTPD_MAX_URI_HANDLERS 10

// Types
typedef struct httpd_data httpd_data_t;
```

### Header Guards
```c
#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP_HTTP_SERVER_H
#define ESP_HTTP_SERVER_H

// Header content

#endif /* ESP_HTTP_SERVER_H */

#ifdef __cplusplus
}
#endif
```

## Data Structure Safety

### Struct Initialization Best Practices
**🔒 CRITICAL REQUIREMENT**: All struct variables in tests and initialization code MUST be explicitly zero-initialized to prevent memory safety vulnerabilities.

```c
// ✅ CORRECT: Zero-initialize structs to prevent uninitialized pointer crashes
uint16_t setup_websocket_server(httpd_handle_t *handle, httpd_uri_t *ws_uri, esp_err_t (*handler)(httpd_req_t *))
{
    memset(ws_uri, 0, sizeof(httpd_uri_t));  // Critical: Prevents _strdup on garbage pointers

    ws_uri->uri = "/ws/ws_fragmentation";
    ws_uri->method = HTTP_GET;
    ws_uri->handler = handler;
    // Optional fields (supported_*) are now safely NULL instead of garbage
}

// ❌ INCORRECT: Uninitialized struct leads to SIGSEGV in _strdup
void broken_setup_websocket_server(httpd_handle_t *handle, httpd_uri_t *ws_uri, esp_err_t (*handler)(httpd_req_t *))
{
    // ws_uri->supported_subprotocol contains random memory address!
    ws_uri->uri = "/ws/ws_fragmentation";  // Only initializes required fields
    ws_uri->method = HTTP_GET;
    // CRASH: httpd_register_uri_handler calls _strdup on uninitialized pointer
}

httpd_uri_t uri;  // ❌ Never leave structs uninitialized in tests
httpd_uri_t uri = {0};  // ✅ Always zero-initialize

// For production API structs passed by caller:
// Document requirement: "Callers MUST zero-initialize struct before use"
// Add runtime validation if necessary:
// if (uri.supported_subprotocol != NULL) { validate_pointer_logic(); }
```

**Impact of Non-Compliance**: Segmentation faults, use-after-free, unpredictable crashes during struct field access.

**Cross-Platform Note**: Memory corruption bugs may manifest differently on different platforms (MINGW64 vs ESP32 vs Linux).

## Testing Standards

### Test Organization
Based on [`test/test_esp_http_server/test.md`](../test/test_esp_http_server/test.md):

**Test Categories:**
1. Server Lifecycle Tests
2. URI Handler Management Tests
3. Request Processing Tests
4. Response Handling Tests
5. WebSocket Tests
6. Client Management Tests
7. Error Handling Tests
8. Utility Tests
9. Async Tests

### Test Requirements
- **Cross-Platform**: Must work on Windows, Linux, embedded
- **No Platform-Specific Libraries**: Avoid Windows Sockets, FreeRTOS APIs
- **Unity Framework**: Use `void test_function(){}` and `RUN_TEST`
- **Single Behavior**: Each test verifies one specific behavior
- **Arrange-Act-Assert**: Follow AAA pattern

### Test Naming (Given-When-Then)
```c
void test_given_valid_config_when_httpd_start_called_then_returns_success(void)
{
    // Given: Setup
    httpd_handle_t handle = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // When: Action
    esp_err_t result = httpd_start(&handle, &config);
    
    // Then: Assertion
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_NOT_NULL(handle);
}
```

## API Contract Standards

### Output Parameter Population
**🚨 CRITICAL REQUIREMENT**: Functions must populate all output parameters before return, even in partial success or header-only operations.

```c
// ✅ CORRECT: Always populate output parameters
esp_err_t httpd_ws_recv_frame(httpd_req_t *req, httpd_ws_frame_t *frame, size_t max_len)
{
    // ... parsing logic ...
    if (max_len == 0) {
        // Header-only mode - MUST populate metadata
        frame->len = parsed_length;
        frame->type = parsed_type;
        frame->final = parsed_final_flag;
        return ESP_OK;  // Fields are now populated
    }
    // ... payload handling ...
}

// ❌ INCORRECT: Leaving output parameters uninitialized
esp_err_t broken_ws_recv_frame(httpd_req_t *req, httpd_ws_frame_t *frame, size_t max_len)
{
    if (max_len == 0) {
        // Forgot to set frame->len, frame->type, frame->final!
        return ESP_OK;  // Calling code gets garbage values
    }
}
```

**Impact of Non-Compliance**: Empty responses, socket state corruption, test failures masking implementation bugs.

## Security Best Practices

### WebSocket Protocol Compliance
**🔒 CRITICAL REQUIREMENT**: All WebSocket close frames MUST include proper status codes per RFC 6455 Section 5.5.1 when terminating connections due to errors or violations.

```c
// ✅ CORRECT: Include status code for error closures
esp_err_t handle_ws_error(httpd_req_t *req)
{
    httpd_ws_frame_t close_frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 (Protocol Error) in big-endian
        .len = 2
    };
    httpd_ws_send_frame(req, &close_frame);
    return ESP_FAIL;
}

// ❌ INCORRECT: Missing status code (RFC violation)
esp_err_t bad_ws_error_handler(httpd_req_t *req)
{
    httpd_ws_frame_t close_frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_CLOSE,
        .payload = NULL,
        .len = 0  // Non-compliant
    };
    httpd_ws_send_frame(req, &close_frame);
    return ESP_FAIL;
}
```

**Common Close Codes**:
- `1002` (0x03EA): Protocol Error - Invalid framing or protocol violation
- `1003` (0x03EB): Unsupported Data - Data format not accepted
- `1007` (0x03EF): Invalid Frame Payload Data - Malformed UTF-8 or invalid data
- `1008` (0x03F0): Policy Violation - Endpoint policy violated (security violations)
- `1009` (0x03F1): Message Too Big - Frame exceeds size limits

### Input Validation
```c
esp_err_t httpd_register_uri_handler(httpd_handle_t handle,
                                    const httpd_uri_handler_t *handler)
{
    if (handle == NULL || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handler->uri == NULL || handler->method == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}
```

### Buffer Security
```c
// Good
char buffer[256];
snprintf(buffer, sizeof(buffer), "HTTP/1.1 %d %s\r\n", status_code, reason_phrase);
buffer[sizeof(buffer) - 1] = '\0';

// Avoid
char buffer[256];
sprintf(buffer, "HTTP/1.1 %d %s\r\n", status_code, reason_phrase);
```

### Memory Safety
```c
// Good
httpd_handle_t *handle = calloc(1, sizeof(httpd_handle_t));
if (handle == NULL) {
    return ESP_ERR_NO_MEM;
}
// Use handle...
free(handle);
handle = NULL;

// Avoid
httpd_handle_t *handle = malloc(sizeof(httpd_handle_t));
// No error checking
```

## Error Handling

### Error Codes
```c
#define ESP_ERR_HTTPD_INVALID_REQUEST   0x100
#define ESP_ERR_HTTPD_URI_NOT_FOUND     0x101
#define ESP_ERR_HTTPD_METHOD_NOT_ALLOWED 0x102

esp_err_t httpd_process_request(httpd_req_t *req)
{
    if (req == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}
```

### Error Propagation
```c
esp_err_t httpd_start_server(void)
{
    esp_err_t err = httpd_init();
    if (err != ESP_OK) {
        return err;
    }
    
    err = httpd_start(&server_handle, &config);
    if (err != ESP_OK) {
        httpd_deinit();
        return err;
    }
    
    return ESP_OK;
}
```

## Memory Management

### Allocation and Deallocation
```c
esp_err_t httpd_create_handler(const char *uri, httpd_uri_handler_t **out_handler)
{
    httpd_uri_handler_t *handler = calloc(1, sizeof(httpd_uri_handler_t));
    if (handler == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    handler->uri = strdup(uri);
    if (handler->uri == NULL) {
        free(handler);
        return ESP_ERR_NO_MEM;
    }
    
    *out_handler = handler;
    return ESP_OK;
}

void httpd_free_handler(httpd_uri_handler_t *handler)
{
    if (handler) {
        free((void *)handler->uri);
        free(handler);
    }
}
```

## Cross-Platform Compatibility

### Platform Abstraction
```c
#ifdef CONFIG_HTTPD_USE_POSIX_SOCKETS
    #include "port/posix_sockets.h"
#elif defined(CONFIG_HTTPD_USE_ESP_SOCKETS)
    #include "port/esp_sockets.h"
#endif

static int httpd_socket_create(void)
{
    return httpd_port_socket_create();
}
```

## Code Review Checklist

### Before Submitting
- [ ] Code follows style guidelines
- [ ] Input validation implemented
- [ ] Error handling is comprehensive
- [ ] Memory management is correct
- [ ] Security considerations addressed
- [ ] Tests added for new functionality

### Testing Verification
- [ ] All existing tests pass
- [ ] New tests follow naming conventions
- [ ] Tests are cross-platform compatible
- [ ] Memory leak tests pass

## References

- [Development Guide](./development.md)
- [Test Documentation](../test/test_esp_http_server/test.md)
- [PlatformIO Configuration](../platformio.ini)

---

**Note**: This document covers essential standards. For detailed implementation examples, refer to existing code in the project.
