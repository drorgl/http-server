# WebSocket Extensions Framework Implementation Design Document

## Version 1.0

### Revision History

| Version | Date | Description |
|---------|------|-------------|
| 1.0 | 2025-12-24 | Initial implementation design for RFC 6455 Section 9 compliance |

### Authors
- AI Assistant (based on RFC 6455 Section 9 and ESP HTTP Server analysis)

## Overview

This document describes the design and implementation of WebSocket extensions support for the ESP HTTP Server library, specifically implementing the WebSocket Extensions Framework as defined in RFC 6455 Section 9.

## RFC 6455 Section 9 Compliance Analysis

### Requirements from RFC 6455 Section 9

**Functional Requirements:**
- **Extension Identification**: Extensions use Sec-WebSocket-Extensions header with extension-name and parameters
- **Negotiation Process**: Server lists supported extensions in handshake response; client accepts via subsequent messages
- **Parameter Negotiation**: Extension parameters are negotiated during handshake
- **Multiple Extensions**: Multiple extensions can be negotiated per connection
- **Failure Handling**: Unknown or unsupported extensions are ignored (not cause handshake failure)
- **Security**: Extensions must not introduce security vulnerabilities

**Key Points:**
- Extensions are optional capabilities negotiated during handshake
- Server announces supported extensions in 101 Switching Protocols response
- Client decides whether to proceed after seeing server capabilities
- Extensions apply to WebSocket message payloads (frames)

### Current Implementation State

**Implemented:**
- Basic WebSocket handshake (version 13, key validation)
- Subprotocol negotiation (single subprotocol)
- Frame sending/receiving (text, binary, control frames)
- Security validation (masking enforcement)

**Missing:**
- Sec-WebSocket-Extensions header parsing
- Extension negotiation logic
- Response extension advertisement
- Extension parameter handling

## Architecture Design

### Core Components

```
WebSocket Handshake Flow:
1. Client Request
   ├── Sec-WebSocket-Key: <base64-key>
   ├── Sec-WebSocket-Version: 13
   ├── Sec-WebSocket-Protocol: <optional>
   └── Sec-WebSocket-Extensions: <extension-list>  ← NEW

2. Server Processing
   ├── Validate WebSocket headers
   ├── Process subprotocol negotiation
   ├── Parse extension offers                 ← NEW
   ├── Perform extension negotiation          ← NEW
   └── Build switching protocols response     ← NEW

3. Server Response (101 Switching Protocols)
   ├── Upgrade: websocket
   ├── Connection: Upgrade
   ├── Sec-WebSocket-Accept: <computed-key>
   ├── Sec-WebSocket-Protocol: <negotiated>      (optional)
   └── Sec-WebSocket-Extensions: <negotiated>    ← NEW (optional)
```

### API Extensions

#### 1. httpd_uri_t Structure Extension

**Current Structure** (from esp_http_server.h):
```c
typedef struct httpd_uri {
    // ... existing fields ...
    bool is_websocket;
    bool handle_ws_control_frames;
    const char *supported_subprotocol;
} httpd_uri_t;
```

**Proposed Extension:**
```c
typedef struct httpd_uri {
    // ... existing fields ...
    bool is_websocket;
    bool handle_ws_control_frames;
    const char *supported_subprotocol;
    const char *supported_extensions;        /**< Comma-separated list of supported extensions */
} httpd_uri_t;
```

**Alternative Design - Extension Array:**
```c
typedef struct httpd_uri {
    // ... existing fields ...
    bool is_websocket;
    bool handle_ws_control_frames;
    const char *supported_subprotocol;
    const char **supported_extensions;       /**< NULL-terminated array of extension strings */
    size_t num_supported_extensions;         /**< Number of extensions in array */
} httpd_uri_t;
```

**Decision**: Use comma-separated string for simplicity and consistency with subprotocol.

#### 2. httpd_ws_respond_server_handshake API Change

**Current Signature:**
```c
esp_err_t httpd_ws_respond_server_handshake(httpd_req_t *req, const char *supported_subprotocol);
```

**Proposed Signature:**
```c
esp_err_t httpd_ws_respond_server_handshake(httpd_req_t *req, const char *supported_subprotocol, const char *supported_extensions);
```

**Alternative - Bundle Parameters:**
```c
typedef struct httpd_ws_handshake_params {
    const char *supported_subprotocol;
    const char *supported_extensions;
} httpd_ws_handshake_params_t;

esp_err_t httpd_ws_respond_server_handshake(httpd_req_t *req, const httpd_ws_handshake_params_t *params);
```

**Decision**: Simple parameter addition to maintain backward compatibility with NULL extensions.

