# 3-Layer Middleware Architecture for Connection Limiting and Rate Control

## Overview

This document presents a **3-Layer Progressive Defense Architecture** for implementing connection limiting and rate control in the ESP HTTP Server. The architecture builds upon the existing middleware framework while extending it to three distinct layers that execute at different stages of the HTTP connection lifecycle.

## 1. Architecture Overview

### Connection Lifecycle Integration

```
Server Loop
├── select() detects incoming connection
├── Session Management Layer (LRU capacity check)  ← Layer 0
├── TCP accept() creates connection
├── Connection Management Layer (IP limits, RST)    ← Layer 1
├── HTTP session established
├── HTTP request parsing
├── Request Management Layer (429 responses)       ← Layer 2
└── URI routing + existing middleware + handler
```

### Layer Responsibilities

#### Layer 0: Session Management
- **Purpose**: Infrastructure capacity protection
- **Execution**: Before `accept()` - server capacity management
- **Current Implementation**: LRU purge logic
- **Decision**: Can server handle ANY new connection?

#### Layer 1: Connection Management
- **Purpose**: TCP-level connection filtering and resource conservation
- **Execution**: After `accept()`, before HTTP session creation
- **Logic**: Per-IP connection counting, hard limits
- **Action**: Reject with TCP RST (immediate, no memory allocation)
- **Decision**: Should THIS specific connection be accepted?

#### Layer 2: Request Management
- **Purpose**: HTTP-level policy enforcement and user experience
- **Execution**: After HTTP header parsing, before request processing
- **Logic**: HTTP-aware request throttling with business context
- **Action**: HTTP 429 response with proper headers and graceful closure
- **Decision**: Should THIS specific HTTP request be processed?

## 2. Technical Implementation

### Layer Data Structures

#### Configuration Structure

```c
typedef struct httpd_config {
    // ... existing fields ...

    // 3-Layer Connection Protection
    struct {
        // Layer 0: Session Management
        bool enable_session_management;
        uint16_t max_sessions;
        bool lru_purge_enable;

        // Layer 1: Connection Management (TCP level)
        bool enable_connection_management;
        uint16_t hard_conn_limit_per_ip;
        uint32_t connection_table_size;

        // Layer 2: Request Management (HTTP level)
        bool enable_request_management;
        uint16_t soft_conn_limit_per_ip;
        uint16_t soft_req_limit_per_ip;
        uint32_t retry_after_seconds;

        // Shared state
        httpd_connection_table_t *connection_table;
    } protection_layers;

} httpd_config_t;
```

#### Connection Table (Shared State)

```c
// Platform-agnostic connection tracking
typedef struct httpd_connection_table {
    void *platform_table;           // std::unordered_map, hash table, etc.
    httpd_mutex_t mutex;           // Platform-specific mutex/semaphore
    size_t max_entries;
} httpd_connection_table_t;

// Connection tracking entry
typedef struct httpd_connection_entry {
    struct sockaddr_storage addr;   // Client IP/port
    uint32_t active_connections;    // Current TCP connections
    uint32_t active_requests;       // Current HTTP requests
    uint64_t last_request_time;     // For rate limiting
    uint32_t requests_this_window;  // Sliding window counter
    // ... additional rate limiting data
} httpd_connection_entry_t;
```

### Layer Execution Integration

#### Layer 0: Pre-Accept Session Check

```c
// In httpd_accept_conn() - executed before accept()
esp_err_t httpd_session_management_check(struct httpd_data *hd) {
    if (!httpd_is_sess_available(hd) && hd->config.protection_layers.lru_purge_enable) {
        // Asynchronous LRU cleanup - prevents new connections temporarily
        httpd_sess_close_lru_async(hd);
        return ESP_PRECONN_CAPACITY_FULL;  // Don't call accept()
    }
    return ESP_OK;
}
```

#### Layer 1: Post-Accept Connection Validation

```c
// In httpd_accept_conn() - executed after accept(), before session creation
esp_err_t httpd_connection_management_validate(struct httpd_data *hd,
                                             int new_fd,
                                             struct sockaddr *client_addr) {
    uint32_t active_conns = httpd_conn_table_get_active(hd->connection_table, client_addr);

    if (active_conns >= hd->config.protection_layers.hard_conn_limit_per_ip) {
        // Immediate rejection: TCP RST
        struct linger sl = {.l_onoff = 1, .l_linger = 0};
        setsockopt(new_fd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
        close(new_fd);
        return ESP_CONN_REJECTED;
    }

    // Increment connection counter
    httpd_conn_table_incr_conn(hd->connection_table, client_addr);
    return ESP_OK;
}
```

#### Layer 2: Post-Parse HTTP Request Validation

