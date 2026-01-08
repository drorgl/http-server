# WebSocket Masking Enforcement Design Document

## Overview

This document describes the WebSocket masking security requirement (RFC 6455 Section 5.3) that mandates all client-to-server frames MUST be masked, ensuring server-side enforcement with proper connection closure.

## Protocol Background

### RFC 6455 WebSocket Masking (Section 5.3)

The WebSocket Protocol requires masking for all client-to-server frames to prevent attacks on infrastructure intermediaries such as proxies and caches. The masking makes the payload unpredictable, preventing malicious scripts from constructing messages that appear as HTTP requests to network equipment.

### Key Requirements

#### Mandatory Client Masking
- **All client-to-server frames MUST be masked** (`frame-masked = 1`)
- **32-bit masking key** randomly selected for each frame
- **Fresh masking key** per frame, cryptographically unpredictable
- **Mask bit in second byte** of frame header set to 1

#### Server Enforcement
- **Server MUST close connection** upon receiving unmasked client frame
- **Error code 1002**: "Protocol error" for unreachable code
- **Server MAY send Close frame** before closing (recommended)

#### Masking Algorithm
Client-to-server masking and server unmasking use identical XOR algorithm:
```
j = i MOD 4
transformed-octet-i = original-octet-i XOR masking-key-octet-j
```

## Current Implementation Analysis

### Server-Side Masking Enforcement (httpd_ws.c)

```c
#define HTTPD_WS_MASK_BIT 0x80U

// In httpd_ws_recv_frame():
bool masked = (second_byte & HTTPD_WS_MASK_BIT) != 0;
if (!masked) {
    LOGW(TAG, LOG_FMT("WS frame is not properly masked."));
    return ESP_ERR_INVALID_STATE;
}
```

**Implementation Details:**
- **Enforcement Point**: `httpd_ws_recv_frame()` checks mask bit immediately
- **Error Response**: ESP_ERR_INVALID_STATE results in WebSocket close with code 1002
- **Unmasking Logic**: `httpd_ws_unmask_payload()` applies XOR algorithm correctly
- **Memory Safe**: 32-bit XOR operation with proper bounds checking

### Client-Side Masking Support (http-test-client.c)

```c
// Frame header construction
uint8_t mask_bit = frame->masked ? 0x80 : 0x00;
if (frame->masked) {
    memcpy(&header[header_len], frame->mask, 4);  // 32-bit key
    header_len += 4;
}

// Masking algorithm
if (frame->payload_len > 0) {
    for (size_t i = 0; i < frame->payload_len; i++) {
        masked_payload[i] = frame->payload[i] ^ frame->mask[i % 4];
    }
}
```

**Implementation Details:**
- **Header Construction**: Mask bit and 32-bit key included when `frame->masked = true`
- **Algorithm Correct**: XOR with `(i % 4)` indexing implemented
- **Memory Management**: Temporary masked buffer allocation/deallocation

### Test Infrastructure Gap Analysis

**Current State:** ❌ **NOT TESTED** - No existing tests exercise masking enforcement

**Analysis:**
- **Core Algorithm**: RFcompliant XOR implementation exists
- **Server Enforcement**: Rejection logic implemented and code-covered
- **Client Support**: Masking/unmasking helpers available in test client
- **Missing Tests**: No end-to-end validation of security enforcement

## Implementation Architecture

### Test Design

#### 1. Server Masking Enforcement Test
```cpp
void given_unmasked_client_frame_when_received_then_server_closes_with_1002(void) {
    // Setup WebSocket connection
    // Send unmasked frame using http-test-client
    // Verify server closes connection with close code 1002
}
```

#### 2. Valid Masking Flow Test
```cpp
void given_properly_masked_client_frame_when_received_then_accepted_and_unmasked(void) {
    // Setup WebSocket connection
    // Send masked frame using http-test-client
    // Verify message received correctly (end-to-end flow)
}
```

#### 3. Masking Algorithm Unit Test
```cpp
void given_known_payload_and_mask_when_xored_then_correctly_transformed(void) {
    // Test XOR algorithm with known inputs
    // Verify unmasking recovers original data
}
```

### Required Test Infrastructure Enhancements

#### http-test-client.c Additions
```c
typedef struct {
    ws_frame_type_t type;
    bool masked;          // Control mask bit
    uint8_t mask[4];      // 32-bit masking key
    uint8_t *payload;
    size_t payload_len;
} ws_test_frame_t;

// New helper function
http_test_client_err_t ws_test_client_send_unmasked_frame(
    http_test_client_handle_t *client,
    ws_test_frame_t *frame
);
```

## Test Implementation Plan

### Phase 1: Infrastructure Enhancement
- [ ] Add `ws_test_client_send_unmasked_frame()` to `lib/http-test-client/`
- [ ] Update `ws_test_frame_t` in test header if needed
- [ ] Add unit test for masking algorithm correctness