### Negotiation Algorithm

#### Extension Parsing Logic

```c
/**
 * Extension format: extension-name; param1=value1; param2=value2
 * Example: "permessage-deflate; client_max_window_bits=15"
 */
typedef struct ws_extension {
    char *name;
    char **parameters;  /* Key-value pairs */
    size_t num_params;
} ws_extension_t;
```

**Parsing Steps:**
1. Split header by comma to get individual extension offers
2. For each extension, split by semicolon to get name and parameters
3. Parse parameters as key=value pairs
4. Validate extension syntax

#### Negotiation Process

```c
ws_extension_t *negotiate_extensions(const char *client_extensions,
                                    const char *server_supported,
                                    size_t *num_negotiated) {
    ws_extension_t *negotiated = NULL;
    *num_negotiated = 0;

    // Parse client extension offers
    ws_extension_t *client_offers = parse_extensions(client_extensions);

    // Parse server supported extensions
    ws_extension_t *server_support = parse_extensions(server_supported);

    // Find intersection of client offers and server support
    for each client_offer in client_offers:
        for each server_ext in server_support:
            if (extension_matches(client_offer, server_ext)):
                // Negotiate parameters if needed
                ws_extension_t negotiated_ext = negotiate_params(client_offer, server_ext);
                add_to_negotiated_list(negotiated, negotiated_ext);
                break;

    return negotiated;
}
```

#### Response Construction

**Sec-WebSocket-Extensions Header Format:**
```
Sec-WebSocket-Extensions: extension1; param1=value1, extension2; param2=value2
```

**Implementation:**
```c
char *build_extension_header(ws_extension_t *extensions, size_t num_extensions) {
    if (num_extensions == 0) return NULL;

    // Calculate buffer size
    size_t buf_size = calculate_extension_header_size(extensions, num_extensions);
    char *header = calloc(1, buf_size);

    // Build header string
    build_extension_string(header, buf_size, extensions, num_extensions);

    return header;
}
```

## Implementation Plan

### Phase 1: Core Infrastructure (Week 1)

**Deliverables:**
- Extend `httpd_uri_t` with `supported_extensions` field
- Update `httpd_ws_respond_server_handshake()` signature
- Add extension parsing utilities to `httpd_ws.c`

**Code Changes:**
1. **esp_http_server.h**: Add supported_extensions field to httpd_uri_t
2. **httpd_ws.c**: Update function signature and stub implementation
3. **httpd_ws_priv.h**: Add internal extension structures

### Phase 2: Extension Negotiation (Week 2)

**Deliverables:**
- Implement extension parsing from Sec-WebSocket-Extensions header
- Implement negotiation algorithm (intersection of client offers and server support)
- Add extension response header construction
- Update handshake response to include negotiated extensions

**Key Functions:**
```c
// New utility functions
static esp_err_t httpd_ws_parse_extensions(const char *header, ws_extension_t **extensions, size_t *num_extensions);
static esp_err_t httpd_ws_negotiate_extensions(const ws_extension_t *client_extensions, size_t client_count,
                                              const ws_extension_t *server_extensions, size_t server_count,
                                              ws_extension_t **negotiated, size_t *negotiated_count);
static char *httpd_ws_build_extension_header(const ws_extension_t *extensions, size_t count);
```

### Phase 3: Error Handling and Security (Week 3)

**Deliverables:**
- Validate extension header syntax
- Handle malformed extension offers gracefully
- Add security checks for extension parameters
- Ensure maximum header size limits
- Add logging for extension negotiation

**Security Considerations:**
- Prevent buffer overflow from malicious extension headers
- Validate extension parameter values (reasonable ranges)
- Log extension negotiation for debugging
- Time complexity bounds on parsing

### Phase 4: Comprehensive Testing (Week 4)

**Unit Tests** (test_websocket_extensions.cpp):
```c
// Basic parsing tests
void test_extension_header_parsing();
void test_complex_extension_parameters();
void test_multiple_extensions_parsing();

// Negotiation tests
void test_extension_negotiation_matching();
void test_extension_negotiation_no_match();
void test_extension_parameter_negotiation();

// Error handling tests
void test_malformed_extension_header();
void test_extension_header_too_long();
void test_unsupported_extension_handling();

// Integration tests
void test_handshake_with_extensions();
void test_handshake_without_extensions();
void test_extension_response_header();
```

**E2E Tests:**
- Client sends extension offers, server responds with negotiated extensions
- Client sends no extensions, server doesn't include extensions header
- Client sends malformed extensions, graceful handling

### Phase 5: Documentation and Examples (Week 5)

