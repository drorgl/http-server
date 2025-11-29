/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef http_parser_h
#define http_parser_h
#ifdef __cplusplus
extern "C" {
#endif

/* Also update SONAME in the Makefile whenever you change these. */
#define HTTP_PARSER_VERSION_MAJOR 2
#define HTTP_PARSER_VERSION_MINOR 9
#define HTTP_PARSER_VERSION_PATCH 4

#include <stddef.h>
#if defined(_WIN32) && !defined(__MINGW32__) && \
  (!defined(_MSC_VER) || _MSC_VER<1600) && !defined(__WINE__)
#include <BaseTsd.h>
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif

/* Compile with -DHTTP_PARSER_STRICT=0 to make less checks, but run
 * faster
 */
#ifndef HTTP_PARSER_STRICT
# define HTTP_PARSER_STRICT 1
#endif

/* Maximium header size allowed. If the macro is not defined
 * before including this header then the default is used. To
 * change the maximum header size, define the macro in the build
 * environment (e.g. -DHTTP_MAX_HEADER_SIZE=<value>). To remove
 * the effective limit on the size of the header, define the macro
 * to a very large number (e.g. -DHTTP_MAX_HEADER_SIZE=0x7fffffff)
 */
#ifndef HTTP_MAX_HEADER_SIZE
# define HTTP_MAX_HEADER_SIZE (80*1024)
#endif

typedef struct http_parser http_parser;
typedef struct http_parser_settings http_parser_settings;


/* Callbacks should return non-zero to indicate an error. The parser will
 * then halt execution.
 *
 * The one exception is on_headers_complete. In a HTTP_RESPONSE parser
 * returning '1' from on_headers_complete will tell the parser that it
 * should not expect a body. This is used when receiving a response to a
 * HEAD request which may contain 'Content-Length' or 'Transfer-Encoding:
 * chunked' headers that indicate the presence of a body.
 *
 * Returning `2` from on_headers_complete will tell parser that it should not
 * expect neither a body nor any further responses on this connection. This is
 * useful for handling responses to a CONNECT request which may not contain
 * `Upgrade` or `Connection: upgrade` headers.
 *
 * http_data_cb does not return data chunks. It will be called arbitrarily
 * many times for each string. E.G. you might get 10 callbacks for "on_url"
 * each providing just a few characters more data.
 */
typedef int (*http_data_cb) (http_parser*, const char *at, size_t length);
typedef int (*http_cb) (http_parser*);


/**
 * HTTP status codes macro
 *
 * This macro defines all standard HTTP response status codes as defined
 * in RFC 7231 and related specifications. It takes a macro function as
 * argument that will be called for each status code with three arguments:
 * the numeric code, the symbolic name, and the text description.
 *
 * The macro is used to generate:
 * - Status code constants (HTTP_STATUS_*)
 * - Status code enumeration (enum http_status)
 *
 * @param XX A macro function that takes 3 arguments: (code, name, description)
 *
 * @note Status codes are grouped by hundreds:
 *       - 1xx: Informational responses
 *       - 2xx: Successful responses
 *       - 3xx: Redirection messages
 *       - 4xx: Client error responses
 *       - 5xx: Server error responses
 *
 * @see http_status, HTTP_STATUS_*, http_status_str()
 */
#define HTTP_STATUS_MAP(XX)                                                 \
  XX(100, CONTINUE,                        Continue)                        \
  XX(101, SWITCHING_PROTOCOLS,             Switching Protocols)             \
  XX(102, PROCESSING,                      Processing)                      \
  XX(200, OK,                              OK)                              \
  XX(201, CREATED,                         Created)                         \
  XX(202, ACCEPTED,                        Accepted)                        \
  XX(203, NON_AUTHORITATIVE_INFORMATION,   Non-Authoritative Information)   \
  XX(204, NO_CONTENT,                      No Content)                      \
  XX(205, RESET_CONTENT,                   Reset Content)                   \
  XX(206, PARTIAL_CONTENT,                 Partial Content)                 \
  XX(207, MULTI_STATUS,                    Multi-Status)                    \
  XX(208, ALREADY_REPORTED,                Already Reported)                \
  XX(226, IM_USED,                         IM Used)                         \
  XX(300, MULTIPLE_CHOICES,                Multiple Choices)                \
  XX(301, MOVED_PERMANENTLY,               Moved Permanently)               \
  XX(302, FOUND,                           Found)                           \
  XX(303, SEE_OTHER,                       See Other)                       \
  XX(304, NOT_MODIFIED,                    Not Modified)                    \
  XX(305, USE_PROXY,                       Use Proxy)                       \
  XX(307, TEMPORARY_REDIRECT,              Temporary Redirect)              \
  XX(308, PERMANENT_REDIRECT,              Permanent Redirect)              \
  XX(400, BAD_REQUEST,                     Bad Request)                     \
  XX(401, UNAUTHORIZED,                    Unauthorized)                    \
  XX(402, PAYMENT_REQUIRED,                Payment Required)                \
  XX(403, FORBIDDEN,                       Forbidden)                       \
  XX(404, NOT_FOUND,                       Not Found)                       \
  XX(405, METHOD_NOT_ALLOWED,              Method Not Allowed)              \
  XX(406, NOT_ACCEPTABLE,                  Not Acceptable)                  \
  XX(407, PROXY_AUTHENTICATION_REQUIRED,   Proxy Authentication Required)   \
  XX(408, REQUEST_TIMEOUT,                 Request Timeout)                 \
  XX(409, CONFLICT,                        Conflict)                        \
  XX(410, GONE,                            Gone)                            \
  XX(411, LENGTH_REQUIRED,                 Length Required)                 \
  XX(412, PRECONDITION_FAILED,             Precondition Failed)             \
  XX(413, PAYLOAD_TOO_LARGE,               Payload Too Large)               \
  XX(414, URI_TOO_LONG,                    URI Too Long)                    \
  XX(415, UNSUPPORTED_MEDIA_TYPE,          Unsupported Media Type)          \
  XX(416, RANGE_NOT_SATISFIABLE,           Range Not Satisfiable)           \
  XX(417, EXPECTATION_FAILED,              Expectation Failed)              \
  XX(421, MISDIRECTED_REQUEST,             Misdirected Request)             \
  XX(422, UNPROCESSABLE_ENTITY,            Unprocessable Entity)            \
  XX(423, LOCKED,                          Locked)                          \
  XX(424, FAILED_DEPENDENCY,               Failed Dependency)               \
  XX(426, UPGRADE_REQUIRED,                Upgrade Required)                \
  XX(428, PRECONDITION_REQUIRED,           Precondition Required)           \
  XX(429, TOO_MANY_REQUESTS,               Too Many Requests)               \
  XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, Request Header Fields Too Large) \
  XX(451, UNAVAILABLE_FOR_LEGAL_REASONS,   Unavailable For Legal Reasons)   \
  XX(500, INTERNAL_SERVER_ERROR,           Internal Server Error)           \
  XX(501, NOT_IMPLEMENTED,                 Not Implemented)                 \
  XX(502, BAD_GATEWAY,                     Bad Gateway)                     \
  XX(503, SERVICE_UNAVAILABLE,             Service Unavailable)             \
  XX(504, GATEWAY_TIMEOUT,                 Gateway Timeout)                 \
  XX(505, HTTP_VERSION_NOT_SUPPORTED,      HTTP Version Not Supported)      \
  XX(506, VARIANT_ALSO_NEGOTIATES,         Variant Also Negotiates)         \
  XX(507, INSUFFICIENT_STORAGE,            Insufficient Storage)            \
  XX(508, LOOP_DETECTED,                   Loop Detected)                   \
  XX(510, NOT_EXTENDED,                    Not Extended)                    \
  XX(511, NETWORK_AUTHENTICATION_REQUIRED, Network Authentication Required) \

