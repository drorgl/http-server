HTTP Server
===========

:link_to_translation:`zh_CN:[中文]`

Overview
--------

The HTTP Server component provides an ability for running a lightweight web server on {IDF_TARGET_NAME}. Following are detailed steps to use the API exposed by HTTP Server:

    * :cpp:func:`httpd_start`: Creates an instance of HTTP server, allocate memory/resources for it depending upon the specified configuration and outputs a handle to the server instance. The server has both, a listening socket (TCP) for HTTP traffic, and a control socket (UDP) for control signals, which are selected in a round robin fashion in the server task loop. The task priority and stack size are configurable during server instance creation by passing ``httpd_config_t`` structure to ``httpd_start()``. TCP traffic is parsed as HTTP requests and, depending on the requested URI, user registered handlers are invoked which are supposed to send back HTTP response packets.
    * :cpp:func:`httpd_stop`: This stops the server with the provided handle and frees up any associated memory/resources. This is a blocking function that first signals a halt to the server task and then waits for the task to terminate. While stopping, the task closes all open connections, removes registered URI handlers and resets all session context data to empty.
    * :cpp:func:`httpd_register_uri_handler`: A URI handler is registered by passing object of type ``httpd_uri_t`` structure which has members including ``uri`` name, ``method`` type (eg. ``HTTP_GET/HTTP_POST/HTTP_PUT`` etc.), function pointer of type ``esp_err_t *handler (httpd_req_t *req)`` and ``user_ctx`` pointer to user context data.

.. note:: APIs in the HTTP server are not thread-safe. If thread safety is required, it is the responsibility of the application layer to ensure proper synchronization between multiple tasks.

Application Examples
--------------------

- :example:`protocols/http_server/simple` demonstrates how to handle arbitrary content lengths, read request headers and URL query parameters, and set response headers.

- :example:`protocols/http_server/advanced_tests` demonstrates how to use the HTTP server for advanced testing.

Persistent Connections
----------------------

HTTP server features persistent connections, allowing for the reuse of the same connection (session) for several transfers, all the while maintaining context specific data for the session. Context data may be allocated dynamically by the handler in which case a custom function may need to be specified for freeing this data when the connection/session is closed.

Persistent Connections Example
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

    /* Custom function to free context */
    void free_ctx_func(void *ctx)
    {
        /* Could be something other than free */
        free(ctx);
    }

    esp_err_t adder_post_handler(httpd_req_t *req)
    {
        /* Create session's context if not already available */
        if (! req->sess_ctx) {
            req->sess_ctx = malloc(sizeof(ANY_DATA_TYPE));  /*!< Pointer to context data */
            req->free_ctx = free_ctx_func;                  /*!< Function to free context data */
        }

        /* Access context data */
        ANY_DATA_TYPE *ctx_data = (ANY_DATA_TYPE *)req->sess_ctx;

        /* Respond */
        ...............
        ...............
        ...............

        return ESP_OK;
    }


Check the example under :example:`protocols/http_server/persistent_sockets`. This example demonstrates how to set up and use an HTTP server with persistent sockets, allowing for independent sessions or contexts per client.


WebSocket Server
----------------

The HTTP server component provides WebSocket support. The WebSocket feature can be enabled in menuconfig using the :ref:`CONFIG_HTTPD_WS_SUPPORT` option.

:example:`protocols/http_server/ws_echo_server` demonstrates how to create a basic WebSocket echo server using the HTTP server, which starts on a local network and requires a WebSocket client for interaction, echoing back received WebSocket frames.

:example:`protocols/http_server/ws_extensions_server` demonstrates advanced WebSocket functionality including extension negotiation as per RFC 6455 Section 9, showing both standard and extension-enabled endpoints.


WebSocket Handler API Patterns
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

**Critical: Synchronous vs Asynchronous APIs**

WebSocket request handlers execute synchronously and block the connection thread. Extreme care must be taken when selecting transmission APIs:

* **✅ CORRECT**: ``httpd_ws_send_frame(req, &frame)`` - Synchronous completion, safe for handlers
* **❌ INCORRECT**: ``httpd_ws_send_data(handle, sockfd, &frame)`` - Asynchronous, causes deadlocks

Using asynchronous APIs like ``httpd_ws_send_data`` in WebSocket handlers creates circular waits: the handler thread waits for completion callbacks that cannot execute while the handler blocks the connection.

This was the root cause of fragmentation test failures - proper implementation with complex debugging showed WebSocket fragmentation worked correctly, but handler API misuse caused deadlocks that appeared as "broken" functionality.