**Deliverables:**
- API documentation updates
- Example WebSocket server with extensions
- RFC compliance verification
- Performance measurements

### Supported Extensions Scope

**Initial Implementation** (Framework Only):
- Extension parsing and negotiation framework
- No specific extensions implemented (e.g., no permessage-deflate)
- Framework enables future extension addition

**Future Extensions:**
- permessage-deflate (compression)
- Custom vendor extensions
- Extension-specific frame processing

## Backward Compatibility

### API Changes
- **httpd_uri_t**: New field `supported_extensions` (can be NULL)
- **httpd_ws_respond_server_handshake()**: New optional parameter

### Migration Path
```c
// Before (still works)
esp_err_t httpd_ws_respond_server_handshake(httpd_req_t *req, const char *subprotocol);

// After (new capability)
esp_err_t httpd_ws_respond_server_handshake(httpd_req_t *req, const char *subprotocol, const char *extensions);
```

### ABI Compatibility
- Structure extension adds field at end (ABI compatible)
- Function addition doesn't break existing calls

## Security Considerations

### Input Validation
- Extension name length limits
- Parameter count limits
- Parameter value validation
- Total header size constraints

### Denial of Service Protection
- Parsing time complexity bounds
- Memory allocation limits
- Extension count limits

### Logging and Monitoring
- Debug logging for extension negotiation
- Warning for rejected extensions
- Error logging for parse failures

## Performance Impact

### Memory Overhead
- Extension parsing buffer: ~1KB temporary allocation
- Negotiated extension list: Variable, but typically small

### Processing Overhead
- Additional header parsing: ~100-500μs for typical extension headers
- Negotiation algorithm: O(client_extensions × server_extensions)

### Benchmarks
- Baseline (no extensions): < 50μs handshake processing
- With 3 extensions: < 150μs handshake processing
- Pathological case (many extensions): < 500μs handshake processing

## Testing Strategy

### RFC 6455 Compliance Testing
- Parse all example extension headers from RFC
- Validate negotiation algorithm against RFC examples
- Test edge cases (empty extensions, malformed, etc.)

### Interoperability Testing
- Test with popular WebSocket clients (Chrome, Firefox, etc.)
- Verify extension negotiation with real clients
- Confirm graceful degradation when extensions unsupported

### Unit Test Coverage
- 100% function coverage for extension utilities
- Boundary condition testing
- Error path testing
- Memory leak testing

## Future Extensions

### permessage-deflate Implementation
- Message compression/decompression
- Window size negotiation
- Memory-efficient sliding window

### Extension Registration API
- Dynamic extension registration
- Extension-specific callbacks
- Extension lifecycle management

### Advanced Negotiation
- Parameter range negotiation
- Conditional extension support
- Extension priority ordering

## Post-Implementation Performance Optimizations (Phase 8)

The following tasks outline performance and memory optimizations for the completed WebSocket Extensions Framework implementation. These optimizations ensure the feature meets production performance requirements and maintains robust memory management.

### Performance Optimization Todo List
- [ ] Add performance profiling and unit tests for extension parsing/negotiation - Create benchmark tests in the unittest framework to measure extension parsing, negotiation, and handshake processing times against design targets (<50μs baseline, <150μs with extensions)
- [ ] Optimize memory allocations by reducing strdup calls and using bounded buffers - Minimize `strdup()` calls and allocations by using bounded static buffers where possible, reduce memory fragmentation in parameter parsing
- [ ] Optimize string operations in parsing/negotiation critical path - Reduce string copying and comparisons during parsing/negotiation, streamline header construction logic for better performance
- [ ] Enhance memory management with bounded usage verification and leak prevention tests - Complete bounded memory usage verification, add comprehensive cleanup function tests, and validate behavior under constrained memory conditions

### Goals for Phase 8
These optimizations aim to:
- Maintain handshake processing under 50μs baseline and 150μs with 3 extensions
- Minimize memory allocations in extension parsing pathways
- Eliminate memory leaks in all code paths
- Ensure robust operation under constrained memory environments
- Provide benchmark tests for ongoing performance monitoring

### Implementation Notes
- Follow Unity testing framework patterns for performance benchmarks
- Use bounded static buffers where safe to avoid dynamic allocation
- Add memory tracking in unit tests for leak detection
- Maintain RFC 6455 Section 9 compliance after optimizations

## Conclusion

This implementation provides a solid foundation for WebSocket extensions support while maintaining backward compatibility and focusing on security and performance. The design follows RFC 6455 Section 9 requirements and establishes patterns for future extension implementations.

The framework approach allows the ESP HTTP Server to support advanced WebSocket features like compression while keeping the core implementation clean and maintainable.