/**
 * HTTP response status codes enumeration
 *
 * This enumeration defines the standard HTTP response status codes as defined
 * in RFC 7231 and related specifications. Each status code has an associated
 * symbolic name and numeric value.
 *
 * @note The actual status code text (e.g., "OK" for 200) can be obtained
 *       using http_status_str()
 *
 * @note Status codes are grouped as follows:
 *       - 1xx: Informational responses
 *       - 2xx: Successful responses
 *       - 3xx: Redirection messages
 *       - 4xx: Client error responses
 *       - 5xx: Server error responses
 *
 * @see http_status_str(), HTTP_STATUS_MAP
 */
enum http_status
  {
#define XX(num, name, string) HTTP_STATUS_##name = num,
  HTTP_STATUS_MAP(XX)
#undef XX
  };


/**
 * HTTP request method enumeration
 *
 * This enumeration defines the standard HTTP request methods as defined
 * in RFC 7231 and various extensions. Each method has an associated
 * symbolic name and numeric value.
 *
 * @note The string representation of a method can be obtained using
 *       http_method_str()
 *
 * @note Method categories:
 *       - Basic methods: GET, POST, PUT, DELETE, HEAD, OPTIONS, TRACE
 *       - Extended methods: PATCH (RFC 5789)
 *       - WebDAV methods: PROPFIND, PROPPATCH, MKCOL, MOVE, COPY, LOCK, UNLOCK
 *       - Subversion: REPORT, MKACTIVITY, CHECKOUT, MERGE
 *       - UPnP: M-SEARCH, NOTIFY, SUBSCRIBE, UNSUBSCRIBE
 *       - CalDAV: MKCALENDAR
 *       - Others: CONNECT (proxy), BIND, REBIND, UNBIND, ACL, LINK, UNLINK, SOURCE
 *
 * @note Only some methods are standardized in the core HTTP specification.
 *       Many are from WebDAV, CalDAV, and other extensions.
 *
 * @see HTTP_METHOD_MAP, http_method_str()
 */
#define HTTP_METHOD_MAP(XX)         \
  XX(0,  DELETE,      DELETE)       \
  XX(1,  GET,         GET)          \
  XX(2,  HEAD,        HEAD)         \
  XX(3,  POST,        POST)         \
  XX(4,  PUT,         PUT)          \
  /* pathological */                \
  XX(5,  CONNECT,     CONNECT)      \
  XX(6,  OPTIONS,     OPTIONS)      \
  XX(7,  TRACE,       TRACE)        \
  /* WebDAV */                      \
  XX(8,  COPY,        COPY)         \
  XX(9,  LOCK,        LOCK)         \
  XX(10, MKCOL,       MKCOL)        \
  XX(11, MOVE,        MOVE)         \
  XX(12, PROPFIND,    PROPFIND)     \
  XX(13, PROPPATCH,   PROPPATCH)    \
  XX(14, SEARCH,      SEARCH)       \
  XX(15, UNLOCK,      UNLOCK)       \
  XX(16, BIND,        BIND)         \
  XX(17, REBIND,      REBIND)       \
  XX(18, UNBIND,      UNBIND)       \
  XX(19, ACL,         ACL)          \
  /* subversion */                  \
  XX(20, REPORT,      REPORT)       \
  XX(21, MKACTIVITY,  MKACTIVITY)   \
  XX(22, CHECKOUT,    CHECKOUT)     \
  XX(23, MERGE,       MERGE)        \
  /* upnp */                        \
  XX(24, MSEARCH,     M-SEARCH)     \
  XX(25, NOTIFY,      NOTIFY)       \
  XX(26, SUBSCRIBE,   SUBSCRIBE)    \
  XX(27, UNSUBSCRIBE, UNSUBSCRIBE)  \
  /* RFC-5789 */                    \
  XX(28, PATCH,       PATCH)        \
  XX(29, PURGE,       PURGE)        \
  /* CalDAV */                      \
  XX(30, MKCALENDAR,  MKCALENDAR)   \
  /* RFC-2068, section 19.6.1.2 */  \
  XX(31, LINK,        LINK)         \
  XX(32, UNLINK,      UNLINK)       \
  /* icecast */                     \
  XX(33, SOURCE,      SOURCE)       \

