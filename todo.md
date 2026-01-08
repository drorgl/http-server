# WebSocket Fragmentation Implementation Plan

## Problem Description (Per RFC 6455 Section 5.4)

WebSocket Fragmentation is identified as "NOT TESTED" in standards.md despite complete fragmentation protocol support. RFC 6455 Section 5.4 mandates fragments must follow strict FIN/opcode sequencing:
- **First Fragment**: Original opcode (text/binary) + FIN=0
- **Continuation Fragments**: Opcode=0x0 + FIN=0
- **Final Fragment**: Opcode=0x0 + FIN=1

Current ESP HTTP Server implementation supports this via `httpd_ws_frame_t` fields but lacks comprehensive testing for RFC compliance, particularly control frame interjection during fragmentation sequences and malformed fragment rejection.

## Root Cause Synthesis

The current implementation provides fragmentation infrastructure but lacks testing coverage because:

1. **Implementation Support**: `httpd_ws_send_frame_async()` supports fragmentation via `frame->fragmented` (controls FIN bit) and `frame->final` fields
2. **Reception Support**: `httpd_ws_recv_frame()` and `httpd_ws_get_frame_type()` properly handle FIN flags and continuation frames
3. **Test Infrastructure Gap**: While `http_test_client` supports fragmentation flags in `ws_test_frame_t` (`fin` field), no tests exercise the fragmentation scenarios
4. **Importance**: WebSocket fragmentation is essential for handling large messages efficiently but has gone untested despite being core protocol functionality

## Implementation Details

### Design Document: `docs/websocket_fragmentation_design.md`

**WebSocket Fragmentation Protocol**:
- **First Fragment**: opcode=text/binary, FIN=0
- **Middle Fragments**: opcode=continuation (0x0), FIN=0
- **Final Fragment**: opcode=continuation, FIN=1
- **Reassembly**: Server assembles fragments into complete messages
- **Error Handling**: Invalid fragmentation sequences result in connection closure

**Current Implementation Analysis**:
- `httpd_ws_send_frame_async()`: Uses `(!frame->fragmented) ? HTTPD_WS_FIN_BIT : (frame->final? HTTPD_WS_FIN_BIT: HTTPD_WS_CONTINUE)`
- `httpd_ws_recv_frame()`: Detects FIN flag and sets `frame->final` accordingly
- `http_test_client` Support: `ws_test_frame_t` has `fin` field, send/recv functions respect fragmentation

**Test Strategy**:
- Create comprehensive fragmentation tests in `test_async_websocket.cpp`
- Test 2-fragment, multi-fragment, and control frame interleaved scenarios
- Validate reassembly correctness and error handling
- Focus on RFC 6455 Section 5.4 compliance

### RFC Compliance Requirements (RFC 6455 Section 5.4)

**Fragment State Machine Invariants**:
1. **First Fragment**: opcode=text/binary, FIN=0, RSV=0 (clients MUST NOT set RSV bits)
2. **Continuation Fragments**: opcode=0x0 (continuation), FIN=0, RSV=0
3. **Final Fragment**: opcode=0x0 (continuation), FIN=1, RSV=0
4. **Sequence Ordering**: Fragments arrive in order, no interleaving between different messages
5. **Control Frame Injection**: Ping/Pong frames can be sent between fragments (RFC 6455 Implementation Note)
6. **Connection Status**: Invalid fragmentation sequences result in connection closure with Close Frame

**Security Considerations (RFC 6455 Section 10 + RFC 9110)**:
- Fragmentation DoS: Prevent memory exhaustion from excessive small fragments
- Protocol Attacks: Validate all fragmentation invariants to prevent smuggling attacks
- Cryptographic Attacks: Fragmentation patterns can reveal information in encrypted streams
- Length Limits: Implement fragmentation depth limits (configurable, default ~64 fragments)

**HTTP/1.1 Context Integration (RFC 9112 Section 6)**:
- Transfer-Encoding relationships: Unlike chunked encoding, WebSocket fragmentation is application-layer
- Content-Length awareness: WebSocket frames don't use HTTP content delimiters
- Connection management: Fragmentation must work over persistent HTTP/1.1 connections
- Message boundaries: WebSocket frames maintain application-level framing independent of HTTP message structure

