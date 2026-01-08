# C HTTP Server

![http-server](http-server.png)

[![Status](https://github.com/drorgl/http-server/actions/workflows/platformio-test.yml/badge.svg)](https://github.com/drorgl/http-server/actions/workflows/platformio-test.yml)

## Overview

The ESP HTTP Server is a lightweight, RFC-compliant HTTP/1.1 server library for ESP32 and cross-platform development. It provides both traditional HTTP endpoints and WebSocket support with a middleware framework for authentication, security, and content negotiation.

Key features include:
- Full HTTP/1.1 compliance with persistent connections and transfer encoding
- RFC 6455 WebSocket support with fragmentation and extensions
- Middleware framework for pluggable components (authentication, CORS, logging)
- Cross-platform compatibility (ESP32, Linux, Windows/MINGW64)
- Comprehensive test suite with extensive RFC compliance testing

## Features

- **HTTP/1.1 Protocol Support**: Full compliance including persistent connections, transfer encoding, and security hardening
- **WebSocket Integration**: Bidirectional communication with fragmentation, masking, and extension negotiation
- **Security Features**: Built-in protection against response splitting, request smuggling, and CRLF injection
- **Authentication**: Basic authentication middleware (with Digest authentication planned)
- **Content Negotiation**: Accept headers processing, quality values, and Vary header support
- **Range Requests**: Partial content delivery with proper header handling
- **Conditional Requests**: ETag and If-Match/If-None-Match support
- **Middleware Architecture**: Pluggable components for logging, CORS, and custom processing

## Standards Compliance

Based on comprehensive analysis in `standards.md`, the server is compliant with:

- **RFC 9110** (HTTP Semantics): Full support for Authentication, Conditional Requests, Range Requests, Content Negotiation
- **RFC 9112** (HTTP/1.1 Message Syntax): Transfer-Encoding, Connection Management, Security Hardening
- **RFC 6455** (WebSocket Protocol): Full WebSocket support with extensions and fragmentation
- **RFC 1945** (HTTP/1.0): PUT/DELETE/HEAD methods, header support
- **RFC 1945 Authentication**: Basic authentication per RFC 7617
- **RFC 9112 Connection Persistence**: Keep-alive and connection management

## Documentation

### API Reference
- **Main API Documentation**: `lib/http-server/docs/http-server.rst` - Complete HTTP server API reference with examples

### Architecture and Design
- **Development Guide**: `docs/development.md` - Setup, building, testing, and development workflow
- **Coding Standards**: `docs/coding-standards.md` - Code style and best practices
- **Middleware Design**: `docs/middleware_design.md` - Pluggable middleware framework design
- **Authentication Design**: `docs/authentication_design.md` - Authentication systems architecture
- **WebSocket Design**: `docs/websocket_design.md` - WebSocket implementation and usage patterns
- **Technical Debt**: `docs/http_server_technical_debt.md` - Known issues and architectural notes

### Additional Design Documents
- `docs/conditional_requests_design.md` - Conditional requests implementation
- `docs/content_negotiation_design.md` - Content negotiation algorithms
- `docs/range_requests_design.md` - Range request processing
- `docs/websocket_fragmentation_design.md` - Message fragmentation handling
- `docs/websocket_extensions_design.md` - Extension negotiation
- `docs/websocket_masking_design.md` - Client masking validation
- `docs/connection_limiting_strategies_design.md` - Connection management strategies
- `docs/connection_persistence_design.md` - Keep-alive implementation
- `docs/content_negotiation_implementation_plan.md` - Implementation roadmap

## Examples

### HTTP Server Examples
- `examples/esp_http_client/` - Basic HTTP client operations for testing
- `examples/http_server/simple/` - Basic HTTP server with custom handlers
- `examples/http_server/advanced_tests/` - Advanced server testing scenarios
- `examples/http_server/ws_echo_server/` - WebSocket echo server implementation
- `examples/http_server/ws_extensions_server/` - WebSocket with extension negotiation
- `examples/http_server/file_serving/` - HTTP file server with upload/download
- `examples/http_server/chunked_example/` - Chunked transfer encoding
- `examples/http_server/connection_persistence/` - Connection persistence testing
- `examples/http_server/persistent_sockets/` - Persistent socket usage
- `examples/http_server/restful_server/` - RESTful API server implementation
- `examples/http_server/async_handlers/` - Asynchronous request handling
- `examples/http_server/captive_portal/` - Captive portal implementation

### HTTPS Examples
- `examples/https_mbedtls/` - HTTPS server with mbedTLS
- `examples/https_request/` - HTTPS client requests
- `examples/https_server/` - HTTPS server implementations
- `examples/https_x509_bundle/` - X.509 certificate bundles

## Libraries and Components

### Core HTTP Server
- **`lib/http-server/`** - Main HTTP server library with WebSocket support (see `lib/http-server/docs/http-server.rst`)

### Middleware Framework
- **`lib/http-server-middleware/`** - Pluggable middleware for authentication, CORS, and logging (see `lib/http-server-middleware/README.md`)

### Supporting Libraries
- **`lib/http-parser/`** - Enhanced Joyent HTTP parser (see `lib/http-parser/README.md`)
- **`lib/logger/`** - Logging utilities for debugging (see `lib/logger/readme.md`)
- **`lib/base64/`** - Base64 encoding/decoding for authentication
- **`lib/hashing/`** - SHA1 hashing utilities
- **`lib/generic_event_groups/`** - Cross-platform event handling

## Testing

- **Test Suite**: Comprehensive coverage in `test/` directory
- **Test Documentation**: `test/README`, `test/test_esp_http_server/test.md`
- **Coverage Analysis**: PlatformIO-based coverage reporting
- **CI/CD**: GitHub Actions workflow for automated testing (ESP32 + Linux)
- **Development Testing Guide**: `docs/development.md`

The test suite includes 400+ tests covering HTTP methods, WebSocket functionality, security features, and RFC compliance.

## Development

- **Development Guide**: `docs/development.md`
- **Build Environments**: PlatformIO configurations for ESP32, Linux, Windows
- **Agent Rules**: `AGENTS.md` - Development workflows and best practices
- **Standards Analysis**: `standards.md` - RFC compliance matrix and gap analysis

## Technical Debt

Known architectural considerations documented in `docs/http_server_technical_debt.md`. Most resolved issues include:
- WebSocket API contract violations
- Connection persistence implementation
- Security hardening measures
- Test framework stability improvements

## TODO

- [x] Implement middleware framework (Completed - Basic auth, CORS, logging)
- [ ] Implement Digest authentication
- [ ] Resolve remaining control socket isolation conflicts (Technical debt item)
- [ ] Add support for WebSocket compression extensions
- [ ] HTTPS Server (Not Implemented Yet - Just Forked)

## Licensing

This project is licensed under multiple open-source licenses depending on the component. All licenses are standard OSI-approved licenses.

### Primary License (Apache-2.0)

The main project is a fork of [Espressif's esp-http-server](https://github.com/espressif/esp-idf/tree/master/components/esp_http_server) and is licensed under Apache License 2.0. The root `LICENSE` file contains the full Apache-2.0 license text.

All files forked from Espressif's esp-idf project retain their original Apache-2.0 license headers and copyright notices. This includes:
- `lib/http-server/` - Core HTTP server implementation
- `lib/logger/` - Logging utilities (forked from esp-idf)

### Custom Middleware (Apache-2.0)

The `lib/http-server-middleware/` directory contains custom middleware components licensed under Apache License 2.0 with copyright held by Dror Gluska. A dedicated `lib/http-server-middleware/LICENSE` file contains the Apache-2.0 license text with appropriate attribution.

### Third-Party Components

- **`lib/http-parser/`** - Enhanced Joyent HTTP parser (MIT license)
  - Original copyright: Joyent, Inc. and other Node contributors
  - License file: `lib/http-parser/LICENSE-MIT`

### License Compliance

This project maintains license compatibility by using Apache-2.0 throughout, which allows inclusion of MIT-licensed components. No original copyright notices from forked esp-idf files have been modified. All license files are included with their respective components for clarity and compliance.
