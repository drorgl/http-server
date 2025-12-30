# HTTP/1.1 Connection Persistence and Keep-Alive Handling Design Document

## Overview

This document describes the design and implementation of HTTP/1.1 connection persistence and keep-alive handling middleware for the ESP HTTP Server library. Connection persistence is a fundamental HTTP/1.1 feature (RFC 9112 Sections 9.3-9.6) that allows multiple HTTP requests to be sent over a single TCP connection, improving performance by reducing connection overhead.

HTTP/1.1 defaults to persistent connections unless explicitly closed with `Connection: close` headers. This implementation adds middleware to properly manage connection lifecycle, keep-alive timeouts, and connection state transitions.

## Table of Contents

1. [Requirements Analysis](#requirements-analysis)
2. [RFC 9112 Compliance Analysis](#rfc-9112-compliance-analysis)
3. [Architecture Overview](#architecture-overview)
4. [API Design](#api-design)
5. [Implementation Details](#implementation-details)
6. [Security Considerations](#security-considerations)
7. [Testing Strategy](#testing-strategy)
8. [Performance Considerations](#performance-considerations)

## Requirements Analysis

### Functional Requirements

**REQ-PERSIST-1**: Server must maintain persistent connections by default (HTTP/1.1)
**REQ-PERSIST-2**: Server must honor `Connection: close` header to terminate connections
**REQ-PERSIST-3**: Server must support configurable keep-alive idle timeouts
**REQ-PERSIST-4**: Middleware must track connection state (persistent vs close)
**REQ-PERSIST-5**: Must integrate with existing session management (LRU, close callbacks)

### Non-Functional Requirements

**NFR-PERSIST-1**: Minimal overhead when connections are kept alive
**NFR-PERSIST-2**: Thread-safe operation within server's concurrency model
**NFR-PERSIST-3**: Configurable behavior through existing httpd_config_t
**NFR-PERSIST-4**: Cross-platform compatibility (MINGW64, Linux, ESP32)

## RFC 9112 Compliance Analysis

### RFC 9112 Section 9.3 - Connection Persistence

**Requirements Met:**
- ✅ **Default Persistence**: HTTP/1.1 connections remain open unless `Connection: close`
- ✅ **Connection Header Processing**: Parse and respond to `Connection` header values
- ✅ **Closure Propagation**: Echo `Connection: close` in response when requested

**Current Implementation Status:**
- ❌ **Connection Header Processing**: Not explicitly parsed/enforced
- ❌ **State Tracking**: No middleware-level connection state management
- ❌ **Version-Aware Persistence**: Need HTTP/1.0 vs HTTP/1.1 logic per RFC 9112 Section 9.3

**Precise RFC 9112 Algorithm:**
```
A recipient determines whether a connection is persistent or not based on:
1. If "close" connection option is present → NOT persistent
2. If received protocol is HTTP/1.1 (or later) → PERSISTENT
3. If received protocol is HTTP/1.0 + "keep-alive" option present
   + recipient is not a proxy or message is response
   + recipient wishes to honor HTTP/1.0 keep-alive → PERSISTENT
4. Otherwise → NOT persistent
```

### RFC 9112 Section 9.4 - Message Boundaries

**Requirements Met:**
- ✅ **Message Parsing**: Existing HTTP parser handles message boundaries
- ❓ **Connection Reuse Validation**: Needs verification for persistent connections

### RFC 9112 Section 9.5 - Connection Close

**Requirements Met:**
- ✅ **Connection Closure**: Core session management supports connection closure
- ❓ **Graceful Close Handling**: Requires testing with persistent connections

### RFC 9112 Section 9.6 - Connection Management (Enhanced)

**Requirements Met:**
- ✅ **Client-Initiated Closure**: Handles `Connection: close` from client
- ✅ **Server-Initiated Closure**: Supports server-side connection termination
- ❓ **Keep-Alive Behavior**: Partial support, needs comprehensive validation

**RFC 9112 Closure Rules:**
- Servers MUST send `Connection: close` when intending to close
- Clients MUST NOT send requests after sending/receiving `close`
- Servers MUST initiate teardown after `close` responses
- "Close" field name is reserved to prevent conflicts

## Architecture Overview

### System Context

```
HTTP Client --(TCP)-- ESP HTTP Server
    │                       │
    │   Multiple Requests   │
    │   on Same Connection  │
    └───(Connection: close)─┘
                    │
                    ▼
        Connection Persistence Middleware
                    │
                    ▼
        Existing Request Processing
```

### Component Architecture

```
Connection Persistence Layer
├── connection_middleware.c    (Middleware implementation)
├── connection_parser.c        (Connection header parsing)
├── connection_tracker.c       (Connection state management)
└── connection_timeout.c       (Keep-alive timeout handling)
```

### Integration with Existing Server

The middleware integrates with the existing middleware stack in `httpd_uri.c`:

```
httpd_uri() ──► execute_middleware_stack()
    │                       │
    ├─► connection_middleware()  [NEW - Pre-handler]
    │   │
    │   └─► Parse Connection headers
    │       Track connection state
    │       Set close flags if needed
    │
    ├─► URI handler execution
    │
    └─► response_middleware()   [Future enhancement]
```

## API Design

### Middleware Configuration

```c
typedef struct httpd_connection_config {
    bool enable_persistence;      /**< Enable persistence (default: true for HTTP/1.1) */
    bool honor_client_close;      /**< Honor client Connection: close (default: true) */
    uint32_t max_requests_per_conn; /**< Max requests per connection (0 = unlimited) */
    struct {
        uint32_t idle_timeout_sec;   /**< Idle timeout before closing (0 = use httpd_config) */
        uint32_t max_timeout_sec;    /**< Absolute max connection lifetime */
    } keep_alive;
} httpd_connection_config_t;
```

### Middleware Function Signature

```c
typedef struct httpd_connection_ctx {
    bool should_close;           /**< Connection should be closed after response */
    uint32_t request_count;      /**< Requests processed on this connection */
    time_t last_activity;        /**< Timestamp of last activity */
    char connection_header[32];  /**< Client's Connection header value */
} httpd_connection_ctx_t;

esp_err_t httpd_connection_middleware(httpd_req_t *req,
                                    httpd_uri_t *uri,
                                    void *ctx);
```

### Public API Functions

```c
/**
 * @brief Set connection persistence configuration
 */
esp_err_t httpd_config_connection_persistence(httpd_handle_t handle,
                                           const httpd_connection_config_t *config);

/**
 * @brief Force connection closure for current request
 */
esp_err_t httpd_connection_close_after_response(httpd_req_t *req);

/**
 * @brief Check if connection should remain persistent
 */
bool httpd_connection_is_persistent(httpd_req_t *req);
```

## Implementation Details

### Connection Header Processing

**Client Request Analysis:**
1. Parse `Connection` header from request
2. Check for `close` value (case-insensitive)
3. Store connection directive in middleware context
4. Set connection state flags

**Server Response Generation:**
1. Check if connection should be closed
2. Add `Connection: close` header to response if needed
3. Ensure proper header formatting

```c
static esp_err_t parse_connection_header(httpd_req_t *req,
                                       httpd_connection_ctx_t *ctx) {
    char connection_value[32] = {0};
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Connection",
                                               connection_value,
                                               sizeof(connection_value));

    if (err == ESP_OK) {
        // Case-insensitive comparison per RFC 9110
        if (strcasecmp(connection_value, "close") == 0) {
            ctx->should_close = true;
            strcpy(ctx->connection_header, "close");
        } else if (strcasecmp(connection_value, "keep-alive") == 0) {
            ctx->should_close = false;
            strcpy(ctx->connection_header, "keep-alive");
        }
    }

    // HTTP/1.0 default is close, HTTP/1.1 default is keep-alive
    if (strcmp(req->version, "HTTP/1.0") == 0 && ctx->connection_header[0] == '\0') {
        ctx->should_close = true;
    }

    return ESP_OK;
}
```

### Connection State Management

**State Tracking:**
- Track per-connection request count
- Monitor connection idle time
- Flag connections marked for closure
- Integrate with session management

**Timeout Handling:**
- Use configured keep-alive idle timeouts
- Track last activity timestamps
- Trigger connection closure on timeouts
- Respect server configuration limits

### Integration Points

**Session Context:**
- Store connection state in httpd_sess_get_ctx/set_ctx
- Persist across multiple requests
- Clean up on connection termination

**Response Headers:**
- Conditionally add `Connection: close` header
- Based on client request analysis
- Before sending response

## Security Considerations

### Denial of Service Protection

**Connection Flooding:**
- Limit concurrent persistent connections
- Enforce maximum requests per connection
- Implement connection lifetime limits

**Resource Exhaustion:**
- Monitor memory usage per connection
- Prevent excessive header storage
- Timeout idle connections aggressively

### Request Smuggling Prevention

**Connection State Isolation:**
- Ensure connection state doesn't leak between clients
- Proper cleanup on connection transitions
- Validate state consistency

### Information Disclosure

**Header Sanitization:**
- Don't echo sensitive connection headers
- Validate header values before processing
- Log connection events securely

## Testing Strategy

### Unit Testing

**test_connection_middleware.cpp:**
```c
// Basic functionality tests
void test_connection_header_parsing_close();
void test_connection_header_parsing_keep_alive();
void test_connection_default_behavior_http11();
void test_connection_default_behavior_http10();

// State management tests
void test_connection_state_tracking();
void test_connection_request_counting();
void test_connection_timeout_handling();

// Integration tests
void test_connection_middleware_with_server();
```

### Integration Testing

**Connection Persistence E2E Tests:**
```c
// Multiple requests on same connection
void test_multiple_requests_same_connection();

// Connection closure scenarios
void test_connection_close_header_respected();
void test_connection_close_on_timeout();

// Keep-alive behavior
void test_keep_alive_timeout_enforcement();
void test_keep_alive_max_requests_limit();
```

### Performance Testing

**Connection Overhead Tests:**
```c
// Measure connection setup/teardown overhead
void test_connection_persistence_overhead();

// Stress test with many concurrent connections
void test_connection_limit_stress_testing();
```

### Test Categories

Following existing test structure:
- **test_connection_middleware_basic.cpp**: Basic middleware functionality
- **test_connection_middleware_edge_cases.cpp**: Edge cases and error conditions
- **test_connection_e2e.cpp**: End-to-end connection testing
- **test_connection_performance.cpp**: Performance benchmarking

## Performance Considerations

### Optimized Execution

**Minimal Overhead:**
- Header parsing only when Connection header present
- Lazy state allocation in session context
- Fast-path for default HTTP/1.1 behavior

**Memory Efficiency:**
- Compact connection context structure (~64 bytes)
- Shared string constants for headers
- Automatic cleanup on connection termination

### Scalability

**Connection Limits:**
- Configurable concurrent connection limits
- LRU eviction for connection management
- Resource pooling for frequently used structures

### Benchmarks

Expected performance impact:
- **No connection headers**: < 1μs overhead
- **Connection: close processing**: < 5μs overhead
- **Keep-alive timeout checks**: < 10μs overhead

## Implementation Plan

### Phase 1: Core Middleware Framework (Week 1)
- Create middleware structure and basic functionality
- Implement header parsing logic
- Add connection state tracking

### Phase 2: Server Integration (Week 2)
- Integrate with httpd_uri.c middleware stack
- Add session context management
- Implement response header generation

### Phase 3: Timeout and Limits (Week 3)
- Add keep-alive timeout handling
- Implement connection limits
- Add resource management

### Phase 4: Testing and Validation (Week 4)
- Comprehensive unit test suite
- Integration and E2E tests
- Performance benchmarking
- RFC compliance verification

## RFC 9112 Compliance Checklist

- ✅ **9.3.1 Establishing Connections**: TCP connection establishment
- ✅ **9.3.2 Connection Reuse**: New connections for parallel requests
- ❌ **9.3.3 Connection Persistence**: Needs header processing enforcement
- ❌ **9.4 Message Boundaries**: Connection reuse validation needed
- ❌ **9.5 Connection Closure**: Comprehensive close testing needed
- ❌ **9.6 Connection Management**: Keep-alive implementation needed

## Conclusion

The connection persistence middleware addresses critical gaps in RFC 9112 compliance, specifically around HTTP/1.1's default persistent connection behavior. By implementing proper Connection header processing and connection state management, the ESP HTTP Server will correctly handle modern HTTP client expectations for connection reuse.

The design maintains backward compatibility while adding essential functionality for production HTTP server deployments. Thorough testing and performance optimization ensure the implementation is both correct and efficient for embedded systems.
