# WebSocket Design and Implementation Guide

This document provides comprehensive design principles and implementation patterns for WebSocket functionality in the ESP HTTP Server, including critical guidance on API usage patterns discovered through extensive debugging.

## Overview

The ESP HTTP Server provides RFC 6455-compliant WebSocket support with additional features for embedded systems. WebSocket connections enable bidirectional communication over HTTP, allowing real-time data exchange between clients and servers.

## WebSocket URIs

RFC 6455 defines two URI schemes for WebSocket connections:
- **`ws://`** - Unencrypted WebSocket connections (default port 80)
- **`wss://`** - TLS-encrypted WebSocket connections (default port 443)

The resource name is extracted from the URI path and query parameters, following the format `/resource/name?query`.

## Protocol Fundamentals

### RFC 6455 WebSocket Protocol

WebSocket operates over HTTP using an upgrade handshake:
- **Client Request**: GET with `Upgrade: websocket` header
- **Server Response**: `101 Switching Protocols` with challenge/response
- **Connection**: Remains bidirectional after upgrade

### Handshake Negotiation (RFC 6455 Section 4)

The WebSocket handshake involves several key headers for protocol negotiation:

- **`Sec-WebSocket-Version`**: Client specifies requested version (must be 13 for RFC 6455)
- **`Sec-WebSocket-Protocol`**: Client lists desired subprotocols (optional)
- **`Sec-WebSocket-Extensions`**: Client requests protocol extensions
- **`Origin`**: Client's origin for same-origin policy enforcement

Server responds with negotiated values or rejects incompatible requests.

### RSV Bits for Extensions (RFC 6455 Section 5.2)

Frame header RSV1, RSV2, RSV3 bits are reserved for extensions:
- **RSV1**: First reserved bit for extension use
- **RSV2**: Second reserved bit for extension use
- **RSV3**: Third reserved bit for extension use

These bits MUST be 0 unless a negotiated extension defines their meaning.

### Frame Format and Length Encoding

WebSocket frames use variable-length encoding for payload size:

- **7-bit length**: 0-125 bytes (stored directly)
- **16-bit length**: 126-65535 bytes (ext16 field)
- **64-bit length**: 65536+ bytes (ext64 field, MSB must be 0)

```
  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-------+-+-------------+-------------------------------+
 |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 | |1|2|3|       |K|             |                               |
 +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 |     Extended payload length continued, if payload len == 127  |
 + - - - - - - - - - - - - - - - +-------------------------------+
 |                               |Masking-key, if MASK set to 1  |
 +-------------------------------+-------------------------------+
 | Masking-key (continued)       |          Payload Data         |
 +-------------------------------- - - - - - - - - - - - - - - - +
 :                     Payload Data continued ...                :
 + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
```

## Handler Programming Patterns

### Critical: Synchronous vs Asynchronous API Usage

**🚨 CRITICAL WARNING: Handler Context Matters**

WebSocket handlers in ESP HTTP Server **execute synchronously** and block the connection thread. API choice is critical:

#### ✅ CORRECT: Synchronous APIs in Handlers
```c
esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // Handshake logic
        return ESP_OK;
    }

    // Message processing
    httpd_ws_frame_t response = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = message,
        .len = message_len
    };

    esp_err_t ret = httpd_ws_send_frame(req, &response);  // ✅ SYNCHRONOUS
    return ret;
}
```

#### Critical: Frame Reception API Pattern

**🚨 CRITICAL WARNING: Frame Reception Contract**

The `httpd_ws_recv_frame()` API uses a two-phase pattern for proper frame handling:

#### ✅ CORRECT: Two-Phase Frame Reception
```c
esp_err_t ws_echo_handler(httpd_req_t *req) {
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    // Phase 1: Get frame metadata (length, type, final flag)
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);  // max_len=0
    if (ret != ESP_OK) {
        return ret;
    }

    // Now ws_pkt.len, ws_pkt.type, ws_pkt.final are populated
    if (ws_pkt.len == 0) {
        // Empty frame, handle accordingly
        return ESP_OK;
    }

    // Phase 2: Allocate buffer and receive payload
    uint8_t *buf = (uint8_t*)malloc(ws_pkt.len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);  // Use actual length
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }

    // Process message in buf, len = ws_pkt.len
    // Echo back the received message
    httpd_ws_frame_t response = {
        .final = true,
        .type = ws_pkt.type,  // Echo same type
        .payload = buf,
        .len = ws_pkt.len
    };

    ret = httpd_ws_send_frame(req, &response);
    free(buf);
    return ret;
}
```

#### ❌ INCORRECT: Single-Phase Frame Reception
```c
esp_err_t broken_ws_handler(httpd_req_t *req) {  // DON'T DO THIS
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = big_buffer;  // Pre-allocated buffer

    // Assumes header will populate ws_pkt fields - WRONG!
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(big_buffer));
    if (ret != ESP_OK) {
        return ret;
    }

    // ws_pkt.len may still be 0 - undefined behavior!
    // This causes empty responses and test failures
    httpd_ws_send_frame(req, &ws_pkt);  // Sends empty frame
}
```