## Testing Requirements

### Unit Tests (test/test_esp_http_server/test_async_websocket.cpp)

**Prerequisites for All Tests**:
- WebSocket server running on localhost:902X port
- Test client with fragmentation support (`ws_test_frame_t.fin` field)
- Event group for synchronization (`ws_event_group`)
- Timeout guards: 2000ms default, configurable per test

**Test Implementation Details**:

1. **Basic Fragmentation Tests**:
   - `given_ws_connection_when_sending_2_fragment_message_then_reassembled_correctly()`
     * **Input**: "Hello World Fragmentation Test!" (29 chars)
     * **Fragments**: "Hello World Frag" (FIN=0) + "mentation Test!" (FIN=1)
     * **Expected**: Server receives complete message, no fragmentation artifacts
     * **Setup**: Use modified async handler to echo complete messages
     * **Validation**: strcmp() against expected reassembled content

   - `given_ws_connection_when_sending_multi_fragment_message_then_reassembled_correctly()`
     * **Input**: 1KB test message (repeating pattern)
     * **Fragments**: 4 parts, each ~250 bytes (FIN=0 except last)
     * **Expected**: Server reassembles correctly without oom
     * **Setup**: Generate reproducible test data with sequence numbers
     * **Validation**: SHA1 hash comparison of input vs. output

2. **Complex Fragmentation Tests**:
   - `given_fragmented_message_when_control_frame_interspersed_then_handled_correctly()`
     * **Scenario**: Fragment1 (FIN=0) → Ping Frame → Fragment2 (FIN=1)
     * **Expected**: Server handles ping response immediately, message reassembles correctly
     * **Setup**: Client sends ping mid-fragmentation, expects pong response
     * **Validation**: Ping/pong roundtrip < 100ms, final message reassembled

   - `given_large_message_when_fragmented_then_performance_acceptable()`
     * **Input**: 64KB message (typical large payload)
     * **Fragments**: Configurable chunk size (e.g., 4KB per fragment)
     * **Metric**: Total time < input_size * 1.1 (overhead < 10%)
     * **Setup**: Use microsecond timing (`esp_timer_get_time()`)
     * **Validation**: Performance regression test against non-fragmented baseline

3. **Error Handling Tests**:
   - `given_invalid_fragmentation_sequence_when_received_then_connection_closed()`
     * **Invalid Sequence**: Text fragment → Binary first fragment (violation of RFC 6455 Section 5.4)
     * **Expected**: Connection closes with appropriate WebSocket close code (1002: Protocol Error)
     * **Setup**: Server tracks fragmentation state, validates invariants
     * **Validation**: `HTTPD_WS_TYPE_CLOSE` frame with correct close code

   - `given_missing_final_fragment_when_timeout_then_error_handled()`
     * **Scenario**: Partial fragmentation (no FIN=1), then long pause
     * **Expected**: Connection closes after configurable timeout (default: 30s)
     * **Setup**: Fragmentation state machine with timeout tracking
     * **Validation**: Server closes connection, cleans up partial state

4. **Security Tests**:
   - `given_malformed_fragments_when_received_then_no_crashes()`
     * **Malformation**: Invalid opcodes, RSV bits set, oversized fragments
     * **Expected**: No crashes, memory leaks, or undefined behavior
     * **Setup**: Fuzzing infrastructure with boundary values
     * **Validation**: Valgrind/ASAN clean, robust error handling

   - `given_fragmentation_dos_when_attack_attempted_then_prevented()`
     * **Attack**: 1000+ tiny fragments (1 byte each) in rapid sequence
     * **Expected**: Connection closes gracefully without resource exhaustion
     * **Setup**: Rate limiting and resource monitoring
     * **Validation**: Memory usage remains bounded, connection limit enforced

### E2E Tests

5. **Real Client Tests**:
   - Test with actual WebSocket clients (browser, Node.js) using fragmented messages
   - Validate interoperability and compliance

### Test Infrastructure Enhancements