WebSocket Pre-Handshake Callback
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The HTTP server component provides a pre-handshake callback for WebSocket endpoints. This callback is invoked before the WebSocket handshake is processed—at this point, the connection is still an HTTP connection and has not yet been upgraded to WebSocket.

The pre-handshake callback can be used for authentication, authorization, or other checks. If the callback returns :c:macro:`ESP_OK`, the WebSocket handshake will proceed. If the callback returns any other value, the handshake will be aborted and the connection will be closed.

To use the WebSocket pre-handshake callback, you must enable :ref:`CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT` in your project configuration.

.. code-block:: c

    static esp_err_t ws_auth_handler(httpd_req_t *req)
    {
        // Your authentication logic here
        // return ESP_OK to allow the handshake, or another value to reject.
        return ESP_OK;
    }

    // Registering a WebSocket URI handler with pre-handshake authentication
    static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = handler,           // Your WebSocket data handler
        .user_ctx   = NULL,
        .is_websocket = true,
        .ws_pre_handshake_cb = ws_auth_handler // Set the pre-handshake callback
    };

    // Register the handler after starting the server:
    httpd_register_uri_handler(server, &ws);


WebSocket Extensions
^^^^^^^^^^^^^^^^^^^^

The HTTP server supports WebSocket Extensions as defined in RFC 6455 Section 9, allowing negotiation of protocol extensions that can enhance WebSocket functionality beyond the base specification. Common extensions include compression (permessage-deflate) and custom vendor-specific features.

Extensions are negotiated during the WebSocket handshake. The client sends a ``Sec-WebSocket-Extensions`` header listing offered extensions, and the server responds with a ``Sec-WebSocket-Extensions`` header indicating which extensions were negotiated. Extensions that cannot be negotiated are simply ignored, and the handshake proceeds normally.

To enable WebSocket extensions for a URI handler, set the ``supported_extensions`` field in the ``httpd_uri_t`` structure to a comma-separated list of extension names that the server supports:

.. code-block:: c

    static const httpd_uri_t ws_with_extensions = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true,
        .supported_extensions = "permessage-deflate,compress"  // Server-supported extensions
    };

During handshake:
1. Client sends ``Sec-WebSocket-Extensions: permessage-deflate`` (offers compression)
2. Server finds intersection with ``"permessage-deflate,compress"`` supported list
3. Server responds ``Sec-WebSocket-Extensions: permessage-deflate`` (negotiated extension)
4. WebSocket connection proceeds with negotiated extensions active

The :example:`protocols/http_server/ws_extensions_server` example demonstrates full extension negotiation with both standard and extended WebSocket endpoints.

.. note:: WebSocket extensions are optional and backward compatible. Existing WebSocket handlers without ``supported_extensions`` work unchanged, as the field defaults to NULL (no extensions).


Connection Persistence API
--------------------------

The HTTP Server supports RFC 9112 Section 9.3 connection persistence (keep-alive), allowing multiple HTTP requests to be sent over a single TCP connection. This improves performance by reducing connection overhead and enabling better resource utilization.

Connection persistence is configured through ``httpd_config_t`` during server initialization:

.. code-block:: c

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.connection_config.enable_persistence = true;      // Enable persistent connections (default)
    config.connection_config.max_requests_per_conn = 100;    // Max requests per connection
    config.connection_config.max_idle_sec = 10;              // Idle timeout seconds
    config.connection_config.max_lifetime_sec = 0;           // Unlimited lifetime (0 = no limit)

    httpd_handle_t server = NULL;
    httpd_start(&server, &config);

Connection State Functions
^^^^^^^^^^^^^^^^^^^^^^^^^^

The following functions provide programmatic control over connection persistence state:

.. code-block:: c

    // Check if connection should remain persistent
    bool httpd_connection_should_persist(httpd_handle_t hd, int sockfd);

    // Force connection to close after current response
    esp_err_t httpd_connection_close_after_response(httpd_handle_t hd, int sockfd);

    // Mark connection as WebSocket (persistent by default)
    esp_err_t httpd_connection_mark_websocket(httpd_handle_t hd, int sockfd);

    // Get connection context for inspection
    httpd_connection_ctx_t* httpd_connection_get_ctx(httpd_handle_t hd, int sockfd);

Request Processing Functions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

These functions are used internally by the server but can be useful for custom request processing:

.. code-block:: c

    // Process Connection header and update persistence state
    esp_err_t httpd_connection_process_headers(httpd_handle_t hd, int sockfd,
                                             const char* http_version,
                                             const char* connection_header);

    // Initialize connection context for new connection
    esp_err_t httpd_connection_init(httpd_handle_t hd, int sockfd);

    // Update activity timestamp for connection
    esp_err_t httpd_connection_update_timestamp(httpd_handle_t hd, int sockfd);

    // Clean up connection context when connection closes
    esp_err_t httpd_connection_cleanup(httpd_handle_t hd, int sockfd);

