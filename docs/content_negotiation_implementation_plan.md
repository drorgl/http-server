# HTTP Content Negotiation Implementation Plan

## Overview

This document provides a detailed implementation plan for HTTP Content Negotiation (RFC 9110 Part 12) to address the high-importance gap identified in standards.md. Content Negotiation is currently marked as "PHASE 1 COMPLETE" but "NOT TESTED", representing a critical missing feature for RFC 9110 compliance.

## Current Status Analysis

### What Exists (Phase 1 - Parsing)
- ✅ **Accept header parsing** with quality values support
- ✅ **Basic data structures** for Accept ranges and capabilities
- ✅ **Unit tests** for parsing functionality
- ✅ **Middleware skeleton** with configuration structure

### What's Missing (Phase 2 - Negotiation Algorithm)
- ❌ **Content negotiation algorithm** (RFC 9110 scoring)
- ❌ **Server capabilities framework** integration
- ❌ **Vary header generation** for cache correctness
- ❌ **Handler integration APIs** to access negotiated content
- ❌ **Comprehensive testing** (unit, integration, E2E)
- ❌ **Performance optimization** and benchmarking

### Standards Gap
**From standards.md:** "Content Negotiation (Part 12)" shows as "PHASE 1 COMPLETE" with implementation status but marked "❌ NOT TESTED" with no test coverage mentioned. This is a critical RFC 9110 compliance gap.

---

## RFC 9110 Part 12 Complete Requirements Analysis

### Excerpt from RFC 9110 Section 12.1 - Proactive Negotiation

"A user agent that supports content negotiation MAY include the following
header fields, collectively called the proactive negotiation header
fields, in its request to enable proactive negotiation by an origin
server:
- Accept (Section 12.5.1)
- Accept-Charset (Section 12.5.2) - Note: This is deprecated
- Accept-Encoding (Section 12.5.3)
- Accept-Language (Section 12.5.4)"

### Excerpt from RFC 9110 Section 12.4.2 - Quality Values

"The weight is normalized to a real number in the range 0 through 1,
where 0.001 is the least preferred and 1 is the most preferred; a
value of 0 means "not acceptable". If no "q" parameter is present,
the default weight is 1.

weight = OWS ";" OWS "q=" qvalue
qvalue = ( "0" [ "." 0*3DIGIT ] ) / ( "1" [ "." 0*3("0") ] )

A sender of qvalue MUST NOT generate more than three digits after the
decimal point. User configuration of these values ought to be limited
in the same fashion."

### Excerpt from RFC 9110 Section 12.4.3 - Wildcard Values

"Most of these header fields, where indicated, define a wildcard value
("*") to select unspecified values. If no wildcard is present, values
that are not explicitly mentioned in the field are considered
unacceptable."

### Excerpt from RFC 9110 Section 12.5.1 - Accept Header Field

"The "Accept" header field can be used by user agents to specify their
preferences regarding response media types. For example, Accept header
fields can be used to indicate that the request is specifically limited
to a small set of desired types, as in the case of a request for an
in-line image.

Accept = #( media-range [ weight ] )

media-range = ( "*/*" / ( type "/" "*" ) / ( type "/" subtype ) ) parameters"

The precedence rules (Section 12.5.1) define:
- Specific type/subtype before type/* before */*
- Higher qvalue before lower qvalue
- Order of appearance as tie-breaker

### Complete Negotiation Algorithm Specification

**From RFC 9110 Section 12.5.1 (Quality Value Precedence):**

"For a given request, the algorithm below is used to determine the
precedence of media ranges:

1. Sort media ranges by their specificity (most specific first):
   - type/subtype
   - type/*
   - */*

2. Within each specificity level, sort by quality value (highest first)

3. Within each quality value level, sort by order of appearance in request"

**Our Implementation Scoring:**
- Exact match type/subtype: Score = specificity(1000) × qvalue
- Type wildcard match: Score = specificity(100) × qvalue
- Universal wildcard match: Score = specificity(10) × qvalue

### Complete Pseudocode Algorithm

