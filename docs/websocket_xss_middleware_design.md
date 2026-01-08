# WebSocket XSS Protection Middleware Design Document

## Executive Summary

This design document outlines the implementation of Cross-Site Scripting (XSS) protection as middleware for WebSocket endpoints in the ESP HTTP Server. The solution provides configurable server-side validation of WebSocket text message payloads to prevent XSS attacks, following OWASP guidelines and RFC 6455 security considerations.

## Design Requirements

### Functional Requirements

**Security Validation:**
- Detect common XSS attack vectors in WebSocket text frames
- Support multiple blocking/sanitization modes
- UTF-8 aware payload analysis
- Configurable validation rules per endpoint

**Middleware Architecture:**
- Extends existing HTTP middleware pattern to WebSocket
- Per-endpoint configuration via handler context
- Error responses that comply with RFC 6455 Section 7.4.1

**Performance Constraints:**
- Minimal processing overhead on valid messages
- Cross-platform UTF-8 support (MINGW64/Linux/ESP32)
- Memory-efficient pattern matching without regex libraries

## System Architecture

### Component Structure

```
lib/http-server-middleware/
├── include/
│   └── middleware_websocket_xss.h     # Public API
├── src/
│   ├── middleware_websocket_xss.c     # Implementation
│   └── xss_detector.c                 # XSS detection logic
└── test/
    └── test_websocket_xss_integration.c  # Cross-component tests
```

### Integration Points

**WebSocket Handler Flow:**
```
HTTP GET Request (Handshake)
    ↓
WebSocket Upgrade Response
    ↓
WebSocket Message Loop
    ↓
httpd_ws_recv_frame() → XSS Middleware Check → Handler Processing
```

**Middleware Registration:**
```c
// Register XSS middleware for WebSocket endpoint
const httpd_uri_t ws_endpoint = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = &xss_config,  // XSS middleware config
    .is_websocket = true,
    .supported_extensions = "permessage-deflate",
};
```

## Detailed Design

### 1. API Definition

#### Configuration Structure

```c
typedef enum {
    XSS_MODE_BLOCK,          // Reject frame with 1003 (Unsupported Data)
    XSS_MODE_SANITIZE,       // Escape dangerous content
    XSS_MODE_LOG_ONLY,       // Allow but log potential threats
    XSS_MODE_OFF             // Disable validation
} xss_action_mode_t;

typedef struct xss_detection_config {
    xss_action_mode_t action_mode;       // How to handle XSS detection

    // Detection options
    bool check_script_tags;              // <script> tags
    bool check_javascript_urls;          // javascript: URLs
    bool check_event_handlers;           // on* attributes
    bool check_inline_styles;            // style= with url(js:)
    bool check_html_entities;            // HTML entities in attributes

    // Performance limits
    size_t max_payload_scan_size;        // Don't scan beyond this size

    // Callbacks for custom validation
    bool (*custom_xss_checker)(const char *payload, size_t len, void *ctx);
    void *custom_ctx;

    // Logging configuration
    bool enable_logging;
    esp_log_level_t log_level;
} xss_detection_config_t;
```

#### Middleware Function

```c
esp_err_t middleware_websocket_xss(httpd_req_t *req,
                                  const httpd_ws_frame_t *frame,
                                  xss_detection_config_t *config);
```

### 2. XSS Detection Methods

#### Pattern-Based Detection

**Script Tag Injection:**
- Pattern: `<script[^>]*>.*?</script[^>]*>`
- Case-insensitive matching
- Account for HTML comments: `<!--<script>-->`

**JavaScript URL Schemes:**
- Pattern: `(javascript|vbscript|data):`
- Context-aware: check within href=, src=, action= attributes

**Event Handler Injection:**
- Pattern: `\bon\w+\s*=`
- Common targets: onclick, onload, onmouseover

**Style-Based Execution:**
- Pattern: `style\s*=\s*["'][^"']*url\s*\(\s*javascript:`

#### UTF-8 Safety

- All pattern matching must be UTF-8 aware
- No assumptions about character boundaries
- Test with various Unicode injection attempts

### 3. Handler Integration

#### Message Processing Flow

```c
esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        return ESP_OK;  // Handshake
    }

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;

    // Allocate buffer
    uint8_t *payload = malloc(frame.len + 1);
    frame.payload = payload;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        free(payload);
        return ret;
    }

    // ⭐ XSS Middleware Integration Point
    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        xss_detection_config_t *config = (xss_detection_config_t*)req->user_ctx;
        if (config && config->action_mode != XSS_MODE_OFF) {
            ret = middleware_websocket_xss(req, &frame, config);
            if (ret != ESP_OK) {
                free(payload);
                return ret;  // XSS detected and handled
            }
        }
    }

    // Process sanitized payload
    // ...
}
```

#### Error Response Handling

```c
esp_err_t middleware_websocket_xss(httpd_req_t *req,
                                  const httpd_ws_frame_t *frame,
                                  xss_detection_config_t *config) {
    if (httpd_contains_xss_pattern(frame.payload, frame.len, config)) {
        switch (config->action_mode) {
            case XSS_MODE_BLOCK:
                // Close connection per RFC 6455 Section 7.4.1
                httpd_ws_send_close_frame(req, 1003); // Unsupported Data
                return ESP_FAIL;

            case XSS_MODE_SANITIZE:
                // Modify frame.payload in-place with escaped content
                escape_html_entities(frame.payload, frame.len);
                return ESP_OK;

            case XSS_MODE_LOG_ONLY:
                ESP_LOGW(TAG, "XSS pattern detected but allowed");
                return ESP_OK;

            default:
                return ESP_OK;
        }
    }
    return ESP_OK;
}
```