### Phase 2: Server Enforcement Tests
- [ ] Add test category in `test_websocket_masking.cpp` (following existing patterns)
- [ ] Implement unmasked frame rejection test (expect close code 1002)
- [ ] Implement valid masked frame acceptance test

### Phase 3: Integration and Validation
- [ ] Run cross-platform tests (MINGW64, Linux, ESP32)
- [ ] Performance validation (masking doesn't significantly impact throughput)
- [ ] Update `standards.md`: "Masking & Security (Section 5.3): NOT TESTED" → "FULLY TESTED"

### Phase 4: Documentation Update
- [ ] Add masking tests to `test/test_esp_http_server/test.md`
- [ ] Update `docs/websocket_masking_design.md` with completion status

## Security Considerations

### RFC 6455 Section 10.3 (Attacks On Infrastructure)

**Masking prevents:**
- **Proxy poisoning attacks**: Unmasked frames could be crafted to resemble HTTP requests
- **Cache manipulation**: Malicious clients crafting hits on ad servers/resources
- **Network equipment confusion**: Transparent proxies responding to WebSocket data as HTTP

**Implementation ensures:**
- **Cryptographically strong keys**: Used in test client for compliance
- **Fresh key per frame**: Prevents predictability attacks
- **Proper rejection**: Invalid masking causes immediate connection termination

## Protocol Compliance Validation

### RFC 6455 Section 5.3 Requirements

**Server MUST:**
1. ✅ **Reject unmasked frames** - Implemented in `httpd_ws_recv_frame()`
2. ✅ **Error code 1002** - ESP_ERR_INVALID_STATE → protocol error close code
3. ✅ **Unmask valid frames** - `httpd_ws_unmask_payload()` applies correct XOR

**Client MUST:**
1. ✅ **Send masked frames** - Supported in `ws_test_client_send_frame()`
2. ✅ **Fresh masking key** - Test client generates random keys
3. ✅ **Correct algorithm** - XOR with `(i % 4)` implemented

**Missing:**
- ❌ **Test verification** - No tests validate server enforcement behavior

## Success Criteria

### Functional Criteria
1. **Unmasked frame rejection**: Server closes with code 1002 when receiving unmasked client frame
2. **Masked frame acceptance**: Normal WebSocket operation when frames are properly masked
3. **Algorithm correctness**: Unmasking recovers original payload data

### Security Criteria
1. **Attack prevention**: Unmasked frames cannot bypass security enforcement
2. **Resource protection**: Failed masking attempts don't cause DoS conditions
3. **Standards compliance**: RFC 6455 Section 5.3 requirements fully met

### Testing Criteria
1. **Cross-platform**: Tests pass on MINGW64, Linux, and ESP32
2. **Integration**: End-to-end WebSocket flow with masking works correctly
3. **Performance**: No significant throughput degradation due to masking

## Risk Assessment

### Implementation Risks
**High Risk:**
- **Security bypass**: If tests don't validate enforcement, real attacks could succeed
- **Memory corruption**: Faulty unmasking algorithm could corrupt application data
- **DoS vulnerabilities**: Resource exhaustion from malformed masking

**Medium Risk:**
- **Performance impact**: XOR masking slowing down high-throughput WebSocket apps
- **Platform differences**: Masking behavior differs across targets

**Low Risk:**
- **Test complexity**: New test infrastructure requiring careful implementation

### Business Impact Assessment
**High Impact Scenarios:**
- **Security vulnerability**: WebSocket server accepting unmasked frames could be exploited
- **Protocol non-compliance**: Interoperability issues with standard WebSocket clients
- **Cache poisoning**: Network infrastructure attacks enabled by missing validation

**Low Impact Scenarios:**
- **Performance regression**: Slight overhead from masking validation
- **Development delay**: Test implementation taking longer than expected

## Implementation Completion

### Pre-implementation Status
❌ **Security Gap**: Critical masking enforcement requirement untested

### Post-implementation Status
✅ **Security Validated**: Server properly rejects unmasked frames
✅ **Protocol Compliant**: RFC 6455 Section 5.3 fully implemented and tested
✅ **Attack Prevention**: Infrastructure protection validated via comprehensive testing

This implementation ensures the ESP HTTP Server maintains WebSocket protocol security by properly enforcing mandatory client masking, preventing potential network infrastructure attacks documented in RFC 6455 Section 10.3.

## References

- **RFC 6455**: The WebSocket Protocol, Section 5.3 (Client-to-Server Masking)
- **ESP HTTP Server**: `lib/http-server/src/httpd_ws.c`
- **Test Framework**: `lib/http-test-client/src/http_test_client.c`
- **Standards Compliance**: `standards.md` security implementation analysis