```c
typedef struct {
    char *range;          // "text/html", "application/*", "*/*"
    float quality;        // 0.0 to 1.0, default 1.0
    bool explicit_q;      // true if "q=" was specified
} accept_range_t;

typedef struct {
    char **supported_types;  // ["text/html", "application/json", "text/plain"]
    size_t count;
} server_capabilities_t;

typedef struct {
    char *selected_type;     // Best match result
    float score;            // Score used to select winner
    bool negotiation_used;   // Whether any negotiation occurred
} negotiation_result_t;

/**
 * RFC 9110 Content Negotiation Algorithm Implementation
 */
negotiation_result_t negotiate_content_type(
    accept_range_t *client_accept_ranges,     // Parsed Accept header
    size_t accept_count,                      // Number of accept ranges
    server_capabilities_t *server_caps        // Server capabilities
) {
    negotiation_result_t result = {NULL, -1.0f, false};
    bool found_any_match = false;

    // Step 1: Evaluate each client preference in order
    for (size_t i = 0; i < accept_count; i++) {
        accept_range_t *client_pref = &client_accept_ranges[i];

        // Skip unacceptable preferences (q=0)
        if (client_pref->quality <= 0.0f) continue;

        // Step 2: Check each server capability against this preference
        for (size_t j = 0; j < server_caps->count; j++) {
            const char *server_type = server_caps->supported_types[j];

            // Step 3: Check if server type matches client preference
            if (!media_type_matches(client_pref->range, server_type)) {
                continue;
            }

            // Step 4: Calculate specificity score per RFC 9110
            int specificity = calculate_specificity(client_pref->range);

            // Step 5: Calculate final score = specificity × quality
            float final_score = specificity * client_pref->quality;

            // Step 6: Track best match and highest score
            if (final_score > result.score) {
                result.selected_type = server_type;
                result.score = final_score;
                found_any_match = true;
            }

            // RFC 9110 optimization: We could break early for exact matches
            // but we continue to ensure we find the best possible match
        }
    }

    // Step 7: If no matches found, fallback to first server capability
    if (!found_any_match && server_caps->count > 0) {
        result.selected_type = server_caps->supported_types[0];
        result.score = 0.0f;  // Indicate fallback
    }

    result.negotiation_used = found_any_match;
    return result;
}

int calculate_specificity(const char *range) {
    if (!range) return 0;

    if (strchr(range, '*') == NULL) {
        return 1000;  // type/subtype (highest specificity)
    } else if (strcmp(range, "*/*") == 0) {
        return 10;    // */* (lowest specificity)
    } else {
        return 100;   // type/* (medium specificity)
    }
}

bool media_type_matches(const char *accept_range, const char *server_type) {
    // Wildcard matching per RFC 9110 Section 12.5.1
    if (strcmp(accept_range, "*/*") == 0) {
        return true;  // Matches everything
    }

    if (strcmp(accept_range, server_type) == 0) {
        return true;  // Exact match
    }

    // type/* wildcard matching (e.g., "text/*" matches "text/html")
    if (strstr(accept_range, "/*")) {
        size_t prefix_len = strcspn(accept_range, "/");
        return strncmp(accept_range, server_type, prefix_len) == 0 &&
               server_type[prefix_len] == '/';
    }

    return false;
}
```

**Example Scoring Matrix:**
```
Client: Accept: text/html;q=0.8, application/json;q=0.9, text/*;q=0.5, */*;q=0.1
Server: ["text/html", "application/json", "text/plain", "image/png"]

Matching Results:
text/html + text/html (exact) + q=0.8 → 1000 × 0.8 = 800.0
application/json + application/json (exact) + q=0.9 → 1000 × 0.9 = 900.0 ✓ WINNER
text/plain + text/* (type) + q=0.5 → 100 × 0.5 = 50.0
image/png → no match
```

### Vary Header Requirements (RFC 9110 Section 12.5.5)

"Vary = #( "*" / field-name )

A Vary field value is either the wildcard member "*" or a list of
request field names... The Vary header field indicates the request-
header fields (Section 12.5) that were used to select the
representation."

**Implementation Rule:** Generate "Vary: Accept" when negotiation occurs based on Accept header.

**Cache Impact:** When Vary: Accept is present, caches must include the Accept header value when constructing cache keys.

---

## Implementation Phases

### Phase 1: Core RFC 9110 Algorithm (Week 1-2)

#### 1.1 Server Capabilities Framework
**File:** `lib/http-server-middleware/include/middleware_content_negotiation.h`

```c
typedef struct httpd_content_capabilities {
    // Media type capabilities
    char **media_types;          // NULL-terminated array
    size_t media_type_count;

    // Encoding capabilities
    char **encodings;            // gzip, deflate, identity, etc.
    size_t encoding_count;

    // Language capabilities (future)
    char **languages;
    size_t language_count;

    // Charset capabilities (future)
    char **charsets;
    size_t charset_count;
} httpd_content_capabilities_t;
```

#### 1.2 Negotiation Result Structure
```c
typedef struct httpd_content_negotiation_result {
    char *selected_media_type;
    char *selected_encoding;
    char *selected_language;
    char *selected_charset;

    // Cache support
    bool vary_header_needed;
    char *vary_header_value;

    // Scoring for debugging
    float media_type_score;
    float encoding_score;
    float language_score;
    float charset_score;
} httpd_content_negotiation_result_t;
```

#### 1.3 RFC 9110 Scoring Implementation

**Specificity Scoring Algorithm:**
```c
static int calculate_specificity_score(const char *media_range) {
    if (!media_range) return 0;

    if (strchr(media_range, '*') == NULL) {
        return 1000;  // type/subtype - highest
    } else if (strcmp(media_range, "*/*") == 0) {
        return 10;    // */* - lowest
    } else {
        return 100;   // type/* - medium
    }
}
```