/**
 * HTTP request method enumeration
 *
 * See HTTP_METHOD_MAP for the complete list of supported methods.
 *
 * @see HTTP_METHOD_MAP, http_method_str()
 */
enum http_method
  {
#define XX(num, name, string) HTTP_##name = num,
  HTTP_METHOD_MAP(XX)
#undef XX
  };


/**
 * Parser type enumeration
 *
 * This enumeration defines the different types of HTTP messages that
 * the parser can handle.
 *
 * @note The parser type is set during initialization via http_parser_init()
 *       and determines what type of messages the parser expects to process.
 *
 * @see http_parser_init(), http_parser
 */
enum http_parser_type {
  HTTP_REQUEST,  /**< Parser expects HTTP request messages (method, path, version) */
  HTTP_RESPONSE, /**< Parser expects HTTP response messages (version, status, reason) */
  HTTP_BOTH      /**< Parser can handle both requests and responses (auto-detection) */
};


/**
 * HTTP message flag values
 *
 * This enumeration defines the flag values that can be set in the
 * http_parser.flags field. These flags indicate various characteristics
 * of the parsed HTTP message.
 *
 * @note These flags are automatically set by the parser based on the
 *       message headers and characteristics. They should be treated
 *       as read-only by user code.
 *
 * @note The flags field is a bitmask, so multiple flags can be set
 *       simultaneously.
 *
 * @see http_parser.flags
 */
enum flags
  {
    F_CHUNKED               = 1 << 0,  /**< Message uses chunked transfer encoding */
    F_CONNECTION_KEEP_ALIVE = 1 << 1,  /**< Connection: keep-alive header present */
    F_CONNECTION_CLOSE      = 1 << 2,  /**< Connection: close header present */
    F_CONNECTION_UPGRADE    = 1 << 3,  /**< Connection: upgrade header present */
    F_TRAILING              = 1 << 4,  /**< Message has trailing headers (HTTP/2) */
    F_UPGRADE               = 1 << 5,  /**< Upgrade header present in response */
    F_SKIPBODY              = 1 << 6,  /**< Message body should be skipped */
    F_CONTENTLENGTH         = 1 << 7,  /**< Content-Length header present */
    F_TRANSFER_ENCODING     = 1 << 8   /**< Transfer-Encoding header present (never set in http_parser.flags) */
  };


/**
 * HTTP parser error codes macro
 *
 * This macro defines all possible error codes that can be encountered
 * during HTTP message parsing. It takes a macro function as argument
 * that will be called for each error code with two arguments: the
 * error name and the error description string.
 *
 * The macro is used to generate:
 * - Error code constants (HPE_*)
 * - Error code enumeration (enum http_errno)
 * - Error name strings (via HTTP_ERRNO_GEN)
 *
 * @param XX A macro function that takes 2 arguments: (name, description)
 *
 * @note Error codes are grouped into:
 *       - Success: HPE_OK
 *       - Callback errors: HPE_CB_*
 *       - Parsing errors: HPE_INVALID_*
 *
 * @see http_errno, HTTP_ERRNO_GEN
 */
#define HTTP_ERRNO_MAP(XX)                                           \
  /* No error */                                                     \
  XX(OK, "success")                                                  \
                                                                     \
  /* Callback-related errors */                                      \
  XX(CB_message_begin, "the on_message_begin callback failed")       \
  XX(CB_url, "the on_url callback failed")                           \
  XX(CB_header_field, "the on_header_field callback failed")         \
  XX(CB_header_value, "the on_header_value callback failed")         \
  XX(CB_headers_complete, "the on_headers_complete callback failed") \
  XX(CB_body, "the on_body callback failed")                         \
  XX(CB_message_complete, "the on_message_complete callback failed") \
  XX(CB_status, "the on_status callback failed")                     \
  XX(CB_chunk_header, "the on_chunk_header callback failed")         \
  XX(CB_chunk_complete, "the on_chunk_complete callback failed")     \
                                                                     \
  /* Parsing-related errors */                                       \
  XX(INVALID_EOF_STATE, "stream ended at an unexpected time")        \
  XX(HEADER_OVERFLOW,                                                \
     "too many header bytes seen; overflow detected")                \
  XX(CLOSED_CONNECTION,                                              \
     "data received after completed connection: close message")      \
  XX(INVALID_VERSION, "invalid HTTP version")                        \
  XX(INVALID_STATUS, "invalid HTTP status code")                     \
  XX(INVALID_METHOD, "invalid HTTP method")                          \
  XX(INVALID_URL, "invalid URL")                                     \
  XX(INVALID_HOST, "invalid host")                                   \
  XX(INVALID_PORT, "invalid port")                                   \
  XX(INVALID_PATH, "invalid path")                                   \
  XX(INVALID_QUERY_STRING, "invalid query string")                   \
  XX(INVALID_FRAGMENT, "invalid fragment")                           \
  XX(LF_EXPECTED, "LF character expected")                           \
  XX(INVALID_HEADER_TOKEN, "invalid character in header")            \
  XX(INVALID_CONTENT_LENGTH,                                         \
     "invalid character in content-length header")                   \
  XX(UNEXPECTED_CONTENT_LENGTH,                                      \
     "unexpected content-length header")                             \
  XX(INVALID_CHUNK_SIZE,                                             \
     "invalid character in chunk size header")                       \
  XX(INVALID_CONSTANT, "invalid constant string")                    \
  XX(INVALID_INTERNAL_STATE, "encountered unexpected internal state")\
  XX(STRICT, "strict mode assertion failed")                         \
  XX(PAUSED, "parser is paused")                                     \
  XX(UNKNOWN, "an unknown error occurred")                           \
  XX(INVALID_TRANSFER_ENCODING,                                      \
     "request has invalid transfer-encoding")                        \