```c
// In httpd_uri() - executed after HTTP parsing, integrating with existing middleware
esp_err_t httpd_request_management_validate(struct httpd_data *hd,
                                          httpd_req_t *req) {
    struct sockaddr_storage client_addr = get_client_addr_from_req(req);

    // Check connection limits
    uint32_t active_conns = httpd_conn_table_get_active(hd->connection_table, &client_addr);
    uint32_t active_reqs = httpd_conn_table_get_requests(hd->connection_table, &client_addr);

    // Check request limits with sliding window
    bool over_soft_limit = (active_conns >= hd->config.protection_layers.soft_conn_limit_per_ip) ||
                          check_rate_limit_exceeded(hd, &client_addr);

    if (over_soft_limit) {
        // HTTP 429 response
        httpd_resp_set_status(req, HTTPD_429);
        httpd_resp_set_hdr(req, "Connection", "close");
        httpd_resp_set_hdr(req, "Retry-After",
                         httpd_uint32_to_str(hd->config.protection_layers.retry_after_seconds));
        httpd_resp_send(req, "Too Many Requests", HTTPD_RESP_USE_STRLEN);

        // Signal graceful connection closure after response
        httpd_connection_close_after_response(req->handle, httpd_req_to_sockfd(req));
        return ESP_REQ_REJECTED;
    }

    // Increment request counter for this window
    httpd_conn_table_incr_req(hd->connection_table, &client_addr);
    return ESP_OK;
}
```

### Connection Counter Lifecycle Management

#### Increment on Session Creation

```c
// In httpd_sess_new() - after successful session allocation
esp_err_t httpd_sess_new(struct httpd_data *hd, int newfd) {
    // ... existing session allocation ...

    if (hd->config.protection_layers.enable_connection_management) {
        // Get client address (need platform-specific implementation)
        struct sockaddr_storage addr;
        getpeername(newfd, (struct sockaddr*)&addr, &addr_len);

        // Connection counter was already incremented in Layer 1
        // Just attach to session for cleanup tracking
        session->connection_entry_ref = find_connection_entry(&addr);
    }

    return ESP_OK;
}
```

#### Decrement on Session Cleanup

```c
// In httpd_sess_delete() - when connection closes
void httpd_sess_delete(struct httpd_data *hd, struct sock_db *session) {
    // ... existing cleanup ...

    if (hd->config.protection_layers.enable_connection_management && session->connection_entry_ref) {
        httpd_conn_table_decr_conn(hd->connection_table, &session->connection_entry_ref->addr);
    }

    // ... existing socket close ...
}
```

## 3. RFC 9110 Compliance Features

### Connection Flooding Protection (17.6.1)

**✓ Layer 0**: Prevents DoS by limiting total server connections
**✓ Layer 1**: Per-IP hard limits prevent single source exhaustion
**✓ Layer 2**: HTTP 429 responses provide RFC-compliant feedback

### HTTP 429 Too Many Requests Status

**✓ Layer 2 Implementation**:
- `HTTPD_429` status code from http_status_codes.h
- `Connection: close` header for unambiguous termination
- `Retry-After` header with configurable delay
- Graceful connection closure after response transmission

### Connection Persistence Integration

**Connection Context Awareness**:
- WebSocket connections bypass Layer 2 limits (connection_mark_websocket)
- Persistent connections tracked through existing ctx lifecycle
- Request counting integrated with httpd_connection_increment_request_count

## 4. Platform-Specific Implementations

### Connection Table Backend

#### ESP32/FreeRTOS
```c
typedef struct httpd_connection_table {
    StaticSemaphore_t mutex_buffer;
    SemaphoreHandle_t mutex;

    httpd_connection_entry_t *entries;  // Pre-allocated array
    size_t max_entries;
    size_t entry_count;

    // Simple linear search for small N (200 entries max)
} httpd_connection_table_t;

// Mutex operations
#define httpd_mutex_lock(table)    xSemaphoreTake(table->mutex, portMAX_DELAY)
#define httpd_mutex_unlock(table)  xSemaphoreGive(table->mutex)
```

#### Linux/macOS
```c
typedef struct httpd_connection_table {
    pthread_mutex_t mutex;

    // std::unordered_map<sockaddr_storage, httpd_connection_entry_t>
    void *hash_table;  // std::unordered_map via extern "C"

} httpd_connection_table_t;

// Mutex operations
#define httpd_mutex_lock(table)    pthread_mutex_lock(&table->mutex)
#define httpd_mutex_unlock(table)  pthread_mutex_unlock(&table->mutex)
```

#### Windows
```c
typedef struct httpd_connection_table {
    CRITICAL_SECTION mutex;

    // Hash table or map implementation
    void *hash_table;

} httpd_connection_table_t;

// Mutex operations
#define httpd_mutex_lock(table)    EnterCriticalSection(&table->mutex)
#define httpd_mutex_unlock(table)  LeaveCriticalSection(&table->mutex)
```

### Socket Operations