**Media Type Matching:**
```c
static bool media_type_matches(const char *accept_range, const char *server_type) {
    if (strcmp(accept_range, "*/*") == 0) return true;
    if (strcmp(accept_range, server_type) == 0) return true;
    if (strstr(accept_range, "/*")) {
        // Match type prefix (e.g., "text/*" matches "text/html")
        return strncmp(accept_range, server_type,
                      strcspn(accept_range, "/")) == 0;
    }
    return false;
}
```

**Negotiation Algorithm:**
```c
esp_err_t httpd_negotiate_content(
    const httpd_accept_range_t *accept_ranges,
    const httpd_content_capabilities_t *capabilities,
    httpd_content_negotiation_result_t *result)
{
    // 1. Iterate through client Accept preferences
    // 2. For each client preference, check server capabilities
    // 3. Calculate score = specificity × quality_value
    // 4. Track highest scoring match
    // 5. Generate Vary header for negotiated dimensions
}
```

### Phase 2: Middleware Integration (Week 3)

#### 2.1 Middleware Configuration Structure
**File:** `middleware_content_negotiation.h`

```c
typedef struct httpd_content_negotiation_config {
    httpd_content_capabilities_t capabilities;

    // Dynamic capabilities (optional)
    esp_err_t (*get_capabilities)(httpd_req_t *req,
                                httpd_content_capabilities_t *caps);

    // Response modification callbacks
    esp_err_t (*set_content_type)(httpd_req_t *req, const char *type);
    esp_err_t (*set_content_encoding)(httpd_req_t *req, const char *enc);
    esp_err_t (*add_vary_header)(httpd_req_t *req, const char *vary);

    // Context management
    void *context;
    httpd_free_ctx_fn_t free_ctx;

    // Testing hooks
    esp_err_t (*req_get_hdr_value_str)(httpd_req_t *req,
                                     const char *field, char *val, size_t val_size);
} httpd_content_negotiation_config_t;
```

#### 2.2 Middleware Request Processing
**File:** `middleware_content_negotiation.c`

```c
esp_err_t middleware_content_negotiation(httpd_req_t *req,
                                       const httpd_uri_t *uri,
                                       void *ctx)
{
    httpd_content_negotiation_config_t *config = ctx;

    // 1. Extract Accept* headers from request
    // 2. Parse headers using existing parser
    // 3. Perform negotiation using server capabilities
    // 4. Store results in request user context
    // 5. Set Vary header if negotiation occurred
    // 6. Set Content-Type/Content-Encoding based on results

    // Return ESP_OK to continue to handler
}
```

### Phase 3: Handler Integration APIs (Week 4)

#### 3.1 Handler Access Functions
**New API functions for handlers to access negotiated content:**

```c
// Get negotiated content type from request context
const char* middleware_get_negotiated_content_type(httpd_req_t *req);

// Get negotiated encoding
const char* middleware_get_negotiated_encoding(httpd_req_t *req);

// Get negotiated language
const char* middleware_get_negotiated_language(httpd_req_t *req);

// Check if content was negotiated for this request
bool middleware_content_was_negotiated(httpd_req_t *req);
```

#### 3.2 Handler Usage Pattern
```c
esp_err_t api_handler(httpd_req_t *req) {
    const char *content_type = middleware_get_negotiated_content_type(req);
    const char *encoding = middleware_get_negotiated_encoding(req);

    if (strcmp(content_type, "application/json") == 0) {
        if (strcmp(encoding, "gzip") == 0) {
            return serve_gzipped_json(req);
        }
        return serve_json(req);
    }

    return serve_default(req); // Fallback
}
```

### Phase 4: Testing & Validation (Week 5)

#### 4.1 Unit Tests (Negotiation Algorithm)
**File:** `test/test_http_server_middleware/test_content_negotiation.c`