/**
 * HTTP parser error codes enumeration
 *
 * This enumeration defines all possible error codes that can be encountered
 * during HTTP message parsing. Each error code is prefixed with HPE_ (HTTP
 * Parser Error) to avoid namespace conflicts.
 *
 * @note Error codes can be retrieved using HTTP_PARSER_ERRNO() macro
 * @note Human-readable error descriptions are available via http_errno_description()
 * @note Error code names are available via http_errno_name()
 *
 * @see HTTP_PARSER_ERRNO(), http_errno_description(), http_errno_name(), HTTP_ERRNO_MAP
 */
#define HTTP_ERRNO_GEN(n, s) HPE_##n,
enum http_errno {
  HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
};
#undef HTTP_ERRNO_GEN


/**
 * Extract error code from parser
 *
 * This macro extracts the current error code from an http_parser object.
 * The error code indicates the last error encountered during parsing.
 *
 * @param p Pointer to an http_parser object
 * @return enum http_errno value representing the error code
 *
 * @note When http_parser_execute() returns an error, this macro can be
 *       used to get the specific error that occurred.
 *
 * @see http_parser_execute(), http_errno, http_errno_description()
 */
#define HTTP_PARSER_ERRNO(p)            ((enum http_errno) (p)->http_errno)


/**
 * HTTP message parser state structure
 *
 * This structure contains the parsing state for an HTTP message. One http_parser
 * object should be used per TCP connection or message stream. The structure
 * maintains both private internal state and public information about the
 * parsed message.
 *
 * @note The parser maintains state between calls to http_parser_execute().
 *       For persistent connections, reuse the same parser object for multiple
 *       messages by calling http_parser_init() before each new message.
 *
 * @note For thread-safe usage, ensure proper synchronization when sharing
 *       parsers between threads. The data field can be used to associate
 *       thread-local context.
 *
 * @see http_parser_init(), http_parser_execute(), http_parser_settings
 */
struct http_parser {
  /* ==================== PRIVATE FIELDS ==================== */
  /** @name Internal Parser State
   * @{
   * These fields are used internally by the parser and should not be
   * modified directly by user code.
   */
  
  unsigned int type : 2;         /**< Parser type: HTTP_REQUEST, HTTP_RESPONSE, or HTTP_BOTH */
  unsigned int flags : 8;        /**< Bitmask of F_* flags indicating parsed message characteristics */
  unsigned int state : 7;        /**< Current parsing state machine state */
  unsigned int header_state : 7; /**< Current header parsing state */
  unsigned int index : 5;        /**< Index into current token matching pattern */
  unsigned int extra_flags : 2;  /**< Additional parsing flags */
  unsigned int lenient_http_headers : 1; /**< Enable lenient header parsing */

  uint32_t nread;                /**< Total number of bytes read since parser initialization */
  uint64_t content_length;       /**< Content-Length header value (0 if not present) */
  /** @} */

  /* ==================== READ-ONLY FIELDS ==================== */
  /** @name Parsed Message Information
   * @{
   * These fields are populated by the parser and contain information
   * about the HTTP message being parsed. They are read-only and should
   * not be modified by user callbacks.
   */
  
  unsigned short http_major;     /**< HTTP major version (e.g., 1 for HTTP/1.1) */
  unsigned short http_minor;     /**< HTTP minor version (e.g., 1 for HTTP/1.1) */
  unsigned int status_code : 16; /**< HTTP response status code (responses only) */
  unsigned int method : 8;       /**< HTTP request method enum value (requests only) */
  unsigned int http_errno : 7;   /**< Last error code encountered during parsing */

  /**
   * Upgrade protocol indicator
   *
   * Set to 1 if an "Upgrade" header was present and the parser has exited
   * due to protocol upgrade (e.g., WebSocket). Set to 0 if no upgrade header
   * was present.
   *
   * @note Always check this field when http_parser_execute() returns, in
   *       addition to checking for parsing errors. Non-HTTP protocol data
   *       begins at the buffer position returned by http_parser_execute().
   *
   * @see http_parser_execute(), http_should_keep_alive()
   */
  unsigned int upgrade : 1;

  /** @} */

  /* ==================== PUBLIC FIELDS ==================== */
  /** @name User Data
   * @{
   * This field is available for user applications to store application-specific
   * context. The parser does not modify or interpret this pointer.
   */
  
  void *data;                    /**< User-defined pointer for application context (connection, socket, etc.) */
  
  /** @} */
};


