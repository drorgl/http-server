# HTTP Server Middleware Library

This library provides a middleware framework for the ESP HTTP Server, implementing the design specified in `middleware_design.md`. The middleware system enables pluggable, reusable components for handling cross-cutting concerns such as authentication, security, logging, and HTTP feature compliance.



## Features Implemented (POC)

### Core Components
- **Data Structures**: `httpd_middleware_config_t`, function types, wrapper context
- **Wrapper Function**: `httpd_uri_wrap_with_middleware()` creates wrapped handlers
- **Execution Engine**: Priority-ordered middleware execution with short-circuit support

### Middleware Execution Model
1. Middleware functions execute in **registration order** (array position order)
2. Middleware execute before the original URI handler
3. Each middleware can inspect `httpd_req_t`, `httpd_uri_t`, and context
4. Middleware returns `ESP_OK` to continue or error to short-circuit
5. Disabled middleware are skipped
6. URI pattern filtering using wildcard matching
7. HTTP method filtering support

### Example Middleware
- **Logging**: Logs request details
- **Authentication**: Callback-based Basic Auth (dynamic credential/path validation)

## Design Approach

This implementation follows the **Dependency Injection** approach for maximum flexibility and testability, using callback function pointers injected at runtime. Middleware modules no longer directly call HTTP server functions, allowing for easy mocking in unit tests and cross-platform compatibility.

### Dependency Injection

All `*config_t` structures now include function pointer callbacks for HTTP operations used by that module:

- `auth_config_t`: `req_get_hdr_value_str`, `resp_set_status`, `resp_set_hdr`, `resp_send_err`, `check_credentials`, `requires_auth`
- `cors_config_t`: `req_get_hdr_value_str`, `resp_set_status`, `resp_set_hdr`, `resp_send_err`, `resp_send`
- `logging_config_t`: `req_get_hdr_value_str`, `method_str`, `printf`
- `httpd_middleware_config_t`: `uri_match_wildcard`

In production code, assign the actual HTTP server functions:

```c
#include <http_server.h>  // For function pointers

cors_config_t config = {
    .allowed_origins = "https://example.com",
    .allowed_methods = "GET,POST",
    .allow_credentials = false,
    .max_age = 3600,
    // Function callback assignments
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    .resp_set_status = httpd_resp_set_status,
    .resp_set_hdr = httpd_resp_set_hdr,
    .resp_send_err = httpd_resp_send_err,
    .resp_send = httpd_resp_send
};
```

In unit tests, assign mock function pointers for isolated testing.

## Breaking Changes

### v3.1.0 - Authentication Callbacks
**Replaced static credentials/path bypass with flexible callbacks for dynamic auth:**

- ❌ Removed `auth_config_t.username`, `auth_config_t.password`, `auth_config_t.allow_public`
- ✅ Added `auth_config_t.check_credentials()` (validate username/password)
- ✅ Added `auth_config_t.requires_auth()` (per-URI auth bypass)

### v3.0.0 - Dependency Injection
**Introduced DI for testability/cross-platform:**

- ✅ Added HTTP callback pointers to all `*config_t`
- ❌ Removed direct httpd_* calls from middleware

### Migration Guide

#### From v3.0.0 → v3.1.0 (Auth Callbacks)
```c
// BEFORE (static):
auth_config_t config = {
    .username = "admin",
    .password = "secret",
    .allow_public = true,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    // ...
};

// AFTER (callbacks):
static esp_err_t my_check(const char *u, const char *p, void *ctx) {
    return (strcmp(u, "admin")==0 && strcmp(p, "secret")==0) ? ESP_OK : ESP_FAIL;
}
static bool my_requires(const char *uri, void *ctx) {
    return strncmp(uri, "/public/", 8) != 0;
}

auth_config_t config = {
    .check_credentials = my_check,
    .check_ctx = NULL,
    .requires_auth = my_requires,
    .bypass_ctx = NULL,
    .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
    // ...
};
```

#### v3.0.0 DI Migration (unchanged)
[existing code]

## Usage Example

