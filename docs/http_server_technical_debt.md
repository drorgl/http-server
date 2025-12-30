# HTTP Server Technical Debt

This document outlines known technical debt items that require future attention for improved reliability and maintainability.

## Option 2: Control Socket Port Isolation (HIGH PRIORITY)

### Problem Description
Multiple HTTP server instances sharing the same UDP control port (32768) causes message delivery conflicts and server instability. This affects connection management tests and can cause deadlock in multi-server environments.

### Root Cause
- `ESP_HTTPD_DEF_CTRL_PORT` (32768) is shared across all server instances
- SO_REUSEADDR allows multiple socket binds to same address/port pair
- Control messages (shutdown, work queue) may deliver to wrong server instance
- Results in select() errors, thread deadlocks, and test failures

### Impact
- **Connection Management Tests:** 4 tests fail due to server thread becoming unresponsive
- **Reliability:** Unpredictable behavior when multiple servers run concurrently
- **Debugging:** Difficult to isolate issues in production multi-server deployments

### Evidence
```
test_connection_management.cpp failures:
- given_default_server_when_http11_request_without_connection_header_persists
- given_default_server_when_connection_close_header_forces_closure
- given_default_server_when_forced_close_api_closes_connection
- given_server_config_with_connection_settings_respects_config

Log evidence:
D [httpd_server] control socket fd=844 not ready for reading
E [httpd_server] error in select (0)
W [httpd_process_ctrl_msg] error in recv (0)
```

### Solution Options

#### A. Dynamic Port Allocation
**Description:** Auto-assign unique control port per server instance
**Implementation:**
- Add port search algorithm in `httpd_server_init()`
- Attempt sequential ports starting from `ESP_HTTPD_DEF_CTRL_PORT + 1`
- Store assigned port in `httpd_config_t.ctrl_port`
- Ensure sender/receiver use matching ports

**Complexity:** Medium
**Risk:** Moderate (port allocation race conditions possible)
**Timeline:** 2-3 weeks
**Testing:** Multi-server concurrent test suite required

#### B. Per-Process Port Offset
**Description:** Use process ID to offset base port
**Implementation:**
- Base port = ESP_HTTPD_DEF_CTRL_PORT + (getpid() % 1000)
- Reduces but doesn't eliminate conflicts
- Configurable offset range

**Complexity:** Low
**Risk:** Low
**Timeline:** 1 week
**Testing:** Multi-process test scenarios

#### C. Configuration-Based Port Assignment
**Description:** Require explicit ctrl_port in httpd_config_t
**Implementation:**
- Remove `ESP_HTTPD_DEF_CTRL_PORT` default
- Force developer to specify unique ports
- Documentation update for usage

**Complexity:** Low
**Risk:** Low (backward compatibility breakage)
**Timeline:** 1 week
**Testing:** Configuration validation tests

## Select() Invalid FD Race Condition (HIGH PRIORITY)

### Problem Description
Intermittent select() errors caused by invalid file descriptors in fd_set, leading to race conditions between server thread and test execution. Test failures occur when sessions are prematurely deleted during fd validation.

### Root Cause
- Test cases create session with hardcoded invalid fd (e.g., 42) without creating actual socket
- select() fails with EBADF when fd_set contains invalid fds
- httpd_sess_delete_invalid() removes invalid sessions, deleting test contexts
- Race condition between server thread and synchronous test calls

### Impact
- **Test Reliability:** Connection persistence tests fail intermittently
- **Debugging:** errno=0 logged (overwritten), masking actual EBADF cause
- **Production:** Potential for valid connections to be incorrectly deleted if fd corrupted

### Evidence
```
E (3) httpd: lib\http-server\src\httpd_main.c:356 [httpd_server] error in select (0)
W (6) httpd_sess: lib\http-server\src\httpd_sess.c:155 [enum_function] Closing invalid socket 42
test\test_esp_http_server\test_connection_persistence.cpp:197:test_connection_should_persist:FAIL: Expected TRUE Was FALSE
```

### Solution Options

#### A. Guard Rails Prevention
**Description:** Add fd validation in set_descriptors and improved error handling
**Implementation:**
- Check fd_is_valid before FD_SET in httpd_sess_set_descriptors
- Only call delete_invalid on EBADF errno in select error handler
- Add logging for fd validation and invalid session detection

**Complexity:** Low
**Risk:** Low
**Timeline:** 1 week
**Testing:** Run tests 100+ times to verify no intermittent failures

#### B. Test Infrastructure Improvement
**Description:** Fix test to use valid socket descriptors
**Implementation:**
- Modify test helpers to create real dummy sockets
- Properly manage socket lifecycle (create, close)
- Remove hardcoded fd assumptions

**Complexity:** Medium
**Risk:** Moderate (test changes may affect other functionality)
**Timeline:** 2 weeks
**Testing:** All test suites pass consistently