#### RST on Hard Limit (Layer 1)
```c
#include <http_server.h>  // Platform-specific includes handled internally

static void send_tcp_rst(int sockfd) {
    #ifdef _WIN32
        closesocket(sockfd);
    #else
        struct linger sl = {.l_onoff = 1, .l_linger = 0};
        setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));
        close(sockfd);
    #endif
}
```

## 5. Error Handling and Edge Cases

### Shared State Protection
- **Mutex Scope**: Lock entire check→modify sequences
- **Atomic Operations**: Counter increments/decrements
- **Cleanup Guarantees**: Connection tracking cleaned up on all exit paths

### Memory Pressure Handling
- **ESP32 Limits**: 200 max tracked IPs (configurable)
- **LRU Cleanup**: Old entries removed when table full
- **Fallback Behavior**: When table allocation fails, disable protection gracefully

### Timer and Clock Dependencies
- **Platform Timers**: Cross-platform millisecond timestamps for rate limiting
- **Clock Synchronization**: Relative time differences handled gracefully
- **Wraparound Protection**: 64-bit counters prevent integer overflow

### Connection State Consistency
- **Failure Recovery**: Socket close decrements counters even on error paths
- **Resource Leaks**: Proper cleanup ensures no zombie connections in tracking table
- **Concurrent Access**: Thread-safe operations prevent race conditions

## 6. Integration with Existing Systems

### Existing Middleware Compatibility
- **Layer 2** integrates with current `httpd_register_middleware()` system
- **Priority Ordering**: 3-layer protection runs before application middleware
- **Short-circuit Logic**: Failed layer checks prevent expensive downstream processing

### WebSocket Support
- **Layer 2 Bypass**: WebSocket connections use `httpd_connection_mark_websocket()`
- **Connection Context**: WebSocket upgrade tracked in existing connection state
- **Resource Accounting**: WebSocket connections still count toward per-IP limits

### Connection Persistence
- **State Continuity**: Existing `httpd_connection_*` APIs used for persistence tracking
- **Limit Integration**: Request counting coordinated with Layer 2 enforcement
- **Context Sharing**: Protection layers access existing connection contexts

## 7. Performance Characteristics

### Overhead Analysis

**Layer 0**: ~100ns (capacity check)
**Layer 1**: ~5μs (hash table lookup + counter check)
**Layer 2**: ~10μs (request parsing + rate limiting)

**Total Overhead**: <20μs per connection/request on ESP32

### Memory Usage

**Per-IP Tracking**: ~100 bytes (ESP32 efficient)
**Table Overhead**: ~20KB for 200 IPs
**Total Footprint**: <50KB additional RAM

### Scalability Profile

**Embedded (ESP32)**: Excellent - designed for resource constraints
**Desktop (Server)**: Scales to 1000+ IPs with appropriate backends
**Cross-Platform**: Performance portability through abstraction layers

## 8. Testing and Validation Strategy

### Unit Tests
- **Layer Isolation**: Each layer tested independently
- **Mocking**: Platform-specific operations mocked
- **Connection Lifecycle**: Counter increment/decrement validation

### Integration Tests
- **End-to-End Scenarios**: A/B/C test flows from handover document
- **Load Testing**: Connection flood resistance validation
- **Platform Coverage**: Tests run on all target platforms

### Security Validation
- **DoS Resistance**: NIST SP 800-189 rate limiting requirements
- **State Exhaustion**: Memory safety under attack conditions
- **Information Leakage**: No sensitive data in responses

## 9. Implementation Plan

### Phase 1: Core Infrastructure (2 weeks)
- Connection table implementation (platform backends)
- Layer 1 framework (TCP RST functionality)
- Basic API definitions

### Phase 2: Layer Integration (2 weeks)
- Layer 0 (LRU) coordination
- Layer 2 (HTTP 429) implementation
- Configuration structure updates

### Phase 3: Platform Porting (1 week)
- Linux, Windows, ESP32 backend completion
- Performance optimization per platform

### Phase 4: Testing & Validation (2 weeks)
- Comprehensive test suite
- Performance benchmarking
- Security assessment

### Phase 5: Documentation & Examples (1 week)
- API documentation
- Integration examples
- Migration guides

## 10. Success Criteria

**Compliance**: RFC 9110 connection limiting fully implemented
**Performance**: <20μs overhead, <50KB memory usage
**Reliability**: No crashes under DoS attack conditions
**Compatibility**: Fully backward compatible with existing applications
**Maintainability**: Clean abstraction layers for future enhancements

## References

- RFC 9110 Section 17.6.1 - Denial-of-Service Attack Prevention
- RFC 9110 Section 15.5.20 - 429 Too Many Requests
- `docs/connection_limiting_strategies_design.md` - Original analysis
- `docs/middleware_design.md` - Existing middleware framework
- `lib/http-server-middleware/` - Current middleware implementation
- `lib/http-server/include/http_server.h` - Core server APIs