/**
 * HTTP parser callback configuration structure
 *
 * This structure defines the callback functions that will be invoked during
 * HTTP message parsing. Set the callback function pointers to NULL for
 * callbacks you don't need. The parser will skip calling unset callbacks.
 *
 * @note All callback functions must return 0 on success. Returning a non-zero
 *       value indicates an error and will cause the parser to halt immediately.
 *
 * @note Data callbacks (http_data_cb) may be called multiple times for the
 *       same logical data element if the data arrives in chunks. You must
 *       buffer the data across multiple calls if you need the complete value.
 *
 * @note The callback functions are invoked in a specific sequence during
 *       parsing. See the individual callback documentation for details.
 *
 * @note For thread-safe usage, ensure proper synchronization in your callback
 *       functions when accessing shared data.
 *
 * @see http_cb, http_data_cb, http_parser_execute(), http_parser_init()
 */
struct http_parser_settings {
  /** @name Message Lifecycle Callbacks
   * @{
   * These callbacks track the overall parsing progress of an HTTP message.
   */
  
  /**
   * Called at the beginning of a new HTTP message
   *
   * This callback is invoked when the parser starts processing a new HTTP
   * message (request or response). Use this to initialize message-specific
   * state in your application.
   *
   * @param parser The parser object for this message
   * @return 0 on success, non-zero to abort parsing
   *
   * @see on_message_complete, on_headers_complete
   */
  http_cb      on_message_begin;

  /**
   * Called when the complete HTTP message has been parsed
   *
   * This callback is invoked after all headers and body have been successfully
   * parsed. At this point, the complete HTTP message information is available
   * in the parser object.
   *
   * @param parser The parser object for this message
   * @return 0 on success, non-zero to abort parsing
   *
   * @note For persistent connections, prepare the parser for the next message
   *       by calling http_parser_init() again.
   *
   * @see on_message_begin, on_headers_complete
   */
  http_cb      on_message_complete;
  
  /** @} */

  /** @name Request/Response Specific Callbacks
   * @{
   * These callbacks provide access to request or response specific data.
   */
  
  /**
   * Called for request URL data (requests only)
   *
   * This callback provides the URL from HTTP requests. The URL may be
   * delivered in multiple chunks, so you must buffer the data across calls.
   *
   * @param parser The parser object for this request
   * @param at Pointer to the start of the URL data chunk
   * @param length Length of the data chunk in bytes
   * @return 0 on success, non-zero to abort parsing
   *
   * @note May be called multiple times for a single URL
   * @note The data is only valid for the lifetime of this callback
   * @note Use http_parser_parse_url() to parse the complete URL
   *
   * @see on_status, http_parser_parse_url()
   */
  http_data_cb on_url;

  /**
   * Called for response status text (responses only)
   *
   * This callback provides the status text from HTTP responses (e.g., "OK",
   * "Not Found"). The status text may be delivered in multiple chunks.
   *
   * @param parser The parser object for this response
   * @param at Pointer to the start of the status text chunk
   * @param length Length of the data chunk in bytes
   * @return 0 on success, non-zero to abort parsing
   *
   * @note May be called multiple times for a single status text
   * @note The data is only valid for the lifetime of this callback
   *
   * @see on_url, parser->status_code
   */
  http_data_cb on_status;
  
  /** @} */

  /** @name Header Processing Callbacks
   * @{
   * These callbacks provide access to HTTP headers as they are parsed.
   */
  
  /**
   * Called for header field names
   *
   * This callback provides header field names (e.g., "Content-Type", "Host").
   * Field names may be delivered in multiple chunks and may span across
   * multiple calls.
   *
   * @param parser The parser object for this message
   * @param at Pointer to the start of the header field name chunk
   * @param length Length of the data chunk in bytes
   * @return 0 on success, non-zero to abort parsing
   *
   * @note May be called multiple times for a single field name
   * @note The data is only valid for the lifetime of this callback
   * @note Follow this with on_header_value for the corresponding value
   *
   * @see on_header_value, on_headers_complete
   */
  http_data_cb on_header_field;

  /**
   * Called for header field values
   *
   * This callback provides the values for header fields. Each call to
   * on_header_field should be followed by one or more calls to on_header_value.
   *
   * @param parser The parser object for this message
   * @param at Pointer to the start of the header value chunk
   * @param length Length of the data chunk in bytes
   * @return 0 on success, non-zero to abort parsing
   *
   * @note May be called multiple times for a single field value
   * @note The data is only valid for the lifetime of this callback
   *
   * @see on_header_field, on_headers_complete
   */
  http_data_cb on_header_value;

  /**
   * Called after all headers have been parsed
   *
   * This callback is invoked after the complete header section has been
   * processed. At this point, you have access to all headers and can make
   * decisions about message processing.
   *
   * Special return values for HTTP responses:
   * - Return 1: Skip the message body (useful for HEAD responses)
   * - Return 2: Skip body and signal connection close (useful for CONNECT responses)
   *
   * @param parser The parser object for this message
   * @return 0 to continue normally, 1 to skip body, 2 to skip body and close
   *
   * @note This is the last chance to examine headers before body processing
   * @note Check http_should_keep_alive() to determine connection handling
   *
   * @see on_message_begin, on_message_complete, http_should_keep_alive()
   */
  http_cb      on_headers_complete;
  
  /** @} */

  /** @name Message Body Callbacks
   * @{
   * These callbacks provide access to the HTTP message body.
   */
  