#### Why Two-Phase Pattern Matters
- **Header Phase** (`max_len=0`): Parses frame header and populates metadata
- **Payload Phase**: Allocates exact buffer size and retrieves data
- **Prevents**: Buffer overflows, empty responses, socket state confusion
- **Enables**: Proper memory management and error handling

**🚨 API Contract**: When `max_len=0`, `httpd_ws_recv_frame()` MUST populate `frame->len`, `frame->type`, and `frame->final` before returning `ESP_OK`.

#### ❌ INCORRECT: Asynchronous APIs in Handlers
```c
esp_err_t ws_handler(httpd_req_t *req) {  // DON'T DO THIS
    httpd_ws_frame_t response = {
        .final = true,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = message,
        .len = message_len
    };

    // DEADLOCK: Handler waits for callback that can't execute
    esp_err_t ret = httpd_ws_send_data(req->handle,
                                     httpd_req_to_sockfd(req),
                                     &response);  // ❌ ASYNC IN SYNC CONTEXT
    return ret;
}
```

#### Why This Matters
- **Synchronous Handlers**: Block connection processing - cannot wait for external events
- **Asynchronous APIs**: Require callback execution, creating circular dependencies
- **Result**: Deadlock where handler thread waits for its own completion callbacks

This pattern was discovered during intensive WebSocket fragmentation debugging, where correct fragmentation implementation appeared "broken" due to handler-level API misuse causing deadlocks that masked the functional protocol code.

### Handler Lifecycle

1. **Handshake Phase**: `req->method == HTTP_GET`
   - Receive HTTP request with WebSocket upgrade headers
   - Send `101 Switching Protocols` response
   - Return `ESP_OK` to complete handshake

2. **Message Phase**: `req->method != HTTP_GET`
   - Process WebSocket frames
   - Access frame data via `httpd_ws_recv_frame()`
   - Send responses using appropriate APIs

```c
esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // Handshake - no data processing needed
        return ESP_OK;
    }

    // Handle WebSocket message
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);

    // Process frame data...
    // frame.payload contains message data
    // frame.type indicates message type (TEXT/BINARY/CONTINUATION)

    return ESP_OK;
}
```

## Message Fragmentation

Large WebSocket messages can be split into multiple frames for efficient transmission. Fragmentation is transparent to applications but must be handled correctly in handlers.

### Fragmentation State Machine

```
Start Frame (FIN=0, opcode=TEXT/BINARY)
    ↓
Continuation Frames (FIN=0, opcode=0)
    ↓
Final Frame (FIN=1, opcode=0)
```

### Handler Implementation

```c
esp_err_t ws_fragmentation_handler(httpd_req_t *req) {
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;  // Expected message type

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);

    if (ret == ESP_ERR_HTTPD_WS_PENDING_FRAGMENT) {
        // More fragments coming - do nothing, wait for complete message
        return ESP_OK;
    }

    if (ret == ESP_OK) {
        // Complete message received
        // frame.payload now contains reassembled data
        // frame.len == total reassembled message length

        // Echo back the complete message
        httpd_ws_frame_t response = {
            .final = true,
            .fragmented = false,
            .type = frame.type,
            .payload = frame.payload,
            .len = frame.len
        };

        return httpd_ws_send_frame(req, &response);  // ✅ SYNCHRONOUS
    }

    return ret;
}
```

## Control Frames and Connection Management

RFC 6455 defines control frames for connection management (opcodes 0x8-0xF):

- **Close Frame (0x8)**: Initiates connection closure with optional status code and reason
- **Ping Frame (0x9)**: Heartbeat check, server responds with Pong
- **Pong Frame (0xA)**: Response to Ping, can include payload echo

Control frames can be sent within fragmented messages and MUST be processed immediately. Close frames with status codes 1000-1015 provide specific closure reasons.

## Error Conditions and Status Codes

### UTF-8 Validation (RFC 6455 Section 8.1)
Text frames MUST contain valid UTF-8 sequences. Invalid UTF-8 causes immediate connection closure with status code 1007.