```c
#include "http_server_middleware.h"

// Original handler
esp_err_t my_handler(httpd_req_t *req) {
    httpd_resp_sendstr(req, "Hello World!");
    return ESP_OK;
}

// Middleware configurations
httpd_middleware_config_t configs[] = {
    {
        .func = middleware_logging,
        .enabled = true
    },
    {
        .func = middleware_auth,
        .context = &(auth_config_t){
            .check_credentials = my_auth_check,
            .check_ctx = NULL,
            .requires_auth = my_requires_auth,
            .bypass_ctx = NULL,
            .req_get_hdr_value_str = httpd_req_get_hdr_value_str,
            .resp_set_status = httpd_resp_set_status,
            .resp_set_hdr = httpd_resp_set_hdr,
            .resp_send_err = httpd_resp_send_err
        },
        .enabled = true
    }
};

// Wrap the handler with middleware
httpd_uri_t my_uri = {
    .uri = "/api/data",
    .method = HTTP_GET,
    .handler = my_handler
};

httpd_uri_t *wrapped_uri = httpd_uri_wrap_with_middleware(&my_uri, configs, 2);

// Register the wrapped handler
httpd_register_uri_handler(server, wrapped_uri);

// Cleanup when done
free(wrapped_uri);
```

## Implementation Notes

- **Standalone Library**: No modifications to core http-server
- **Handler Wrapping**: Application-level integration
- **Cross-platform**: Works on ESP and native platforms
- **Memory Management**: Caller must free returned `httpd_uri_t`

### CORS Middleware (New Feature)

**CORS Middleware** handles Cross-Origin Resource Sharing, essential for modern web applications:

```c
// Configure CORS
cors_config_t cors_config = {
    .allowed_origins = "http://localhost:3000,https://my-app.com",
    .allowed_methods = "GET,POST,PUT,DELETE,OPTIONS",
    .allowed_headers = "Content-Type,Authorization",
    .allow_credentials = true,
    .max_age = 86400  // 24 hours preflight cache
};

// Middleware configuration
httpd_middleware_config_t cors_middleware = {
    .func = middleware_cors,
    .context = &cors_config,
    .uri_pattern = "/api/*",  // Apply to API endpoints
    .enabled = true
};

// Handled automatically:
// - OPTIONS preflight requests
// - Origin validation
// - Appropriate CORS headers
// - Error responses for invalid origins
```

**Features:**
- ✅ Origin validation (allow/deny lists)
- ✅ Preflight OPTIONS handling
- ✅ Credentials support
- ✅ Configurable cache duration
- ✅ Comprehensive header support

### Authentication Middleware (Enhanced)

**Dynamic Basic Auth with callbacks for credential validation and path bypass:**

**Features:**
- ✅ Per-request `requires_auth(uri)` bypass (e.g., `/public/*`, health checks)
- ✅ `check_credentials(username, password)` for DB/JWT/roles
- ✅ Full Basic Auth parsing/validation (header, base64, format)
- ✅ 401 responses with WWW-Authenticate
- ✅ Dependency injection for testing

**Example:**
```c
static bool requires_auth_cb(const char *uri, void *ctx) {
    return strncmp(uri, "/public/", 8) != 0 && strcmp(uri, "/health") != 0;
}
static esp_err_t creds_cb(const char *u, const char *p, void *ctx) {
    return (strcmp(u, "admin")==0 && strcmp(p, "secret")==0) ? ESP_OK : ESP_FAIL;
}

auth_config_t auth_cfg = {
    .requires_auth = requires_auth_cb,
    .check_credentials = creds_cb,
    // + DI callbacks
};
```

## Files Structure

```
lib/http-server-middleware/
├── CMakeLists.txt
├── include/
│   ├── http_server_middleware.h
│   ├── middleware_auth.h
│   ├── middleware_logging.h
│   └── middleware_cors.h
├── src/
│   ├── http_server_middleware.c
│   ├── middleware_auth.c
│   ├── middleware_logging.c
│   └── middleware_cors.c
├── test/
│   └── test_http_server_middleware/
└── README.md
```

## Testing

Unit tests in `test/test_http_server_middleware/`:
- ✅ Framework + individual middleware (auth, logging, CORS)
- ✅ `pio test -e native -vvv`