  /**
   * Called for message body data
   *
   * This callback provides the HTTP message body content. The body may be
   * delivered in multiple chunks, especially for large bodies or chunked
   * transfer encoding.
   *
   * @param parser The parser object for this message
   * @param at Pointer to the start of the body data chunk
   * @param length Length of the data chunk in bytes
   * @return 0 on success, non-zero to abort parsing
   *
   * @note May be called multiple times for a single message body
   * @note The data is only valid for the lifetime of this callback
   * @note Chunked transfer encoding is automatically decoded
   *
   * @see on_headers_complete, on_chunk_header
   */
  http_data_cb on_body;
  
  /** @} */

  /** @name Chunked Transfer Encoding Callbacks
   * @{
   * These callbacks are only invoked when using chunked transfer encoding.
   */
  
  /**
   * Called when a chunk header is parsed
   *
   * This callback is invoked for each chunk in a chunked transfer encoding.
   * The current chunk size is stored in parser->content_length.
   *
   * @param parser The parser object for this message
   * @return 0 on success, non-zero to abort parsing
   *
   * @note Only called for chunked transfer encoding
   * @note parser->content_length contains the chunk size
   * @note Follow with on_body for the chunk data
   *
   * @see on_chunk_complete, on_body, parser->content_length
   */
  http_cb      on_chunk_header;

  /**
   * Called when a chunk is completely parsed
   *
   * This callback is invoked after a complete chunk has been processed.
   * Use this to track chunk boundaries and perform chunk-specific processing.
   *
   * @param parser The parser object for this message
   * @return 0 on success, non-zero to abort parsing
   *
   * @note Only called for chunked transfer encoding
   * @note Follows on_body for the completed chunk
   *
   * @see on_chunk_header, on_body
   */
  http_cb      on_chunk_complete;
  
  /** @} */
};


/**
 * URL field enumeration
 *
 * This enumeration defines the different components that can be parsed
 * from an HTTP URL. Each field corresponds to a component of a URL as
 * defined in RFC 3986.
 *
 * @note The UF_SCHEMA field contains the URL scheme (e.g., "http", "https")
 * @note The UF_PORT field is special - its value is parsed and stored
 *       directly in the port field, not in field_data
 * @note The UF_USERINFO field contains username and password if present
 *
 * @see http_parser_parse_url(), http_parser_url
 */
enum http_parser_url_fields
  {
    UF_SCHEMA           = 0,    /**< URL scheme (e.g., "http", "https") */
    UF_HOST             = 1,    /**< Host component of the URL */
    UF_PORT             = 2,    /**< Port number (parsed to uint16_t) */
    UF_PATH             = 3,    /**< Path component of the URL */
    UF_QUERY            = 4,    /**< Query string (after ?) */
    UF_FRAGMENT         = 5,    /**< Fragment identifier (after #) */
    UF_USERINFO         = 6,    /**< User information (username:password@) */
    UF_MAX              = 7     /**< Number of URL field types */
  };


/**
 * Result structure for URL parsing
 *
 * This structure contains the result of parsing a URL using
 * http_parser_parse_url(). It stores the parsed URL components as
 * offsets and lengths into the original URL string buffer.
 *
 * @note Callers should check field_set to determine which fields
 *       were found in the URL before accessing field_data[]
 * @note The port field is automatically converted from string to uint16_t
 * @note The URL parser does NOT perform percent-encoding decoding
 * @note All offset values are relative to the input buffer
 *
 * @see http_parser_parse_url(), http_parser_url_init(), UF_*
 */
struct http_parser_url {
  uint16_t field_set;           /**< Bitmask indicating which fields were found (1 << UF_*) */
  uint16_t port;                /**< Port number parsed from UF_PORT field (0 if not present) */

  /**< URL component data array */
  struct {
    uint16_t off;               /**< Offset into the input buffer where this field starts */
    uint16_t len;               /**< Length of the field in the input buffer */
  } field_data[UF_MAX];
};


/**
 * Get the HTTP parser library version
 *
 * Returns the version number of the http-parser library encoded as an
 * unsigned long integer. The version is encoded in the format:
 * - Bits 16-23: Major version number
 * - Bits 8-15:  Minor version number
 * - Bits 0-7:   Patch level
 *
 * @return unsigned long containing the version number
 *
 * @note The version numbers are defined by HTTP_PARSER_VERSION_MAJOR,
 *       HTTP_PARSER_VERSION_MINOR, and HTTP_PARSER_VERSION_PATCH macros
 *
 * @note This function is thread-safe and can be called at any time
 *
 * @note Use bit shifting to extract individual components as shown in
 *       the example below
 *
 * @par Example:
 * @code
 * unsigned long version = http_parser_version();
 * unsigned major = (version >> 16) & 255;
 * unsigned minor = (version >> 8) & 255;
 * unsigned patch = version & 255;
 * printf("http_parser v%u.%u.%u\n", major, minor, patch);
 * @endcode
 *
 * @see HTTP_PARSER_VERSION_MAJOR, HTTP_PARSER_VERSION_MINOR, HTTP_PARSER_VERSION_PATCH
 */
unsigned long http_parser_version(void);

/**
 * Initialize HTTP parser state
 *
 * Initializes all fields of the http_parser structure to zero, except
 * for the data field which is preserved. Sets the initial parsing state
 * based on the specified parser type.
 *
 * @param parser Pointer to the http_parser structure to initialize
 * @param type The type of parser (HTTP_REQUEST, HTTP_RESPONSE, or HTTP_BOTH)
 *
 * @note The parser->data field is NOT modified by this function
 * @note This function should be called before processing each new HTTP message
 * @note For persistent connections, call this between messages
 *
 * @see http_parser, http_parser_type, http_parser_execute()
 */
void http_parser_init(http_parser *parser, enum http_parser_type type);