#### C. Server Architecture Hardening
**Description:** Make server thread synchronous for testing
**Implementation:**
- Add test mode flag to disable background thread
- Direct server loop calls in test context
- Eliminate race conditions entirely

**Complexity:** High
**Risk:** High (major test framework changes)
**Timeline:** 3-4 weeks
**Testing:** Complete test rewrite required

## Option 3: Control Socket Architecture Refactor (MEDIUM PRIORITY)

### Problem Description
UDP-based inter-component communication introduces unnecessary complexity and unreliability. The control socket mechanism for shutdown and work queue signaling is over-engineered for embedded systems.

### Root Cause
- UDP sockets add cross-platform complexity (bind, SO_REUSEADDR, port conflicts)
- Asynchronous message delivery introduces race conditions
- Network stack state affects internal server operation
- No benefit over in-process signaling for single-threaded server

### Impact
- **Maintenance:** ~200 LOC dedicated to UDP control socket management
- **Reliability:** Network errors affect internal server operations
- **Testing:** Difficult to test edge cases without UDP stack simulation
- **Performance:** Small overhead in every server loop iteration

### Evidence
See Option 2 evidence above, plus code complexity in:
- `util/ctrl_sock.c` - 178 lines of UDP socket management
- `httpd_main.c:httpd_process_ctrl_msg()` - UDP message processing
- Cross-platform socket compatibility matrix

### Solution Options

#### A. Thread-Safe Event Groups Refactor
**Description:** Replace UDP control with generic_event_groups signaling
**Implementation:**
- Use existing `generic_event_groups` library for thread-safe signaling
- Event bits for: SHUTDOWN_SIGNAL, WORK_AVAILABLE
- Remove all UDP socket code (ctrl_sock.h/c)
- Simplify httpd_server() select loop

**Complexity:** High
**Risk:** High (core architectural change)
**Timeline:** 4-6 weeks
**Testing:** Full test suite + concurrency stress tests

#### B. Condition Variable + Mutex
**Description:** Standard threading primitives for signaling
**Implementation:**
- Add condition variable in `struct httpd_data`
- Work queue protected by mutex
- Server thread waits on cv for shutdown/work events
- Remove select() from server loop

**Complexity:** High
**Risk:** High (major threading model change)
**Timeline:** 4-6 weeks
**Testing:** Requires thread-safety analysis tools

#### C. Work Queue Only Optimization
**Description:** Keep UDP for shutdown, remove work queue dependency
**Implementation:**
- Direct `httpd_queue_work()` calls (remove async queuing)
- Keep UDP for clean shutdown signaling only
- Simplify work handling logic

**Complexity:** Medium
**Risk:** Moderate (API contract changes)
**Timeline:** 3-4 weeks
**Testing:** Work queue dependent tests (async operations)

## Recommendations

### Immediate Priority (Critical Path)
1. Implement **Option 2A** - Dynamic Port Allocation
2. Add configuration validation for ctrl_port uniqueness
3. Update documentation with multi-server usage guidelines

### Future Architecture
1. Plan migration path to **Option 3A** - Event Groups refactor
2. Deprecate UDP control socket in favor of in-process signaling
3. Simplify cross-platform socket compatibility requirements

## Status
- **HTTP/1.1 Connection Persistence Implementation:** RESOLVED - Core server implementation verified and documented; erroneous middleware-based E2E tests removed; test organization violations corrected per quality gates.
- **Session Context Memory Issue:** RESOLVED - Fixed req->sess_ctx preservation across requests, preventing nullification during memset operations.
- **Connection Closure Regressions:** RESOLVED - Reverted premature session closure after response; now only closes on explicit client request or async completion to prevent WebSocket/WebSocket async test failures.
- **Control Socket Timing Issue:** RESOLVED - Reverted select timeout to 100ms from 200ms, optimizing for test performance and reducing "control socket fd not ready" errors.
- **Buffer Garbage Issue in Tests:** RESOLVED - Added memset initialization in test handlers to prevent atoi parsing of uninitialized memory.
- **Select() Invalid FD Race Condition:** RESOLVED - Implemented fd validation guard rails in httpd_sess_set_descriptors() and improved select error handling. All connection persistence tests now pass consistently.
- **Control Socket Port Conflicts:** OPEN - Technical Debt
- **Control Socket Architecture:** OPEN - Technical Debt

## Migration Strategy
1. **Phase 1:** Implement port isolation to fix test reliability
2. **Phase 2:** Architecture refactor for maintainability
3. **Phase 3:** Remove deprecated UDP control code

## Risks & Considerations
- Backward compatibility for existing applications
- Cross-platform socket behavior differences
- Performance impact during transition
- Test coverage for concurrent server scenarios