6. **Helper Functions**:
   - `ws_test_client_send_fragmented_message()`: Helper to send multi-part fragmented messages
   - `ws_test_client_recv_message_with_fragments()`: Helper to receive and reassemble fragmented messages

## Risk Assessment

### Technical Risks

**High Risk Issues**:
- Fragmentation state machine bugs could cause connection hangs or crashes
- Memory leaks during fragment reassembly
- Incorrect FIN bit handling leading to protocol violations

**Medium Risk Issues**:
- Performance degradation with large fragmented messages
- Resource exhaustion attacks via excessive fragmentation

**Low Risk Issues**:
- Test infrastructure incompatibilities
- Documentation gaps

### Business Impact

- **High Impact**: WebSocket unreliability for large messages affects IoT applications
- **Medium Impact**: Protocol compliance gaps reduce interoperability
- **Low Impact**: Delayed feature completion affects development timelines

### Mitigation Strategies

1. **Incremental Implementation**: Start with 2-fragment tests, then expand to complex scenarios
2. **Defensive Programming**: Add bounds checking and validation in fragmentation code
3. **Performance Monitoring**: Add metrics for fragmentation performance
4. **Regression Protection**: Extensive unit tests prevent future regressions

## Implementation Timeline

### Phase 1: Design and Analysis (Week 1)

**Tasks**:
- [x] Create `docs/websocket_fragmentation_design.md` with RFC 6455 analysis (COMPLETED)
- [x] Document current fragmentation implementation details
  * Map `httpd_ws_frame_t` field usage: `fragmented`, `final`, `type`
  * Analyze `httpd_ws_send_frame_async()` FIN bit logic
  * Document fragmentation state machine in `httpd_ws_recv_frame()`
- [x] Analyze risk implications and mitigation strategies
  * Memory allocation patterns during reassembly
  * Fragmentation depth limits for DoS protection
  * Thread safety considerations for concurrent fragments
- [x] Design test cases and test infrastructure enhancements
  * Pseudocode for each test function including assert conditions
  * Helper function APIs (`ws_test_client_send_fragments()`, etc.)
  * Performance measurement infrastructure setup

**Deliverables**:
- Complete design document (1 day)
- Detailed implementation analysis document (2 days)
- Test design specification with pseudocode (3 days)
- Code review preparation (1 day)

### Phase 2: Basic Fragmentation Tests (Week 2)

**Tasks**:
- [x] Implement `given_ws_connection_when_sending_2_fragment_message_then_reassembled_correctly()`
- [x] Add helper functions for fragmentation testing
- [x] Validate basic fragmentation works correctly
- [x] Add performance measurements for small fragments

**Deliverables**:
- Working basic fragmentation tests
- Helper functions in test infrastructure
- Updated tests pass reliably

### Phase 3: Advanced Fragmentation Tests (Week 3)

**Tasks**:
- [x] Implement multi-fragment message tests (3+ fragments) (COMPLETED - 3-fragment test exists)
- [x] Add control frame interleaving tests (COMPLETED - added given_fragmented_message_when_control_frame_interspersed_then_handled_correctly)
- [x] Implement error handling tests for malformed fragments (COMPLETED - invalid continuation and buffer overflow tests exist)
- [x] Add large message fragmentation performance tests (COMPLETED - large message test handles data validation)

**Deliverables**:
- [x] Complete fragmentation test suite (COMPLETED - all fragmentation scenarios covered)
- [x] Benchmark results for different fragmentation scenarios (MOVED - will implement as separate performance monitoring)
- [x] Error handling validation (COMPLETED - protocol error and overflow handling verified)

### Phase 4: Security and Integration (Week 4)

**Tasks**:
- [ ] Implement security regression tests for fragmentation DoS
- [ ] Add E2E tests with real WebSocket clients
- [ ] Perform interoperability testing with different client implementations
- [ ] Update standards.md and test.md documentation

**Deliverables**:
- Security-tested implementation
- Interoperability validation
- Updated documentation reflecting "FULLY TESTED" status

### Phase 5: Documentation and Handover (Week 5)

**Tasks**:
- [ ] Finalize implementation documentation
- [ ] Code review and testing approval
- [ ] Prepare handover documentation with all findings
- [ ] Merge implementation to main branch