/**
 * Initialize parser settings structure
 *
 * Initializes all callback members of the http_parser_settings structure
 * to 0 (NULL). This effectively disables all callbacks until they are
 * explicitly set by the application.
 *
 * @param settings Pointer to the http_parser_settings structure to initialize
 *
 * @note This function sets all callback function pointers to NULL
 * @note Call this before setting individual callback function pointers
 * @note This function does not modify the underlying memory, only resets pointers
 *
 * @see http_parser_settings
 */
void http_parser_settings_init(http_parser_settings *settings);


/**
 * Execute HTTP parser on data buffer
 *
 * Executes the HTTP parser over the given data buffer. This is the main
 * parsing function that processes HTTP message data and invokes the
 * appropriate callbacks based on the message content.
 *
 * @param parser The parser object to use for parsing
 * @param settings The callback configuration structure
 * @param data Pointer to the data buffer to parse
 * @param len Length of the data buffer in bytes
 *
 * @return size_t Number of bytes successfully processed
 *
 * @note On success, returns the number of bytes consumed from the buffer
 * @note On error, sets parser->http_errno and returns the position where parsing stopped
 * @note On upgrade detection, stops after headers and returns bytes processed
 * @note Can be called multiple times with data fragments
 *
 * @note Check parser->upgrade after this function returns to detect protocol upgrades
 * @note If return value < len, check parser->http_errno for error details
 *
 * @par Example:
 * @code
 * size_t nparsed = http_parser_execute(parser, &settings, buf, recved);
 *
 * if (parser->upgrade) {
 *   // Handle protocol upgrade (e.g., WebSocket)
 * } else if (nparsed != recved) {
 *   // Handle parse error
 *   printf("Parse error: %s\n", http_errno_description(HTTP_PARSER_ERRNO(parser)));
 * }
 * @endcode
 *
 * @see http_parser_execute(), http_parser_settings, HTTP_PARSER_ERRNO()
 */
size_t http_parser_execute(http_parser *parser,
                           const http_parser_settings *settings,
                           const char *data,
                           size_t len);


/**
 * Check if connection should be kept alive
 *
 * Determines whether the HTTP connection should be kept alive based on
 * the HTTP version, Connection header value, and message characteristics.
 * This function helps implement proper connection management for both
 * HTTP/1.0 and HTTP/1.1 connections.
 *
 * @param parser The parser object containing the parsed message information
 *
 * @return int Non-zero if connection should be kept alive, 0 if it should be closed
 *
 * @note Returns 0 for HTTP/1.0 without Connection: keep-alive
 * @note Returns 0 for 1xx, 204, and 304 responses
 * @note Returns 0 for responses to HEAD requests
 * @note Returns 0 when Connection: close header is present
 * @note For HTTP/1.1, returns non-zero unless Connection: close is present
 *
 * @note Call this in on_headers_complete or on_message_complete callbacks
 * @note If this returns 0, this should be the last message on the connection
 *
 * @par Server Usage:
 * @code
 * if (!http_should_keep_alive(parser)) {
 *   // Send "Connection: close" header in response
 * }
 * @endcode
 *
 * @par Client Usage:
 * @code
 * if (!http_should_keep_alive(parser)) {
 *   // Close the connection after reading response
 * }
 * @endcode
 *
 * @see http_parser, on_headers_complete, on_message_complete
 */
int http_should_keep_alive(const http_parser *parser);

/**
 * Get string representation of HTTP method
 *
 * Returns a pointer to a string containing the textual representation
 * of the specified HTTP method. For example, HTTP_GET returns "GET".
 *
 * @param m The HTTP method enumeration value
 *
 * @return const char* Pointer to a string constant containing the method name
 *
 * @note Returns "<unknown>" if the method enumeration value is invalid
 * @note Returned string is static and should not be modified or freed
 * @note This function is thread-safe
 *
 * @par Example:
 * @code
 * printf("Method: %s\n", http_method_str(parser->method));
 * @endcode
 *
 * @see http_method, parser->method
 */
const char *http_method_str(enum http_method m);

/**
 * Get string representation of HTTP status code
 *
 * Returns a pointer to a string containing the textual representation
 * of the specified HTTP status code. For example, HTTP_STATUS_200 returns "OK".
 *
 * @param s The HTTP status code enumeration value
 *
 * @return const char* Pointer to a string constant containing the status text
 *
 * @note Returns "<unknown>" if the status code enumeration value is invalid
 * @note Returned string is static and should not be modified or freed
 * @note This function is thread-safe
 *
 * @par Example:
 * @code
 * printf("Status: %s\n", http_status_str(parser->status_code));
 * @endcode
 *
 * @see http_status, parser->status_code
 */
const char *http_status_str(enum http_status s);

/**
 * Get error code name string
 *
 * Returns a pointer to a string containing the symbolic name of the
 * specified HTTP parser error code. For example, HPE_INVALID_VERSION
 * returns "HPE_INVALID_VERSION".
 *
 * @param err The HTTP parser error code
 *
 * @return const char* Pointer to a string constant containing the error name
 *
 * @note Returned string is static and should not be modified or freed
 * @note This function is thread-safe
 *
 * @par Example:
 * @code
 * printf("Error: %s\n", http_errno_name(HTTP_PARSER_ERRNO(parser)));
 * @endcode
 *
 * @see http_errno, HTTP_PARSER_ERRNO(), http_errno_description()
 */
const char *http_errno_name(enum http_errno err);

