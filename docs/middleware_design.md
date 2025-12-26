# ESP HTTP Server Middleware Framework Design Document

## Version 1.1

### Revision History

| Version | Date | Description |
|---------|------|-------------|
| 1.0 | 2025-12-11 | Initial design document (Integrated approach) |
| 1.1 | 2025-12-11 | Revised for standalone component approach |

### Authors
- AI Assistant (based on ESP HTTP Server codebase analysis)

## Alternative Architecture: Standalone Middleware Component

### Overview

This revision presents an alternative design where middleware is implemented as a standalone library/component with zero changes to the ESP HTTP Server core. Instead of integrating middleware execution into `httpd_uri.c`, middleware wraps handler functions at the application layer.

### Key Differences from Version 1.0

- **No core server modifications**: http-server library remains untouched
- **Handler wrapping pattern**: Middleware creates wrapper functions around handlers
- **Application-level integration**: Middleware is applied when registering handlers
- **Trade-off**: Less efficient but more modular and fully backward compatible

### Comparison: Integrated vs Standalone Architecture

| Aspect | Version 1.0 (Integrated) | Version 1.1 (Standalone) |
|--------|--------------------------|--------------------------|
| **Server Changes** | Modifies `httpd_uri.c`, adds to `esp_httpd_priv.h` | No changes to http-server |
| **Performance Overhead** | Linked list traversal per request | Function pointer indirection per wrapped handler |
| **Memory Usage** | Server stores middleware list | Middleware storage per wrapped handler |
| **Flexibility** | Server-wide middleware stack | Per-handler middleware chains |
| **Backward Compatibility** | No changes to existing apps | 100% compatible |
| **Testing Scope** | Requires changes to server test suite | Testable as separate component |
| **Deployment Complexity** | Requires http-server library update | Application adds middleware library |

## Table of Contents