### 4. Cross-Platform Implementation

#### Library Dependencies

- Avoid regex libraries (not available on ESP32 RTOS SDK)
- Use standard C string functions: `strstr()`, `strncmp()`
- UTF-8 aware via portable utilities from `middleware_strings.h`

#### Memory Management

- Zero-copy for performance: in-place sanitization
- Bounded scanning with `max_payload_scan_size`
- No dynamic memory allocation in detection logic

#### Platform-Specific Guards

```c
#ifdef CONFIG_IDF_TARGET_ESP32
    // ESP32-specific: limited stack, prefer heap allocation
#endif

#if defined(_WIN32) && !defined(__MINGW64__)
    // Windows-specific: use winsock2.h for any network operations
#endif
```

### 5. Security Analysis

#### Attack Vector Coverage

**Covered Attacks:**
- ✅ Script tag injection: `<script>alert('xss')</script>`
- ✅ JavaScript URLs: `<a href="javascript:alert(1)">`
- ✅ Event handlers: `<img onload="alert(1)">`
- ✅ Style expressions: `<div style="background:url(javascript:alert(1))">`

**Detection Evasions Addressed:**
- Case variations: `<SCRIPT>`, `<scRipT>`
- HTML comments: `<!--<script-->`
- Entity encoding: `<&#115;cript>` (partial coverage)

#### Performance Impact

**Benchmark Targets:**
- <2% CPU overhead for typical messages (1KB)
- <5ms additional latency for large messages (64KB)
- Memory: O(1) for blocked mode, O(n) for sanitize mode

**Optimization Strategies:**
- Short-circuit detection on first match
- Pattern order by frequency (most common first)
- Pre-compiled pattern structures

### 6. Testing Framework

#### Unit Tests

```c
void test_websocket_xss_blocking(void) {
    xss_detection_config_t config = {
        .action_mode = XSS_MODE_BLOCK,
        .check_script_tags = true
    };

    const char *malicious = "<script>alert('xss')</script>";
    TEST_ASSERT_TRUE(httpd_contains_xss_pattern(malicious, strlen(malicious), &config));
}

void test_websocket_xss_sanitization(void) {
    xss_detection_config_t config = XSS_MODE_SANITIZE;

    char payload[] = "<script src='evil.js'>";
    size_t len = strlen(payload);

    // After sanitization: <script src=&#39;evil.js&#39;>
    httpd_sanitize_xss_payload(payload, &len, &config);
    TEST_ASSERT_FALSE(httpd_contains_xss_pattern(payload, len, &config));
}
```

#### Integration Tests

Two new tests in `test/test_websocket_security.cpp`:

1. `given_websocket_message_with_xss_payload_then_sanitized_or_blocked()`
2. `given_websocket_text_frame_with_malicious_html_then_not_rendered()`

#### Cross-Platform Test Matrix

| Platform | Test Coverage | Notes |
|----------|---------------|-------|
| MINGW64 | Full UTF-8, Windows console logging | FFI to Windows APIs if needed |
| Linux | POSIX compliance, glibc UTF-8 | Standard C library only |
| ESP32 | Basic pattern matching, limited stack | RTOS constraint validation |

### 7. Implementation Timeline

#### Phase 1: Core Implementation (1-2 weeks)
- Core detection functions (`httpd_contains_xss_pattern()`)
- Basic blocking middleware
- Unit test coverage (50+ XSS patterns)

#### Phase 2: Advanced Features (1 week)
- Sanitization mode implementation
- Event handler detection
- Performance optimization

#### Phase 3: Integration & Testing (1 week)
- Handler integration patterns
- Cross-platform testing
- Documentation and examples

#### Phase 4: Security Review (0.5 weeks)
- Penetration testing validation
- False positive analysis
- RFC compliance verification

**Total Effort:** ~3.5 weeks

### 8. Risk Assessment

#### High Risk
- **False Positives:** Legitimate content flagged as malicious (HTML editors, JavaScript code)
- **Performance Regression:** String scanning impacts real-time WebSocket applications
- **UTF-8 Edge Cases:** Incomplete multi-byte sequences or rare Unicode XSS

#### Mitigation Strategies
- **Configurable Detection:** Optional patterns allow customization
- **Mode Selection:** Block vs sanitize vs log-only modes
- **Pattern Tuning:** Field testing to adjust detection sensitivity

#### Low Risk
- **Implementation Bugs:** Standard C string processing, well-tested patterns
- **Integration Issues:** Follows existing middleware patterns
- **Cross-Platform:** Minimal platform differences in string handling

### 9. Success Criteria

#### Functional Success
- ✅ 95%+ XSS attack pattern detection
- ✅ <5 false positives on legitimate content
- ✅ All test platforms pass integration tests
- ✅ Properly handles RFC 6455 error response codes

#### Performance Success
- ✅ <2% CPU overhead for typical workloads
- ✅ No memory leaks in all modes
- ✅ RTOS stack usage within limits

#### Security Success
- ✅ Passes OWASP WebSocket security testing
- ✅ Reduces XSS attack surface for IoT applications
- ✅ Clear audit trail for security events

## References

- [OWASP XSS Prevention Cheat Sheet](https://owasp.org/www-community/xss-filter-evasion-cheat-sheet)
- [RFC 6455: The WebSocket Protocol Section 8](https://datatracker.ietf.org/doc/html/rfc6455#section-8)
- [HTML5 Security Cheat Sheet](https://html5sec.org/)
- Existing middleware patterns in `lib/http-server-middleware/`