/**
 * Get error description string
 *
 * Returns a pointer to a string containing a human-readable description
 * of the specified HTTP parser error code. This is useful for logging
 * and debugging purposes.
 *
 * @param err The HTTP parser error code
 *
 * @return const char* Pointer to a string constant containing the error description
 *
 * @note Returned string is static and should not be modified or freed
 * @note This function is thread-safe
 *
 * @par Example:
 * @code
 * printf("Error: %s\n", http_errno_description(HTTP_PARSER_ERRNO(parser)));
 * @endcode
 *
 * @see http_errno, HTTP_PARSER_ERRNO(), http_errno_name()
 */
const char *http_errno_description(enum http_errno err);

/**
 * Initialize URL parser result structure
 *
 * Initializes all members of the http_parser_url structure to zero.
 * This should be called before passing the structure to http_parser_parse_url().
 *
 * @param u Pointer to the http_parser_url structure to initialize
 *
 * @note Sets all field offsets and lengths to 0
 * @note Sets field_set to 0
 * @note Sets port to 0
 *
 * @see http_parser_url, http_parser_parse_url()
 */
void http_parser_url_init(struct http_parser_url *u);

/**
 * Parse URL string into components
 *
 * Parses a URL string and stores the extracted components in the provided
 * http_parser_url structure. This is a zero-copy parser that stores
 * offsets and lengths into the original URL string.
 *
 * @param buf Pointer to the URL string to parse
 * @param buflen Length of the URL string in bytes
 * @param is_connect Non-zero if parsing for CONNECT request (hostname:port format only)
 * @param u Pointer to the http_parser_url structure to store results
 *
 * @return int 0 on success, 1 on failure
 *
 * @note The URL string is NOT modified - offsets refer to the original buffer
 * @note Percent-encoding is NOT decoded - raw URL components are returned
 * @note Check u.field_set to determine which components were found
 * @note Access components using u.field_data[UF_*].off and u.field_data[UF_*].len
 * @note The port field contains the parsed port number as uint16_t
 *
 * @note For is_connect mode, only hostname and port are recognized
 *
 * @par Example:
 * @code
 * struct http_parser_url url;
 * const char *url_str = "http://example.com:8080/path?query#fragment";
 *
 * http_parser_url_init(&url);
 * if (http_parser_parse_url(url_str, strlen(url_str), 0, &url) == 0) {
 *   if (url.field_set & (1 << UF_HOST)) {
 *     printf("Host: %.*s\n",
 *            url.field_data[UF_HOST].len,
 *            url_str + url.field_data[UF_HOST].off);
 *   }
 *   if (url.field_set & (1 << UF_PORT)) {
 *     printf("Port: %u\n", url.port);
 *   }
 * }
 * @endcode
 *
 * @see http_parser_url, http_parser_url_init(), UF_*
 */
int http_parser_parse_url(const char *buf, size_t buflen,
                          int is_connect,
                          struct http_parser_url *u);

/**
 * Pause or unpause the parser
 *
 * Pauses or unpauses the HTTP parser. When paused, the parser will
 * stop processing data and set its error code to HPE_PAUSED.
 *
 * @param parser The parser object to pause or unpause
 * @param paused Non-zero to pause, zero to unpause
 *
 * @note Pausing is useful for implementing flow control in streaming scenarios
 * @note When paused, http_parser_execute() will return immediately with HPE_PAUSED
 * @note Only call this when the parser is not in a permanent error state
 * @note Unpausing resets the error code to HPE_OK
 *
 * @par Example:
 * @code
 * // Pause parser during slow callback processing
 * http_parser_pause(parser, 1);
 * // ... perform slow operation ...
 * http_parser_pause(parser, 0); // Resume parsing
 * @endcode
 *
 * @see http_errno, HPE_PAUSED, http_parser_execute()
 */
void http_parser_pause(http_parser *parser, int paused);

/**
 * Check if body parsing is complete
 *
 * Checks whether the parser has reached the final state of the HTTP message
 * body parsing. This is useful for determining if the last byte of the body
 * has been processed.
 *
 * @param parser The parser object to check
 *
 * @return int Non-zero if body parsing is complete, 0 otherwise
 *
 * @note This function is primarily useful for HTTP/1.0 responses without Content-Length
 * @note In HTTP/1.1 with Content-Length, this becomes true when all content is read
 * @note For chunked encoding, this becomes true after the last chunk is processed
 *
 * @note This complements http_should_keep_alive() for connection management
 *
 * @par Example:
 * @code
 * if (http_body_is_final(parser)) {
 *   // All body data has been processed
 *   // Can now send response or close connection
 * }
 * @endcode
 *
 * @see http_parser, http_should_keep_alive()
 */
int http_body_is_final(const http_parser *parser);

/**
 * Set maximum header size limit
 *
 * Overrides the default maximum allowed header size at runtime. This
 * function allows applications to dynamically adjust the header size
 * limit for security purposes, particularly to prevent denial-of-service
 * attacks from malicious clients sending excessively large headers.
 *
 * @param size The new maximum header size in bytes
 *
 * @note Default limit is HTTP_MAX_HEADER_SIZE (80KB)
 * @note This setting affects all subsequent parsing operations
 * @note Setting size to 0 removes the effective limit (use with caution)
 * @note This function is not thread-safe - call before starting any parsing
 *
 * @warning Setting this too high may make your application vulnerable to
 *          denial-of-service attacks from malicious clients
 *
 * @par Example:
 * @code
 * // Set more restrictive limit for untrusted connections
 * http_parser_set_max_header_size(16 * 1024); // 16KB limit
 * @endcode
 *
 * @see HTTP_MAX_HEADER_SIZE
 */
void http_parser_set_max_header_size(uint32_t size);

#ifdef __cplusplus
}
#endif
#endif /* http_parser_h */