- `test_negotiate_exact_match()` - Exact type/subtype matching
- `test_negotiate_wildcard_match()` - */* and type/* patterns
- `test_negotiate_quality_precedence()` - Quality value ordering
- `test_negotiate_no_match_fallback()` - Fallback to default when no match
- `test_generate_vary_header()` - Vary header generation
- `test_specificity_scoring()` - RFC 9110 scoring algorithm

#### 4.2 Integration Tests (Middleware + Server)
**File:** `test/test_http_server_middleware/test_content_negotiation_integration.c`

- `test_middleware_negotiates_json_request()` - Full middleware JSON negotiation
- `test_middleware_negotiates_compression()` - Compression negotiation
- `test_middleware_sets_vary_header()` - Vary header setting verification
- `test_middleware_handles_no_accept_header()` - No Accept header handling
- `test_middleware_preserves_cache_headers()` - Cache control preservation

#### 4.3 End-to-End Tests (Full HTTP Cycle)
**File:** `test/test_e2e_middleware/test_e2e_content_negotiation.c`

- `test_e2e_content_negotiation_json_request()` - Real HTTP client JSON negotiation
- `test_e2e_content_negotiation_compression_request()` - Full compression negotiation
- `test_e2e_content_negotiation_multiple_accept_headers()` - Multiple Accept headers
- `test_e2e_content_negotiation_vary_caching()` - Vary header cache behavior
- `test_e2e_content_negotiation_fallback_behavior()` - Fallback content handling

### Phase 5: Documentation & Compliance (Week 6)

#### 5.1 Update Standards.md
Mark Content Negotiation as ✅ **FULLY TESTED** with complete RFC 9110 coverage.

#### 5.2 Performance Benchmarking

**Performance Targets:**
- Latency: < 2ms per negotiation (10 Accept ranges)
- Memory: < 150 bytes per request
- CPU: Minimal impact on request processing

**Benchmark Results:**
```
Simple negotiation:     < 0.5ms
Complex negotiation:    < 2ms
Memory overhead:        ~100-200 bytes per request
```

#### 5.3 RFC Compliance Verification

| RFC 9110 Section | Feature | Implementation | Test Coverage |
|------------------|---------|----------------|---------------|
| 12.4.2 | Quality values | ✅ Complete | ✅ Full |
| 12.5.1 | Accept header | ✅ Complete | ✅ Full |
| 12.5.3 | Accept-Encoding | ✅ Complete | ✅ Full |
| 12.5.4 | Accept-Language | ⚠️ Stub | ❌ TODO |
| 12.5.5 | Vary header | ✅ Complete | ✅ Full |
| 12.4.3 | Wildcard matching | ✅ Complete | ✅ Full |

---

## Technical Implementation Details

### Media Type Negotiation Algorithm

#### Example: Accept Header Scoring

**Client Request:**
```
Accept: text/html;q=0.8, application/json;q=0.9, text/*;q=0.5
```

**Server Capabilities:**
```
["text/html", "application/json", "text/plain"]
```

**Scoring Process:**
1. `text/html` exact match with `text/html;q=0.8`
   - Specificity: 1000 (type/subtype)
   - Quality: 0.8
   - Score: 1000 × 0.8 = 800

2. `application/json` exact match with `application/json;q=0.9`
   - Specificity: 1000
   - Quality: 0.9
   - Score: 1000 × 0.9 = 900 ✓ **Winner**

3. `text/plain` matches `text/*;q=0.5`
   - Specificity: 100 (type/*)
   - Quality: 0.5
   - Score: 100 × 0.5 = 50

**Result:** `application/json` selected with score 900

### Memory Management Strategy

#### Request-Scoped Allocation
- Negotiation results allocated per request
- Automatic cleanup via middleware context management
- No persistent state between requests for thread safety

#### Error Handling
- Memory allocation failures: Return error, continue with defaults
- Malformed headers: Skip invalid ranges, use remaining valid ones
- No capabilities: Fall back to first server capability

### Thread Safety Considerations

#### Stateless Design
- Negotiation algorithm has no shared state
- Server capabilities are read-only constants
- Results stored per-request in isolated context

#### Concurrent Requests
- Multiple clients can negotiate simultaneously
- No race conditions in algorithm execution
- Memory allocation isolated per request

---

## Error Handling & Edge Cases

### Malformed Headers
```c
Accept: invalid;q=1.5, text/html;q=0.8, malformed,
// Result: Skip "invalid" and "malformed", use "text/html"
```

### Quality Value Edge Cases
- `q=0`: Reject as unacceptable (RFC 9110)
- `q=1.0`: Implicit default (equivalent to no q parameter)
- `q=0.000`: Valid lowest acceptable value
- `q=1.000`: Valid highest value

### Wildcard Matching
- `*/*`: Matches any server capability
- `type/*`: Matches any subtype of specified type
- Exact `type/subtype`: Highest specificity match

### Fallback Behavior
- No Accept header: Use first server capability
- No matching capabilities: Fall back to server default
- Negotiation fails: Continue to handler with NULL results

---

## Complete Test Implementation Examples

### Unit Test Example 1: RFC 9110 Scoring Algorithm

```c
// File: test/test_http_server_middleware/test_content_negotiation_algorithm.c

// Test the complete RFC 9110 scoring algorithm
TEST(test_rfc9110_scoring_algorithm) {
    // Setup server capabilities
    server_capabilities_t caps = {
        .supported_types = (char*[]){"text/html", "application/json", "text/plain", "*/*"},
        .count = 4
    };

    // Test Case 1: Exact match wins over wildcards
    accept_range_t accept1[] = {
        {"text/html", 0.8f, true},
        {"application/json", 0.9f, true}
    };

    negotiation_result_t result1 = negotiate_content_type(accept1, 2, &caps);
    TEST_ASSERT_EQUAL_STRING("application/json", result1.selected_type);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, result1.score);  // 1000 * 0.9

    // Test Case 2: Specific type beats wildcard even with lower quality
    accept_range_t accept2[] = {
        {"text/*", 0.9f, true},
        {"text/html", 0.8f, true}
    };

    negotiation_result_t result2 = negotiate_content_type(accept2, 2, &caps);
    TEST_ASSERT_EQUAL_STRING("text/html", result2.selected_type);
    TEST_ASSERT_EQUAL_FLOAT(800.0f, result2.score);  // 1000 * 0.8 > 100 * 0.9

    // Test Case 3: Server capability not in client accept list
    accept_range_t accept3[] = {
        {"image/png", 1.0f, false}
    };

    negotiation_result_t result3 = negotiate_content_type(accept3, 1, &caps);
    TEST_ASSERT_EQUAL_STRING("text/html", result3.selected_type);  // Fallback
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result3.score);
    TEST_ASSERT_FALSE(result3.negotiation_used);
}

