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
esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);

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

## Security Best Practices

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