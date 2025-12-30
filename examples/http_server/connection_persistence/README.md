| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 | Linux |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- | ----- |

# HTTP Server Connection Persistence Example

The Example demonstrates RFC 9112 HTTP/1.1 connection persistence features:
- HTTP/1.1 connections remain persistent by default (no `Connection: close` header)
- `Connection: close` header enforcement for connection termination
- HTTP/1.0 behavior (close by default, persistent with `Connection: keep-alive`)
- Connection state monitoring and reporting
- Configurable connection limits and timeouts

## Overview

This example differs from the `persistent_sockets` example by focusing specifically on RFC 9112 HTTP/1.1 connection persistence rather than session contexts. It demonstrates the core HTTP protocol behavior for connection management.

## Endpoints

### GET /status
Shows real-time connection state information in JSON format:
```json
{
  "connection_state": "persistent",
  "will_persist": true,
  "request_count": 2,
  "connection_close_after_response": false,
  "is_websocket": false,
  "version": "HTTP/1.1",
  "sockfd": 123
}
```

### GET /close
Forces the connection to close after the response by setting the close flag programmatically.

### GET /multiple
Demonstrates multiple requests on the same TCP connection. Each request increments a counter showing persistence.

### GET /version
Displays HTTP version behavior differences between HTTP/1.0 and HTTP/1.1.

### GET /limit
Shows the configured connection limits (max requests, idle timeout, lifetime).

## Connection Persistence Behavior

### HTTP/1.1 (Default Persistent)
```bash
# First request - connection establishes
curl http://192.168.1.100:8080/status

# Second request on SAME connection (reuses TCP connection)
curl http://192.168.1.100:8080/multiple
# Response: "Request #2 on this connection"

# Force connection close
curl http://192.168.1.100:8080/close
# Connection closes after response
```

### HTTP/1.0 (Default Close)
```bash
# HTTP/1.0 requests close by default
curl --http1.0 http://192.168.1.100:8080/version
# Response shows: "Default Connection Behavior: CLOSE"

# HTTP/1.0 with keep-alive
curl --http1.0 -H "Connection: keep-alive" http://192.168.1.100:8080/multiple
# Connection persists as requested
```

## Configuration

The example uses custom connection persistence settings:

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();

// Connection persistence configuration
config.connection_config.enable_persistence = true;          // Enable persistence
config.connection_config.max_requests_per_conn = 5;          // Max 5 requests per connection
config.connection_config.max_idle_sec = 30;                  // 30 second idle timeout
config.connection_config.max_lifetime_sec = 300;             // 5 minute max lifetime
```

## How to use example

### Hardware Required

* A development board with ESP32/ESP32-S2/ESP32-C3 SoC (e.g., ESP32-DevKitC, ESP-WROVER-KIT, etc.)
* A USB cable for power supply and programming

### Configure the project

```
idf.py menuconfig
```
* Open the project configuration menu (`idf.py menuconfig`) to configure Wi-Fi or Ethernet. See "Establishing Wi-Fi or Ethernet Connection" section in [examples/protocols/README.md](../../README.md) for more details.

### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(Replace PORT with the name of the serial port to use.)

### Test the example

Use curl commands to test different endpoints:

#### Basic Connection Persistence
```bash
# Get connection status
curl -v http://192.168.1.100:8080/status

# Multiple requests on same connection
curl http://192.168.1.100:8080/multiple
curl http://192.168.1.100:8080/multiple
curl http://192.168.1.100:8080/multiple

# Force connection close
curl http://192.168.1.100:8080/close
```

#### HTTP Version Differences
```bash
# HTTP/1.1 default behavior (persistent)
curl --http1.1 http://192.168.1.100:8080/version

# HTTP/1.0 default behavior (close)
curl --http1.0 http://192.168.1.100:8080/version

# HTTP/1.0 with keep-alive
curl --http1.0 -H "Connection: keep-alive" http://192.168.1.100:8080/version
```

#### Connection Limits Testing
```bash
# Check connection limits
curl http://192.168.1.100:8080/limit

# Test request limit (max 5 requests)
for i in {1..6}; do curl -s http://192.168.1.100:8080/multiple | grep "Request"; done
```

#### Advanced Connection Tests
```bash
# Send Connection: close explicitly
curl -H "Connection: close" http://192.168.1.100:8080/status

# Test with different HTTP methods
curl -X GET http://192.168.1.100:8080/status
```

## Expected Output

```
I (3367) connection_persistence_example: === HTTP Server Connection Persistence Example ===
I (3367) connection_persistence_example: This example demonstrates RFC 9112 connection persistence features
I (4067) connection_persistence_example: Starting server with connection persistence on port: 8080
I (4077) connection_persistence_example: Connection config: max_req=5, idle_timeout=30, lifetime=300
I (4087) connection_persistence_example: Registering connection persistence demo handlers
I (4097) connection_persistence_example: Connection persistence handlers registered:
I (4097) connection_persistence_example:   GET /status   - Connection state information
I (4107) connection_persistence_example:   GET /close    - Force connection termination
I (4117) connection_persistence_example:   GET /multiple - Track requests on same connection
I (4127) connection_persistence_example:   GET /version  - HTTP version behavior
I (4137) connection_persistence_example:   GET /limit    - Connection limits info
I (4147) connection_persistence_example: Example started. Use curl or browser to test endpoints:
I (4157) connection_persistence_example:   curl -v http://<IP>:8080/status
I (4167) connection_persistence_example:   curl -v http://<IP>:8080/multiple (try multiple times)
I (4177) connection_persistence_example:   curl -v http://192.168.1.100:8080/close
I (4187) connection_persistence_example:   curl -v 'http://192.168.1.100:8080/version' -H 'Connection: close'
```

## Key Features Demonstrated

1. **RFC 9112 Section 9.3 - Connection Persistence**
   - HTTP/1.1 defaults to persistent connections
   - Proper handling of `Connection: close` headers
   - HTTP/1.0 fallback behavior

2. **Connection State Management**
   - Per-connection request counting
   - Connection lifecycle tracking
   - Real-time state reporting

3. **Configurable Limits**
   - Maximum requests per connection
   - Idle timeout enforcement
   - Connection lifetime limits

4. **HTTP Version Awareness**
   - Different behavior for HTTP/1.0 vs HTTP/1.1
   - Proper header processing per version

This example serves as both a demonstration and testing tool for HTTP connection persistence functionality.