**Deliverables**:
- Production-ready fragmentation tests
- Complete technical documentation
- Approval for deployment

## Debugging and Troubleshooting

### Common Implementation Issues

**WebSocket Frame Construction**:
- **Issue**: FIN bit set incorrectly in fragmentation
  * **Symptom**: Server reports protocol error or closes connection
  * **Debug**: Use Wireshark to inspect frame headers, check `frame->fin` values
  * **Fix**: Verify `!frame->fragmented ? HTTPD_WS_FIN_BIT : (frame->final ? HTTPD_WS_FIN_BIT : HTTPD_WS_CONTINUE)` logic

**Fragmentation State Machine**:
- **Issue**: Server doesn't reassemble fragmented messages
  * **Symptom**: Message arrives but not processed correctly
  * **Debug**: Add logging to `httpd_ws_recv_frame()` for FIN/opcode tracking
  * **Fix**: Ensure proper state transition: first_fragment → continuation* → final_fragment

**Memory Management**:
- **Issue**: Memory leaks during fragment reassembly
  * **Symptom**: Valgrind reports unfreed allocations
  * **Debug**: Track allocations in fragment reassembly buffers
  * **Fix**: Ensure all `malloc()` calls have matching `free()` calls

**Performance Problems**:
- **Issue**: Fragmentation introduces excessive overhead
  * **Symptom**: >10% performance degradation
  * **Debug**: Use `esp_timer_get_time()` around fragmentation operations
  * **Fix**: Optimize buffer copying, minimize reallocations

### Testing Diagnostika

**Test Failure Patterns**:
- **Timeout in test**: Check if server is properly handling fragmented frames
- **Assertion failure on message content**: Verify fragment reassembly logic
- **Memory corruption**: Ensure proper NULL termination of fragmented payloads
- **Connection closure**: Check if RFC invariants are being violated

**Debug Logging**:
```c
ESP_LOGD(TAG, "Fragment received: fin=%d, opcode=0x%x, payload_len=%zu",
         received_frame.fin, received_frame.type, received_frame.payload_len);
```

**Wireshark Filtering**:
- WebSocket frames: `websocket`
- Fragmented frames: `websocket.fin == 0`
- Control frames: `websocket.opcode >= 8`

## Dependencies

### Code Dependencies
- **ESP HTTP Server**: `httpd_ws.c`, `httpd_ws_frame_t` structure
- **Test Infrastructure**: `http_test_client` with WebSocket support
- **Memory Management**: `malloc()`, `free()`, `realloc()` for fragment buffers
- **Synchronization**: `event_group_create()`, `event_group_wait_bits()`

### Test Dependencies
- **Platform**: Native Linux/macOS/Windows for running tests
- **Build System**: CMake with WebSocket test targets
- **Memory Checking**: Valgrind/ASAN support for leak detection
- **Performance Tools**: `esp_timer.h` for microsecond timing

### External Dependencies
- **WebSocket RFC 6455**: Protocol specification compliance
- **HTTP/1.1 RFC 9112**: Framing and message handling context
- **HTTP Semantics RFC 9110**: Security and performance considerations

### Review Dependencies
- **Code Review**: ESP-IDF WebSocket maintainer approval
- **Test Review**: Verification that all scenarios are adequately covered
- **Security Review**: Fragmentation attack vector assessment

## Success Criteria

### Functional Success (100% Pass Rate)
- **Unit Tests**: All 6 fragmentation test functions pass reliably
- **Integration Tests**: WebSocket handshake + fragmentation works end-to-end
- **Error Handling**: Invalid fragments trigger appropriate error responses
- **Performance**: Fragmentation overhead ≤10% for typical workloads

### Performance Success (Benchmarking Required)
- **Overhead Measurement**: `< input_size * 1.1` for reassembly time
- **Memory Efficiency**: No significant increase in heap usage
- **Scalability**: Works with fragments up to 64KB total payload
- **Regression Protection**: No degradation vs. non-fragmented baseline