Connection persistence behavior follows RFC 9112 rules:

- **HTTP/1.1**: Connections remain persistent unless ``Connection: close`` is sent
- **HTTP/1.0**: Connections close by default unless ``Connection: keep-alive`` is sent
- **WebSocket**: Connections remain persistent regardless of timeout limits
- **Timeouts**: Connections close if idle longer than ``max_idle_sec`` or older than ``max_lifetime_sec``
- **Request Limits**: Connections close after ``max_requests_per_conn`` requests (0 = unlimited)

Example Usage
^^^^^^^^^^^^^

.. code-block:: c

    esp_err_t my_handler(httpd_req_t *req)
    {
        // Check connection persistence state
        if (httpd_connection_should_persist(req->handle, httpd_req_to_sockfd(req))) {
            ESP_LOGI(TAG, "Connection will persist after this response");
        }

        // Force connection closure if needed (e.g., after error)
        httpd_connection_close_after_response(req->handle, httpd_req_to_sockfd(req));

        httpd_resp_send(req, "Hello", 5);
        return ESP_OK;
    }


Event Handling
--------------

ESP HTTP server has various events for which a handler can be triggered by :doc:`the Event Loop library <../system/esp_event>` when the particular event occurs. The handler has to be registered using :cpp:func:`esp_event_handler_register`. This helps in event handling for ESP HTTP server.

:cpp:enum:`http_server_event_id_t` has all the events which can happen for ESP HTTP server.

Expected data type for different ESP HTTP server events in event loop:

    - HTTP_SERVER_EVENT_ERROR           :   ``httpd_err_code_t``
    - HTTP_SERVER_EVENT_START           :   ``NULL``
    - HTTP_SERVER_EVENT_ON_CONNECTED    :   ``int``
    - HTTP_SERVER_EVENT_ON_HEADER       :   ``int``
    - HTTP_SERVER_EVENT_HEADERS_SENT    :   ``int``
    - HTTP_SERVER_EVENT_ON_DATA         :   ``http_server_event_data``
    - HTTP_SERVER_EVENT_SENT_DATA       :   ``http_server_event_data``
    - HTTP_SERVER_EVENT_DISCONNECTED    :   ``int``
    - HTTP_SERVER_EVENT_STOP            :   ``NULL``

File Serving
------------

:example:`protocols/http_server/file_serving` demonstrates how to create a simple HTTP file server, with both upload and download capabilities.

Captive Portal
--------------

:example:`protocols/http_server/captive_portal` demonstrates two methods of creating a captive portal, which directs users to an authentication page before browsing, using either DNS queries and HTTP requests redirection or a modern method involving a field in the DHCP offer.

Asynchronous Handlers
---------------------

:example:`protocols/http_server/async_handlers` demonstrates how to handle multiple long-running simultaneous requests within the HTTP server, using different URIs for asynchronous requests, quick requests, and the index page.

RESTful API
-----------

:example:`protocols/http_server/restful_server` demonstrates how to implement a RESTful API server and web server, with a modern frontend UI, and designs several APIs to fetch resources, using mDNS to parse the domain name, and deploying the webpage to SPI flash.

URI Handlers
------------

The HTTP server allows you to register URI handlers to handle different HTTP requests. Each URI handler is associated with a specific URI and HTTP method (GET, POST, etc.). The handler function is called whenever a request matching the URI and method is received.

The handler function should return an :cpp:type:`esp_err_t` value.

.. code-block:: c

    esp_err_t my_uri_handler(httpd_req_t *req)
    {
        // Handle the request
        // ...

        // Return ESP_OK if the request was handled successfully
        return ESP_OK;

        // Return an error code to close the connection
        // return ESP_FAIL;
    }

    void register_uri_handlers(httpd_handle_t server)
    {
        httpd_uri_t my_uri = {
            .uri       = "/my_uri",
            .method    = HTTP_GET,
            .handler   = my_uri_handler,
            .user_ctx  = NULL
        };

        httpd_register_uri_handler(server, &my_uri);
    }

In this example, the `my_uri_handler` function handles requests to the `/my_uri` URI. If the handler returns :c:macro:`ESP_OK`, the connection remains open. If it returns any other value, the connection is closed. This behavior allows the application to manage connection closure based on specific events or conditions.

API Reference
-------------

.. include-build-file:: inc/http_server.inc