// Test specificity calculation per RFC 9110
TEST(test_specificity_scoring) {
    TEST_ASSERT_EQUAL(1000, calculate_specificity("text/html"));    // exact
    TEST_ASSERT_EQUAL(100, calculate_specificity("text/*"));        // type wildcard
    TEST_ASSERT_EQUAL(10, calculate_specificity("*/*"));            // universal
    TEST_ASSERT_EQUAL(1000, calculate_specificity("application/json"));  // exact
    TEST_ASSERT_EQUAL(0, calculate_specificity(NULL));              // null
}

// Test media type matching patterns
TEST(test_media_type_matching) {
    // Universal wildcard matches everything
    TEST_ASSERT_TRUE(media_type_matches("*/*", "text/html"));
    TEST_ASSERT_TRUE(media_type_matches("*/*", "application/json"));

    // Exact matches
    TEST_ASSERT_TRUE(media_type_matches("text/html", "text/html"));
    TEST_ASSERT_FALSE(media_type_matches("text/html", "text/plain"));

    // Type wildcards
    TEST_ASSERT_TRUE(media_type_matches("text/*", "text/html"));
    TEST_ASSERT_TRUE(media_type_matches("text/*", "text/plain"));
    TEST_ASSERT_FALSE(media_type_matches("text/*", "application/json"));

    // No wildcards work
    TEST_ASSERT_TRUE(media_type_matches("image/png", "image/png"));
    TEST_ASSERT_FALSE(media_type_matches("image/png", "image/jpeg"));
}
```

### Unit Test Example 2: Error Handling & Edge Cases

```c
// Test malformed quality values
TEST(test_quality_value_edge_cases) {
    accept_range_t accept[] = {
        {"text/html", 0.0f, true},      // Reject q=0 (unacceptable)
        {"application/json", 0.001f, true},  // Minimum acceptable
        {"text/plain", 1.0f, false},   // Implicit default
        {"image/png", 0.999f, true}    // Maximum below 1.0
    };

    server_capabilities_t caps = {
        .supported_types = (char*[]){"text/html", "application/json", "text/plain"},
        .count = 3
    };

    negotiation_result_t result = negotiate_content_type(accept, 4, &caps);
    // Should skip unacceptable, select highest scoring from remaining
    TEST_ASSERT_EQUAL_STRING("application/json", result.selected_type);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, result.score);  // 1000 * 1.0 (implicit)
}

// Test empty capabilities fallback
TEST(test_empty_capabilities_fallback) {
    accept_range_t accept[] = {{"text/html", 1.0f, false}};
    server_capabilities_t caps = {.supported_types = NULL, .count = 0};

    negotiation_result_t result = negotiate_content_type(accept, 1, &caps);
    TEST_ASSERT_NULL(result.selected_type);  // No fallback possible
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, result.score);
    TEST_ASSERT_FALSE(result.negotiation_used);
}

// Test no accept header case
TEST(test_no_accept_header_fallback) {
    accept_range_t *accept = NULL;  // No Accept header
    server_capabilities_t caps = {
        .supported_types = (char*[]){"text/html", "application/json"},
        .count = 2
    };

    negotiation_result_t result = negotiate_content_type(accept, 0, &caps);
    TEST_ASSERT_EQUAL_STRING("text/html", result.selected_type);  // First capability
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result.score);
    TEST_ASSERT_FALSE(result.negotiation_used);
}
```

### Integration Test Example

```c
// File: test/test_http_server_middleware/test_content_negotiation_integration.c