### Protocol Error Status Codes (RFC 6455 Section 7.4.1)
- **1002**: Protocol error (e.g., malformed frame, unmasked client frame)
- **1003**: Unsupported data (received binary data for text-only endpoint)
- **1007**: Invalid frame payload data (UTF-8 validation failure)
- **1008**: Policy violation
- **1009**: Message too big
- **1010**: Missing extension (client expects but server doesn't support)
- **1011**: Internal server error

### Connection Failure Scenarios
- Invalid frame opcodes or RSV bits without negotiated extensions
- Unmasked frames from client to server
- Invalid payload length encoding
- Frame size limits exceeded

## Security Considerations

### Origin-Based Security Model (RFC 6455 Section 10.2)

WebSocket connections are subject to browser same-origin policy:
- Browser clients send `Origin` header identifying script's origin
- Server can reject connections from unauthorized origins
- Non-browser clients may omit or spoof the Origin header

### Client-to-Server Masking (RFC 6455 Section 10.3)

All client-to-server frames MUST be masked to prevent proxy cache poisoning:
- 32-bit XOR masking key applied per frame
- Fresh random key for each frame to ensure unpredictability
- Servers MUST close connections receiving unmasked frames (status 1002)
- Algorithm: `transformed[i] = original[i] XOR mask[i % 4]`

The masking requirement prevents malicious scripts from crafting WebSocket traffic that appears as HTTP requests to intercepting proxies.

### Security Model Clarifications

- **No Built-in Authentication**: WebSocket relies on underlying HTTP authentication
- **TLS Recommended**: Use `wss://` URIs for encrypted transport (prevents MITM attacks)
- **Extension Security**: Extensions must not introduce vulnerabilities
- **Input Validation**: All frame data must be validated before processing

### Input Validation
- Validate frame header fields (opcode, RSV bits, length encoding)
- Check payload length limits and enforce memory bounds
- Ensure proper frame sequencing in fragmented messages

### Memory Safety
- Limit reassembly buffer size to prevent DoS attacks
- Validate UTF-8 sequences in text frames (Section 8.1)
- Implement connection limits and resource quotas

### Authentication and Authorization
- Use pre-handshake callbacks for authentication
- Validate `Origin` header for cross-origin protection
- Leverage HTTP session management for WebSocket authorization

## Security Handler Patterns

### Origin Validation Best Practices

For CSWSH (Cross-Site WebSocket Hijacking) prevention, implement multiple validation layers per RFC 6455 Section 10.2:

#### Handler Isolation Strategy
Create separate handler variants for different security policies rather than modifying existing handlers to maintain test stability:

```c
// Original permissive handler (existing behavior)
esp_err_t ws_security_handler(httpd_req_t *req) {
    // Allows specific origins, rejects mismatches
    // May allow cross-origin if ctx not configured
}

// Strict security variant (enhanced protection)
esp_err_t ws_strict_security_handler(httpd_req_t *req) {
    // Rejects wildcard origins ("*")
    // Enforces Origin header presence
    // Then validates specific origin matching
}
```

#### Defense-in-Depth Validation
Implement layered origin validation:
1. **Reject Wildcard Origins**: Explicitly block `Origin: *` headers
2. **Require Origin Header**: Enforce header presence (browsers always send it)
3. **Validate Specific Origins**: Match against configured allowed origins

#### Vulnerability Relevance Assessment
When selecting security vulnerabilities for websockets:
- **Transport Layer Context**: ws:// (HTTP) vs wss:// (HTTPS) handled by separate `esp_https_server` library
- **Test Stability Priority**: Prefer new handlers/tests over modifying existing working code
- **Security Layering**: Combine multiple validation mechanisms for comprehensive protection

## Testing Patterns

### Fragmentation Testing
- Test 2-frame, 3-frame, and large message scenarios
- Verify frame sequencing and reassembly
- Test boundary conditions

### Error Testing
- Invalid continuation frames
- Buffer overflow attempts
- Protocol violation handling

### Example Test
```c
void test_ws_fragmentation_reassembly(void) {
    // Setup WebSocket connection
    const char *message = "Hello WebSocket World!";
    size_t len = strlen(message);

    // Send fragmented message
    ws_test_client_send_fragmented_message(client, message, len,
                                         WS_TYPE_TEXT, 10);

    // Receive reassembled response
    ws_test_frame_t frame;
    ws_test_client_recv_frame(client, &frame, TEST_TIMEOUT);
    TEST_ASSERT_EQUAL(len, frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(message, frame.payload, len);
}
```

## Extension Negotiation

WebSocket extensions are negotiated during the handshake:
1. Client sends `Sec-WebSocket-Extensions` header
2. Server responds with supported/accepted extensions
3. Connection proceeds with negotiated extensions

### Common Extensions
- **permessage-deflate**: Message-level compression
- **x-custom-ext**: Vendor-specific features

## Implementation Guidelines

### URI Handler Registration
```c
static const httpd_uri_t ws_uri = {
    .uri        = "/websocket",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .user_ctx   = NULL,
    .is_websocket = true,  // Marks as WebSocket endpoint
    .supported_extensions = "permessage-deflate"  // Optional
};
```

### Error Handling
- Return `ESP_OK` for successful frame processing
- Return error codes for protocol violations
- Let server handle connection cleanup

### Resource Management
- Minimize memory allocation in handlers
- Reuse buffers where possible
- Clean up session-specific resources

## References

- [RFC 6455: The WebSocket Protocol](https://datatracker.ietf.org/doc/html/rfc6455)
- [WebSocket Fragmentation Design](./websocket_fragmentation_design.md)
- [WebSocket Extensions Design](./websocket_extensions_design.md)
- [HTTP Server API Documentation](../lib/http-server/docs/http-server.rst)
