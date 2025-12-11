#ifndef MIDDLEWARE_STRINGS_H
#define MIDDLEWARE_STRINGS_H

#ifdef __cplusplus
extern "C" {
#endif

// Status codes
static const char *HTTP_STATUS_200_OK = "200 OK";
static const char *HTTP_STATUS_401_UNAUTHORIZED = "401 Unauthorized";

// Headers
static const char *HTTP_HDR_WWW_AUTHENTICATE = "WWW-Authenticate";
static const char *HTTP_HDR_ACCESS_CONTROL_ALLOW_ORIGIN = "Access-Control-Allow-Origin";
static const char *HTTP_HDR_ACCESS_CONTROL_ALLOW_METHODS = "Access-Control-Allow-Methods";
static const char *HTTP_HDR_ACCESS_CONTROL_ALLOW_HEADERS = "Access-Control-Allow-Headers";
static const char *HTTP_HDR_ACCESS_CONTROL_ALLOW_CREDENTIALS = "Access-Control-Allow-Credentials";
static const char *HTTP_HDR_ACCESS_CONTROL_MAX_AGE = "Access-Control-Max-Age";

// Auth values
static const char *HTTP_AUTH_BASIC_REALM = "Basic realm=\"Protected Area\"";
static const char *HTTP_CORS_TRUE = "true";

// Error messages
static const char *HTTP_ERR_AUTH_REQUIRED = "Authentication required";
static const char *HTTP_ERR_AUTH_BASIC_REQUIRED = "Basic authentication required";
static const char *HTTP_ERR_AUTH_INVALID_FORMAT = "Invalid credentials format";
static const char *HTTP_ERR_AUTH_INVALID_CREDS = "Invalid credentials";
static const char *HTTP_ERR_CORS_ORIGIN_NOT_ALLOWED = "Origin not allowed";

#ifdef __cplusplus
}
#endif

#endif /* MIDDLEWARE_STRINGS_H */