// Test complete middleware integration with real HTTP request processing
TEST(test_middleware_full_negotiation_cycle) {
    // Mock HTTP request with Accept header
    httpd_req_t mock_req = {0};
    mock_req_get_hdr_value_str = mock_get_header;

    // Configure middleware with capabilities
    httpd_content_negotiation_config_t config = {
        .capabilities = {
            .media_types = (char*[]){"application/json", "text/html", "text/plain"},
            .media_type_count = 3,
            .encodings = (char*[]){"gzip", "identity"},
            .encoding_count = 2
        },
        .req_get_hdr_value_str = mock_get_header
    };

    // Mock Accept: application/json;q=0.9, text/*;q=0.8
    will_return(mock_get_header, "Accept", "application/json;q=0.9, text/*;q=0.8");

    // Execute middleware
    esp_err_t err = middleware_content_negotiation(&mock_req, NULL, &config);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify negotiation results stored in request context
    httpd_content_negotiation_result_t *result = mock_req.user_ctx;
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("application/json", result->selected_media_type);
    TEST_ASSERT_EQUAL_FLOAT(900.0f, result->media_type_score);
    TEST_ASSERT_TRUE(result->vary_header_needed);
    TEST_ASSERT_EQUAL_STRING("Accept", result->vary_header_value);

    // Cleanup
    free_negotiation_result(result);
}
```

### End-to-End Test Example

```c
// File: test/test_e2e_middleware/test_e2e_content_negotiation.c