1. [Introduction](#introduction)
2. [Requirements Analysis](#requirements-analysis)
3. [Architecture Overview](#architecture-overview)
4. [API Design](#api-design)
5. [Integration Points](#integration-points)
6. [Middleware Execution Model](#middleware-execution-model)
7. [Use Cases & Examples](#use-cases--examples)
8. [Security Considerations](#security-considerations)
9. [Performance Considerations](#performance-considerations)
10. [Testing Strategy](#testing-strategy)
11. [Implementation Plan](#implementation-plan)
12. [Backward Compatibility](#backward-compatibility)
13. [Limitations & Future Extensions](#limitations--future-extensions)

## 1. Introduction

This document describes the design of a middleware framework for the ESP HTTP Server library. The middleware system enables pluggable, reusable components for handling cross-cutting concerns such as authentication, security, logging, compression, and HTTP feature compliance.

### 1.1 Purpose

The middleware framework addresses critical gaps identified in RFC compliance analysis and standards.md, particularly around missing HTTP/1.1+ features and security validations. It provides a consistent way to add functionality that applies to multiple requests without modifying individual handler code.

### 1.2 Scope

This design covers:
- Middleware registration and execution
- Priority-based ordering
- Conditional execution based on URI patterns and HTTP methods
- Context management integration
- Testing infrastructure

### 1.3 Goals

- **Modularity**: Clean separation of concerns from business logic
- **Reusability**: Middleware components can be shared across projects
- **Performance**: Minimal overhead when no middleware is registered
- **Security**: Enable security-related middleware for HTTP compliance
- **Extensibility**: Easy to add new middleware without core changes

## 2. Requirements Analysis

### 2.1 Functional Requirements

**REQ-1**: Middleware must support authentication, security validation, logging, compression, and HTTP feature implementations.

**REQ-2**: Middleware must execute in configurable order with priority control.

**REQ-3**: Middleware must support conditional execution based on URI patterns and HTTP methods.

**REQ-4**: Middleware must have access to session and global contexts.

**REQ-5**: Middleware must be able to short-circuit request processing by returning early.

**REQ-6**: Middleware must integrate seamlessly with existing request/response APIs.

### 2.2 Non-Functional Requirements

**NFR-1**: Minimal performance overhead when middleware is disabled or not present.

**NFR-2**: Full backward compatibility with existing ESP HTTP Server applications.

**NFR-3**: Memory efficiency for embedded systems.

**NFR-4**: Thread-safety within the server's threading model.

### 2.3 Use Case Requirements

Based on standards.md analysis, middleware should support:

- **Authentication**: WWW-Authenticate, Authorization header processing
- **Security**: CRLF injection prevention, request smuggling protection
- **Conditional Requests**: ETag, If-Match header handling
- **Range Requests**: Range header parsing and validation
- **Content Negotiation**: Accept* header processing
- **Compression**: Content-Encoding handling
- **Logging**: Request/response logging and metrics

## 3. Architecture Overview

### 3.1 System Context

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   HTTP Client   │────▶│   ESP HTTP      │────▶│   Application   │
│                 │     │   Server        │     │   Handlers      │
└─────────────────┘     └─────────────────┘     └─────────────────┘
                               │
                               ▼
                       ┌─────────────────┐
                       │   Middleware    │
                       │   Framework     │
                       └─────────────────┘
```

### 3.2 Component Architecture

```
ESP HTTP Server
├── Core Server (httpd_main.c, httpd_sess.c, etc.)
├── URI Handler System (httpd_uri.c)
│   └── ┌─────────────────────────────────────┐
│       │         Middleware Framework         │
│       │ ┌───────────────┬─────────────────┐ │
│       │ │ Registration  │ Execution       │ │
│       │ │ API           │ Engine          │ │
│       │ │ (Generic      │ (Calls          │ │
│       │ │ Interface)    │ Registered      │ │
│       │ └───────────────┴─────────────────┘ │
│       └─────────────────────────────────────┘
├── Request/Response APIs (httpd_txrx.c)
├── Parser (httpd_parse.c)
└── Test Infrastructure
    └── test_middleware.cpp

ARCHITECTURAL CONSTRAINTS:
• Core server (httpd_uri.c) never includes specific middleware headers
• Core server calls middleware through generic registration interface
• Specific middleware libraries register functions but are not linked into core
• Middleware libraries can reference core server APIs (session mgmt, etc.)
• This maintains unidirectional dependency: middleware → core server
```

### 3.3 Execution Flow (Standalone Component)

```
Application Layer (Using Middleware Component)
1. ┌─────────────────────────────────────────────────────────┐
   │ Application Code                                        │
   │                                                         │
   │ httpd_uri_t my_handler = {                              │
   │     .uri = "/api/data",                                │
   │     .method = HTTP_GET,                                │
   │     .handler = my_actual_handler                       │
   │ };                                                     │
   │                                                         │
   │ // Instead of direct registration:                     │
   │ // httpd_register_uri_handler(handle, &my_handler);    │
   │                                                         │
   │ // Use middleware component:                           │
   │ httpd_register_middleware_handler(handle, &my_handler, │
   │                                    &middleware_chain); │
   │ └─────────────────────────────────────────────────────────┘
                                  │
                                  ▼

HTTP Server Core (Unmodified)
2. HTTP Request Received
   ↓
3. Parse Request (httpd_parse_req)
   ↓
4. URI Resolution (httpd_uri)
   │
   ├─→ Find matching URI handler (actually middleware wrapper)
   ↓
5. ┌─────────────────────────────────┐
   │   Wrapper Handler               │
   │   (Generated by Middleware Lib) │
   │                                 │
   │   ┌─────────────────────────┐   │
   │   │ Middleware 1 (Pri: 1)  │   │
   │   │ ↓                     │   │
   │   │ Middleware 2 (Pri: 2)  │   │
   │   │ ↓                     │   │
   │   │ ...                   │   │
   │   │ ↓                     │   │
   │   │ Middleware N (Pri: N) │   │
   │   └─────────────────────────┘   │
   │                                 │
   │   ↓ (If all middleware pass)    │
   │   Execute Original Handler      │
   └─────────────────────────────────┘
   ↓
6. Respond to client
   ↓
7. Cleanup (httpd_req_delete)
```

## 4. API Design

### 4.1 Core Data Structures

#### 4.1.1 Middleware Function Signature

```c
/**
 * @brief Middleware function type
 *
 * @param req HTTP request structure
 * @param uri Matching URI handler (NULL if no match found yet)
 * @param ctx User context passed during registration
 *
 * @return ESP_OK to continue processing, error code to short-circuit
 */
typedef esp_err_t (*httpd_middleware_func_t)(httpd_req_t *req,
                                           httpd_uri_t *uri,
                                           void *ctx);
```

#### 4.1.2 Middleware Configuration

```c
typedef struct httpd_middleware_config {
    httpd_middleware_func_t func;        /**< Middleware function */
    void *context;                       /**< User context for middleware */
    httpd_free_ctx_fn_t free_ctx;       /**< Context cleanup function */
    int priority;                        /**< Execution priority (lower = earlier) */
    const char *uri_pattern;             /**< URI pattern filter (wildcard support) */
    httpd_method_t method_filter;        /**< HTTP method filter (HTTP_ANY for all) */
    bool enabled;                        /**< Enable/disable flag */
} httpd_middleware_config_t;
```

#### 4.1.3 Internal Middleware Structure

```c
typedef struct httpd_middleware {
    httpd_middleware_func_t func;
    void *context;
    httpd_free_ctx_fn_t free_ctx;
    int priority;
    char *uri_pattern;                   /**< Duplicated for safe storage */
    httpd_method_t method_filter;
    bool enabled;
    SLIST_ENTRY(httpd_middleware) next;  /**< Linked list for priority sorting */
} httpd_middleware_t;
```

### 4.2 Public API Functions

#### 4.2.1 Registration Functions

```c
/**
 * @brief Register a middleware function
 *
 * @param handle Server handle
 * @param config Middleware configuration
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_register_middleware(httpd_handle_t handle,
                                   const httpd_middleware_config_t *config);

/**
 * @brief Unregister a middleware function
 *
 * @param handle Server handle
 * @param func Middleware function to remove
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_unregister_middleware(httpd_handle_t handle,
                                     httpd_middleware_func_t func);

/**
 * @brief Enable/disable middleware
 *
 * @param handle Server handle
 * @param func Middleware function
 * @param enabled Enable/disable flag
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t httpd_middleware_set_enabled(httpd_handle_t handle,
                                      httpd_middleware_func_t func,
                                      bool enabled);
```

#### 4.2.2 Query Functions

```c
/**
 * @brief Count registered middleware
 *
 * @param handle Server handle
 *
 * @return Number of registered middleware
 */
size_t httpd_get_middleware_count(httpd_handle_t handle);

/**
 * @brief Check if middleware is enabled
 *
 * @param handle Server handle
 * @param func Middleware function
 *
 * @return true if enabled, false otherwise
 */
bool httpd_is_middleware_enabled(httpd_handle_t handle,
                                 httpd_middleware_func_t func);
```

## 5. Integration Points

### 5.1 http_server.h Header

```c
// Existing includes...
#include "esp_http_server.h"

// New middleware API declarations
typedef esp_err_t (*httpd_middleware_func_t)(httpd_req_t *req,
                                           httpd_uri_t *uri,
                                           void *ctx);

typedef struct httpd_middleware_config {
    // ... as defined above
} httpd_middleware_config_t;

// API function declarations...
esp_err_t httpd_register_middleware(httpd_handle_t handle,
                                   const httpd_middleware_config_t *config);
// ... other declarations
```

### 5.2 httpd_data Structure Extension

```c
// In esp_httpd_priv.h
struct httpd_data {
    // Existing fields...
    httpd_config_t config;
    httpd_uri_t **hd_calls;

    // New middleware fields
    SLIST_HEAD(, httpd_middleware) middleware_list;  /**< Priority-ordered list */
    size_t middleware_count;                         /**< Number of registered middleware */
};
```

### 5.3 httpd_uri.c Modifications

#### 5.3.1 After URI Handler Found

```c
esp_err_t httpd_uri(struct httpd_data *hd)
{
    // ... existing URI finding logic ...

    /* If URI with method not found, respond with error code */
    if (uri == NULL) {
        // ... existing error handling ...
    }

    /* Attach user context data (passed during URI registration) into request */
    req->user_ctx = uri->user_ctx;

    // NEW: Execute middleware stack
    esp_err_t middleware_result = httpd_execute_middleware_stack(hd, req, uri);
    if (middleware_result != ESP_OK) {
        return middleware_result;
    }

#ifdef CONFIG_HTTPD_WS_SUPPORT
    // ... existing WebSocket handling ...
#endif

    /* Invoke handler */
    if (uri->handler(req) != ESP_OK) {
        /* Handler returns error, this socket should be closed */
        LOGW(TAG, LOG_FMT("uri handler execution failed"));
        return ESP_FAIL;
    }
    return ESP_OK;
}
```

#### 5.3.2 New Core Function

```c
/**
 * @brief Execute middleware stack for a request
 */
static esp_err_t httpd_execute_middleware_stack(struct httpd_data *hd,
                                              httpd_req_t *req,
                                              httpd_uri_t *uri)
{
    httpd_middleware_t *middleware;

    SLIST_FOREACH(middleware, &hd->middleware_list, next) {
        // Skip disabled middleware
        if (!middleware->enabled) {
            continue;
        }

        // URI pattern filtering (if specified)
        if (middleware->uri_pattern) {
            struct http_parser_url *res = &req->aux->url_parse_res;
            size_t uri_len = 0;
            const char *req_uri = req->uri;

            if (res->field_set & (1 << UF_PATH)) {
                req_uri += res->field_data[UF_PATH].off;
                uri_len = res->field_data[UF_PATH].len;
            } else {
                uri_len = strlen(req_uri);
            }

            if (!httpd_uri_match_wildcard(middleware->uri_pattern, req_uri, uri_len)) {
                continue;
            }
        }

        // HTTP method filtering
        if (middleware->method_filter != HTTP_ANY &&
            middleware->method_filter != req->method) {
            continue;
        }

        // Execute middleware
        LOGD(TAG, LOG_FMT("executing middleware (pri: %d)"), middleware->priority);
        esp_err_t ret = middleware->func(req, uri, middleware->context);
        if (ret != ESP_OK) {
            LOGD(TAG, LOG_FMT("middleware short-circuited request (err: %d)"), ret);
            return ret;
        }
    }

    return ESP_OK;
}
```

## 6. Middleware Execution Model

### 6.1 Execution Phases

**Pre-Handler Phase**: Middleware executes before the URI handler
- Access to complete parsed request
- Can modify request data or headers
- Can set session/global context for handler use
- Can return early to bypass handler

**Post-Handler Phase**: Not implemented (can be added later if needed)
- Would execute after handler completion
- Could modify response before sending

### 6.2 Execution Order (Registration Order)

- **Execution Order**: Middleware executes in the order provided in the `configs` array
- **Registration Order**: First middleware in array executes first, followed by subsequent ones
- **Predictability**: Order is deterministic based on how middleware configs are passed to wrapper function
- **No Reordering**: Order cannot be changed after wrapper creation

### 6.3 Filtering Logic

Middleware executes if:
1. **Pattern Matching**: `uri_pattern` is NULL or matches request URI
2. **Method Matching**: `method_filter` is HTTP_ANY or matches request method
3. **Enabled**: `enabled` flag is true

### 6.4 Error Handling

- **Short-circuit**: Middleware can return error code to stop processing
- **Handler Execution**: If middleware succeeds, handler always executes
- **Cleanup**: Error responses handled by existing `httpd_req_handle_err()`

## 7. Use Cases & Examples

### 7.1 Authentication Middleware

```c
static esp_err_t auth_middleware(httpd_req_t *req, httpd_uri_t *uri, void *ctx)
{
    // Skip auth for public endpoints
    if (strstr(req->uri, "/public/")) {
        return ESP_OK;
    }

    // Check Authorization header
    char auth_header[256];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Protected Area\"");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Validate credentials and store user info in session context
    // ... auth logic ...

    return ESP_OK;
}

// Registration
httpd_middleware_config_t auth_config = {
    .func = auth_middleware,
    .context = NULL,
    .priority = 10,  // Execute early
    .uri_pattern = NULL,  // All URIs
    .method_filter = HTTP_ANY,
    .enabled = true
};
```

### 7.2 Security Middleware (CRLF Injection Prevention)

```c
static esp_err_t security_middleware(httpd_req_t *req, httpd_uri_t *uri, void *ctx)
{
    // Check for CRLF in headers
    if (httpd_contains_crlf_in_headers(req)) {
        LOGW("CRLF", "Injection attempt detected");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
        return ESP_FAIL;
    }

    return ESP_OK;
}
```

### 7.3 Logging Middleware

```c
static esp_err_t logging_middleware(httpd_req_t *req, httpd_uri_t *uri, void *ctx)
{
    LOGI("HTTP", "Request: %s %s from %s",
         http_method_str(req->method),
         req->uri,
         get_client_ip(req));

    // Store start time in session context for post-handler timing

    return ESP_OK;
}
```

### 7.4 CORS Middleware

```c
static esp_err_t cors_middleware(httpd_req_t *req, httpd_uri_t *uri, void *ctx)
{
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_set_status(req, HTTPD_200);
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type,Authorization");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;  // Handled OPTIONS request, return without calling handler
    }

    // For non-OPTIONS requests, add CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    return ESP_OK;
}
```

## 8. Security Considerations

### 8.1 Input Validation

- Middleware must validate all user inputs to prevent injection attacks
- Buffer overflow protection in header processing
- Safe memory handling for context data

### 8.2 Denial of Service

- Resource exhaustion through excessive header parsing
- Memory leaks from context allocation
- Infinite loops in middleware functions

### 8.3 Information Disclosure

- Avoid logging sensitive authentication data
- Limit timestamp/header disclosures for security
- Safe error message handling

### 8.4 Execution Security

- Middleware cannot assume trusted input
- Handle malformed HTTP requests gracefully
- Timeouts for middleware operations

## 9. Performance Considerations

### 9.1 Overhead Analysis

- **Zero-cost when disabled**: No middleware = direct handler call
- **Minimal overhead when enabled**: Linked list traversal + string comparisons
- **Memory impact**: ~40 bytes per middleware + context storage

### 9.2 Optimization Strategies

- **Pattern caching**: Pre-compile wildcard patterns
- **Bitmap filtering**: Convert method filters to bitmaps
- **Early exit**: High-priority middleware can short-circuit expensive operations
- **Memory pooling**: Reuse context structures

### 9.3 Benchmarks

Expected overhead:
- **Disabled**: < 1μs per request
- **3 middleware**: < 5μs per request
- **10 middleware**: < 15μs per request

Scales linearly with number of middleware based on filtering cost.

## 10. Testing Strategy

### 10.1 Unit Tests

**test_middleware.cpp** should include:

```c
// Basic registration/unregistration
void test_middleware_registration();

// Priority ordering
void test_middleware_priority_execution();

// URI pattern filtering
void test_middleware_uri_filtering();

// Method filtering
void test_middleware_method_filtering();

// Short-circuit behavior
void test_middleware_short_circuit();

// Enable/disable functionality
void test_middleware_enable_disable();
```

### 10.2 Integration Tests

**End-to-end testing with http_test_client:**

```c
// Authentication middleware E2E test
void test_auth_middleware_end_to_end();

// Security middleware E2E test
void test_security_middleware_blocks_injection();

// CORS middleware E2E test
void test_cors_middleware_handles_options();

// Performance regression test
void test_middleware_performance_overhead();
```

### 10.3 Test Categories

Following existing test structure:
- **test_middleware_basic.cpp**: Basic functionality
- **test_middleware_security.cpp**: Security-focused middleware
- **test_middleware_auth.cpp**: Authentication middleware
- **test_middleware_performance.cpp**: Performance benchmarking

## 11. Implementation Plan

### Phase 1: Core Framework (Week 1)

1. **Data Structures**: Add middleware structures to esp_httpd_priv.h
2. **API Headers**: Update http_server.h with middleware declarations
3. **Registration Logic**: Implement httpd_register_middleware/unregister_middleware
4. **Execution Engine**: Implement httpd_execute_middleware_stack in httpd_uri.c

### Phase 2: Filtering & Context (Week 2)

1. **Pattern Matching**: Add URI wildcard filtering
2. **Method Filtering**: Add HTTP method filtering
3. **Context Management**: Add context cleanup support
4. **Enable/Disable**: Add runtime enable/disable functionality

### Phase 3: Example Middleware (Week 3)

1. **Security Middleware**: CRLF injection prevention
2. **Auth Middleware**: Basic authentication framework
3. **Logging Middleware**: Request/response logging
4. **CORS Middleware**: Cross-origin request handling

### Phase 4: Testing & Documentation (Week 4)

1. **Unit Tests**: Complete middleware framework tests
2. **Integration Tests**: End-to-end testing with client
3. **Performance Tests**: Overhead measurement and optimization
4. **Documentation**: API docs and examples

### Phase 5: Standards Compliance (Week 5)

1. **HTTP/1.1 Features**: ETag, conditional request middleware
2. **Content Negotiation**: Accept headers processing
3. **Range Requests**: Range header parsing/validation
4. **Compression**: Content-Encoding middleware

## 12. Backward Compatibility

### 12.1 Zero Impact on Existing Code

- **No breaking changes**: Existing applications continue to work unchanged
- **Optional feature**: Must be explicitly enabled/registered by applications
- **Memory allocation**: Only allocated when middleware registered
- **Performance**: No overhead when not used

### 12.2 API Extensions

- **New functions only**: No modifications to existing APIs
- **Consistent naming**: Follows existing httpd_* naming convention
- **Error handling**: Uses existing ESP-IDF error codes

### 12.3 Migration Path

For applications wanting to use middleware:

1. **Add middleware declarations** to application code
2. **Register middleware** during server setup (after httpd_start)
3. **Test thoroughly** for any interaction with existing handlers
4. **Monitor performance** with middleware enabled

## 13. Limitations & Future Extensions

### 13.1 Current Limitations

- **Post-handler middleware**: Only pre-handler execution supported
- **URI re-resolution**: Cannot change matched URI from middleware
- **Dynamic reordering**: Priority changes require re-registration
- **WebSocket integration**: Limited middleware support for WebSocket requests

### 13.2 Future Enhancements

- **Response middleware**: Post-handler modification of responses
- **Dynamic priorities**: Runtime priority adjustment
- **Middleware groups**: Named middleware collections
- **Async middleware**: Non-blocking middleware operations
- **Middleware pipelines**: Conditional branching in execution
- **Configuration files**: Declarative middleware configuration

### 13.3 ESP-IDF Integration

- **Kconfig options**: Enable/disable middleware framework
- **Memory monitoring**: Integration with ESP-IDF heap tracing
- **Power management**: Consideration for low-power applications
- **SSL/TLS awareness**: Integration with esp_https_server

## 14. Conclusion

The middleware framework provides a robust, performant solution for adding cross-cutting functionality to the ESP HTTP Server. It addresses critical gaps in HTTP standards compliance while maintaining backward compatibility and following established ESP-IDF patterns.

The design enables modular application development where common functionality like authentication, security, and logging can be shared or customized across projects. The priority-based execution model ensures flexibility while maintaining predictable behavior.

Implementation should proceed in phases, starting with the core framework and gradually adding feature-complete middleware that addresses the standards.md compliance gaps.