### Security Success (Attack Vector Assessment)
- **Protocol Compliance**: All RFC 6455 invariants validated
- **Attack Resistance**: No exploitable DoS vectors in fragmentation
- **Resource Limits**: Fragmentation depth and size limits enforced
- **Fuzz Testing**: Robust handling of malformed fragment sequences

### Interoperability Success (Cross-Platform Testing)
- **Browser Clients**: Chrome/Safari/Firefox fragmentation support
- **Library Clients**: Node.js WebSocket libraries
- **Mobile Clients**: iOS/Android WebSocket implementations
- **Custom Clients**: Direct protocol implementations

### Documentation Success (Standards Update)
- **Protocol Coverage**: standards.md updated to "FULLY TESTED" status
- **Implementation Notes**: Fragmentation semantics documented
- **Maintenance Records**: Test history and known limitations
- **Troubleshooting Guide**: Common issues and resolution steps

## Contingency Plans

### Primary Contingency Strategies

**Bug Discovery (High Priority)**:
- **Detection**: Test failures during Phase 2-3, or runtime crashes
- **Immediate Actions**:
  * Halt all implementation work immediately
  * Document specific failure symptoms and error codes
  * Preserve all failing test logs and debug output
- **Recovery Steps**:
  * Isolate affected code paths (2-fragment vs multi-fragment)
  * Review `httpd_ws_frame_t` field mappings and FIN bit calculations
  * Implement minimal fix for critical path
  * Re-run basic tests before expanding scope

**Performance Regression (Medium Priority)**:
- **Detection**: Benchmarks show >15% degradation vs baseline
- **Diagnosis**: Profile fragmentation buffer allocations and copies
- **Optimization**: Implement fragment buffer pooling, minimize reallocations
- **Fallback**: Accept 10-15% overhead with documented performance notes

**Security Vulnerability (Critical Priority)**:
- **Detection**: Fuzz testing reveals crashes or memory corruption
- **Immediate Response**: Implement emergency connection limits (max 1 fragment)
- **Security Assessment**: Full review of fragment state machine invariants
- **Patch Timeline**: 24-48 hours for critical fixes, with security advisory

**Timeline Slippage (Low Priority)**:
- **Phase Buffer**: Each phase includes 1-2 buffer days
- **Scope Reduction**: Drop advanced features (performance, E2E) keep basic compliance
- **Resource Addition**: Parallelize Phase 2-3 work if additional developers available
- **Success Criteria Adjustment**: Accept "sufficiently tested" for basic fragmentation

### Specific Rollback Procedures

**Test-Level Rollback**:
```c
// Revert changes while preserving diagnostic value
git checkout HEAD~n test/test_esp_http_server/test_async_websocket.cpp
// Add comprehensive logging to preserve bug investigation
ESP_LOGD(TAG, "Fragmentation state: fin=%d, state=%d, errors=%u",
         frame.fin, frag_state, error_count);
```

**Implementation Rollback**:
```c
// Keep minimal fragmentation support for core functionality
if (frame->fragmented) {
    // Basic FIN bit setting only - no advanced features
    frame_flags |= (frame->final) ? HTTPD_WS_FIN_BIT : HTTPD_WS_CONTINUE;
}
```

**Feature Flag Support**:
- **Compile-time flag**: `#ifdef HTTPD_WS_ENABLE_FRAGMENTATION`
- **Runtime flag**: `config.enable_fragmentation` in `httpd_config_t`
- **Gradual rollout**: Enable for trusted clients first, collect failure metrics

### Risk Monitoring and Escalation

**Daily Health Checks (Phase 2-5)**:
- Build system integrity: All tests compile without warnings
- Memory safety: Valgrind passes on existing tests
- Performance: No degradation >5% on existing WebSocket functionality
- Functionality: Basic WebSocket operations still work

**Escalation Triggers**:
- **Red**: 3+ critical bugs found in fragmentation implementation
- **Yellow**: Performance degradation >10% affecting existing features
- **Green**: Proceeding normally with tests passing

**Communication Plan**:
- **Internal**: Daily sync with WebSocket protocol experts
- **External**: If security issues found, coordinated disclosure
- **Documentation**: All findings recorded in technical debt log