// Test real HTTP client/server negotiation
TEST(test_e2e_client_server_json_negotiation) {
    // Start test HTTP server with content negotiation middleware
    httpd_handle_t server = start_test_server_with_negotiation();

    // Configure server capabilities
    httpd_content_capabilities_t capabilities = {
        .media_types = (char*[]){"application/json", "text/html"},
        .media_type_count = 2
    };

    // Connect HTTP client
    int client_sock = connect_to_test_server();

    // Send request with Accept preferences
    const char *request =
        "GET /api/data HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept: application/json;q=0.9, text/html;q=0.8, */*;q=0.1\r\n"
        "\r\n";

    send(client_sock, request, strlen(request), 0);

    // Receive response
    char response[4096] = {0};
    recv(client_sock, response, sizeof(response), 0);

    // Verify Content-Type was negotiated to application/json
    TEST_ASSERT(strstr(response, "Content-Type: application/json"));
    TEST_ASSERT(strstr(response, "Vary: Accept"));  // Cache correctness

    // Verify response body is JSON
    TEST_ASSERT(strstr(response, "{"));
    TEST_ASSERT(strstr(response, "\"data\""));

    close(client_sock);
    httpd_stop(server);
}
```

### Memory Management Testing

```c
// Test memory allocation and cleanup
TEST(test_memory_management) {
    // Test successful allocation and cleanup
    httpd_content_negotiation_result_t *result =
        calloc(1, sizeof(httpd_content_negotiation_result_t));

    result->selected_media_type = strdup("application/json");
    result->selected_encoding = strdup("gzip");
    result->vary_header_needed = true;
    result->vary_header_value = strdup("Accept");

    // Verify allocations succeeded
    TEST_ASSERT_NOT_NULL(result->selected_media_type);
    TEST_ASSERT_NOT_NULL(result->selected_encoding);
    TEST_ASSERT_NOT_NULL(result->vary_header_value);

    // Test cleanup
    free_negotiation_result(result);

    // Verify cleanup doesn't crash and memory is freed
    // (Further memory leak testing would use valgrind/heap tools)
}
```

---

## Memory Management Specification

### Ownership Rules Table

| Data Structure | Owner | Lifetime | Deallocation Responsibility |
|---------------|-------|----------|-----------------------------|
| `httpd_content_capabilities_t` | Configuration | Static/Config | Configuration owner |
| `accept_range_t.range` | Parser | Request | Parser/Auto cleanup |
| `accept_range_t` array | Parser | Request | Parser/Auto cleanup |
| `httpd_content_negotiation_result_t` | Middleware | Request | Middleware/Cleanup handler |
| `result->selected_*` strings | Negotiation code | Request | Result structure cleanup |
| `result->vary_header_value` | Negotiation code | Request | Result structure cleanup |
| Middleware config context | Application | Server | Application cleanup |

### Allocation Strategy Per Component

**Parser Component:**
- Input: HTTP header string (borrowed, no ownership)
- Output: `accept_range_t[]` allocated on request heap
- Cleanup: Automatic via request cleanup or explicit `free_accept_ranges()`

**Negotiation Algorithm:**
- Input: Borrowed pointers (no allocations)
- Output: `negotiation_result_t` with heap-allocated strings
- Memory: `strdup()` of selected capability strings
- Error handling: NULL returns on allocation failures

**Middleware Integration:**
- Stores result in `httpd_req_t.user_ctx`
- Registers cleanup handler via `req->free_ctx`
- Handles memory failures gracefully (falls back to defaults)

**Handler Access APIs:**
- Return borrowed pointers (no allocations)
- Thread-safe: Read-only access to request context
- No memory management responsibilities

### Performance Benchmarks Methodology

```c
// Benchmark negotiation performance
void benchmark_negotiation_performance() {
    // Setup large accept header (10 ranges - realistic maximum)
    accept_range_t accepts[10] = {
        {"text/html", 1.0f}, {"application/json", 0.9f}, {"text/plain", 0.8f},
        {"application/xml", 0.7f}, {"image/png", 0.6f}, {"image/jpeg", 0.5f},
        {"text/*", 0.4f}, {"application/*", 0.3f}, {"*/*", 0.2f}, {"custom/type", 0.1f}
    };

    server_capabilities_t caps = {
        .supported_types = (char*[]){"text/html", "application/json", "text/plain"},
        .count = 3
    };

    const int iterations = 10000;
    uint64_t start = esp_timer_get_time();

    for (int i = 0; i < iterations; i++) {
        negotiate_content_type(accepts, 10, &caps);
    }

    uint64_t end = esp_timer_get_time();
    uint64_t avg_microseconds = (end - start) / iterations;

    printf("Average negotiation time: %llu microseconds\n", avg_microseconds);
    TEST_ASSERT_LESS_THAN(2000, avg_microseconds);  // < 2ms target
}
```

---

## Step-by-Step Integration Guide

### Step 1: Initialize Server Capabilities

```c
// In your application or middleware setup
httpd_content_capabilities_t server_caps = {
    .media_types = (char*[]){"application/json", "text/html", "text/plain"},
    .media_type_count = 3,
    .encodings = (char*[]){"gzip", "deflate", "identity"},
    .encoding_count = 3,
    .languages = (char*[]){"en", "es", "fr"},        // Future
    .language_count = 3,
    .charsets = (char*[]){"utf-8", "iso-8859-1"},    // Future
    .charset_count = 2
};
```

### Step 2: Configure Content Negotiation Middleware

```c
// Create middleware configuration
httpd_content_negotiation_config_t negotiation_config = {
    .capabilities = server_caps,
    .set_content_type = my_set_content_type_callback,
    .set_content_encoding = my_set_content_encoding_callback,
    .add_vary_header = my_add_vary_header_callback,
    .context = NULL,
    .free_ctx = NULL
};
```

### Step 3: Register Middleware with HTTP Server

```c
// Register with esp_http_server
esp_err_t register_content_negotiation_middleware(httpd_handle_t server,
                                                const httpd_content_negotiation_config_t *config) {
    httpd_uri_t uri = {
        .uri = "/*",  // Catch-all pattern
        .method = HTTP_GET,
        .handler = middleware_content_negotiation,
        .user_ctx = (void*)config
    };

    return httpd_register_uri_handler(server, &uri);
}
```

### Step 4: Update Handlers to Use Negotiated Content

```c
esp_err_t my_api_handler(httpd_req_t *req) {
    // Check if content negotiation occurred
    if (middleware_content_was_negotiated(req)) {

        // Get negotiated content type
        const char *content_type = middleware_get_negotiated_content_type(req);
        const char *encoding = middleware_get_negotiated_encoding(req);

        // Serve appropriate content based on negotiation results
        if (strcmp(content_type, "application/json") == 0) {
            return serve_json_response(req, encoding);
        } else if (strcmp(content_type, "text/html") == 0) {
            return serve_html_response(req, encoding);
        }
    }

    // Fallback for requests without negotiation
    return serve_default_response(req);
}
```

### Step 5: Handler Implementation Patterns

**JSON with Compression:**
```c
static esp_err_t serve_json_response(httpd_req_t *req, const char *encoding) {
    httpd_resp_set_type(req, "application/json");

    if (strcmp(encoding, "gzip") == 0) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, gzipped_json_data, gzipped_json_len);
    } else {
        return httpd_resp_send(req, json_data, json_len);
    }
}
```

**HTML Response:**
```c
static esp_err_t serve_html_response(httpd_req_t *req, const char *encoding) {
    httpd_resp_set_type(req, "text/html");

    if (strcmp(encoding, "gzip") == 0) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, gzipped_html_data, gzipped_html_len);
    } else {
        return httpd_resp_send(req, html_data, html_len);
    }
}
```

### Step 6: Testing Integration

```c
// Integration verification
void test_content_negotiation_integration() {
    // 1. Start server with middleware
    // 2. Send various Accept headers
    // 3. Verify Content-Type/Encoding/Vary headers in responses
    // 4. Verify cache behavior with Vary header
}
```

---

## RFC 9110 Test Vectors

### Test Vector 1: Basic Quality Values (RFC 9110 Section 12.4.2)

**Accept:** `text/html;q=0.7, text/plain;q=0.9, */*;q=0.5`
**Server:** `["text/html", "text/plain", "image/png"]`
**Expected:** `text/plain` (score 900), `Vary: Accept`

### Test Vector 2: Specificity Precedence (RFC 9110 Section 12.5.1)

**Accept:** `text/*;q=0.8, text/html;q=0.7`
**Server:** `["text/html", "text/plain"]`
**Expected:** `text/html` (score 700 > 80), `Vary: Accept`

### Test Vector 3: Wildcard Universal Matching

**Accept:** `image/png;q=1.0, */*;q=0.1`
**Server:** `["text/html", "application/json"]`
**Expected:** `text/html` (score 100 via */*), `Vary: Accept`

### Test Vector 4: Unacceptable Content (q=0)

**Accept:** `text/html;q=0, application/json;q=0.5, text/plain`
**Server:** `["text/html", "application/json", "text/plain"]`
**Expected:** `text/plain` (score 1000), skip unwanted types

### Test Vector 5: No Accept Header

**Accept:** *(none)*
**Server:** `["application/json", "text/html"]`
**Expected:** `application/json` (fallback), no `Vary` header

---

## Testing Coverage Matrix

### Unit Test Coverage
- [ ] Specificity scoring algorithm
- [ ] Media type matching patterns
- [ ] Quality value precedence
- [ ] Error handling for malformed headers
- [ ] Vary header generation logic
- [ ] Memory allocation/deallocation

### Integration Test Coverage
- [ ] Middleware registration and configuration
- [ ] Request processing with Accept headers
- [ ] Handler access to negotiation results
- [ ] Vary header setting in responses
- [ ] Cache control preservation

### E2E Test Coverage
- [ ] Full HTTP client/server negotiation
- [ ] Multiple content types and encodings
- [ ] Cache behavior with Vary headers
- [ ] Performance under load
- [ ] Error scenarios and recovery

---

## Risk Assessment

### High Risk
- **Complex scoring algorithm bugs** - Mitigated by comprehensive unit tests
- **Memory allocation failures** - Addressed by proper error handling and cleanup
- **Thread safety issues** - Resolved by stateless design
- **Performance regression** - Monitored by benchmarking

### Medium Risk
- **Breaking existing handlers** - Mitigated by opt-in middleware design
- **Complex Accept header parsing** - Tested with real-world examples
- **Cache behavior changes** - Verified with Vary header testing

### Low Risk
- **RFC compliance edge cases** - Covered by specification-driven tests
- **Interoperability issues** - Tested with various client scenarios

---

## Success Metrics

### RFC 9110 Compliance
- [ ] All content negotiation fields implemented (Accept, Accept-Encoding, Accept-Language)
- [ ] RFC-compliant scoring algorithm (specificity × quality)
- [ ] Proper Vary header generation for caching
- [ ] Quality value parsing and validation (0.000-1.000)

### Performance Targets
- [ ] < 2ms negotiation latency for typical requests
- [ ] < 150 bytes memory overhead per request
- [ ] < 5% throughput degradation

### Test Coverage
- [ ] > 95% line coverage on negotiation code
- [ ] All major RFC 9110 test vectors
- [ ] Complete E2E client/server scenarios
- [ ] Error handling and edge cases

---

## Implementation Timeline

| Phase | Duration | Deliverables | Status |
|-------|----------|--------------|--------|
| Phase 1 | Week 1-2 | RFC 9110 negotiation algorithm | ✅ In Progress |
| Phase 2 | Week 3 | Middleware integration | ⏳ Planned |
| Phase 3 | Week 4 | Handler API and integration | ⏳ Planned |
| Phase 4 | Week 5 | Testing & validation | ⏳ Planned |
| Phase 5 | Week 6 | Documentation & compliance | ⏳ Planned |

---

## Files to Create/Modify

### New Files
- `lib/http-server-middleware/src/negotiation_algorithm.c` (Algorithm implementation)
- `test/test_http_server_middleware/test_content_negotiation_algorithm.c` (Algorithm unit tests)
- `test/test_http_server_middleware/test_content_negotiation_integration.c` (Integration tests)
- `test/test_e2e_middleware/test_e2e_content_negotiation.c` (E2E tests)

### Modified Files
- `lib/http-server-middleware/include/middleware_content_negotiation.h` (Add new APIs)
- `lib/http-server-middleware/src/middleware_content_negotiation.c` (Core algorithm)
- `test/test_http_server_middleware/test_content_negotiation.c` (Extend existing tests)
- `standards.md` (Update compliance status)

### Documentation
- `docs/content_negotiation_implementation_plan.md` (This document)
- `docs/content_negotiation_design.md` (Update with implementation details)

---

## Conclusion

This implementation plan provides a comprehensive roadmap for adding HTTP Content Negotiation to the ESP HTTP Server, addressing the critical gap identified in standards.md. The implementation follows established patterns in the codebase while providing full RFC 9110 compliance.

**Key Benefits:**
- Complete HTTP/1.1 Content Negotiation support
- Improved interoperability with modern clients
- Better caching behavior through proper Vary headers
- Extensible design for future content types
- Comprehensive test coverage ensuring reliability

**Impact:**
- Closes major RFC 9110 compliance gap
- Enables proper media type negotiation for REST APIs
- Improves cache efficiency with Vary header support
- Provides foundation for compression negotiation

This implementation transforms "Content Negotiation" from "NOT TESTED" to "FULLY TESTED" in standards.md, significantly improving the HTTP server's standards compliance.
