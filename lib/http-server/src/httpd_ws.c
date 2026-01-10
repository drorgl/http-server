/*
 * SPDX-FileCopyrightText: 2020-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <log.h>
#include "esp_httpd_priv.h"
#include <http_server.h>
#include "generic_event_group.h"
#include <sha1.h>
#include <base64_codec.h>
#include <malloc.h>
#ifdef ESP_PLATFORM
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <esp_err.h>

#endif


#ifdef CONFIG_HTTPD_WS_SUPPORT

#define WS_SEND_OK      (1 << 0)
#define WS_SEND_FAILED  (1 << 1)

typedef struct {
    httpd_ws_frame_t frame;
    httpd_handle_t handle;
    int socket;
    transfer_complete_cb callback;
    void *arg;
    bool blocking;
    event_group_handle_t transfer_done;
} async_transfer_t;

static const char *TAG="httpd_ws";

/*
 * Bit masks for WebSocket frames.
 * Please refer to RFC6455 Section 5.2 for more details.
 */
#define HTTPD_WS_CONTINUE       0x00U
#define HTTPD_WS_FIN_BIT        0x80U
#define HTTPD_WS_OPCODE_BITS    0x0fU
#define HTTPD_WS_MASK_BIT       0x80U
#define HTTPD_WS_LENGTH_BITS    0x7fU

/*
 * The magic GUID string used for handshake
 * Please refer to RFC6455 Section 1.3 for more details.
 */
static const char ws_magic_uuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* Checks if any subprotocols from the comma seperated list matches the supported one
 *
 * Returns true if the response should contain a protocol field
*/

/**
 * @brief Checks if any subprotocols from the comma seperated list matches the supported one
 *
 * @param supported_subprotocol[in] The subprotocol supported by the URI
 * @param subprotocol[in],  [in]: A comma seperate list of subprotocols requested
 * @param buf_len Length of the buffer
 * @return true: found a matching subprotocol
 * @return false
 */
static bool httpd_ws_get_response_subprotocol(const char *supported_subprotocol, char *subprotocol, size_t buf_len)
{
    /* Request didnt contain any subprotocols */
    if (strnlen(subprotocol, buf_len) == 0) {
        return false;
    }

    if (supported_subprotocol == NULL) {
        LOGW(TAG, "Sec-WebSocket-Protocol %s not supported, URI do not support any subprotocols", subprotocol);
        return false;
    }

    /* Get first subprotocol from comma seperated list */
    char *rest = NULL;
    char *s = strtok_r(subprotocol, ", ", &rest);
    do {
        if (strncmp(s, supported_subprotocol, sizeof(subprotocol)) == 0) {
            LOGD(TAG, "Requested subprotocol supported: %s", s);
            return true;
        }
    } while ((s = strtok_r(NULL, ", ", &rest)) != NULL);

    LOGW(TAG, "Sec-WebSocket-Protocol %s not supported, supported subprotocol is %s", subprotocol, supported_subprotocol);

    /* No matches */
    return false;

}

/**
 * @brief Validate if a string is a valid RFC 6455 token
 *
 * Tokens consist of any character except control characters, separators, and DQUOTE.
 * Valid characters: [a-zA-Z0-9!#$%&'*+-.^_`|~]
 *
 * @param str The string to validate
 * @return true if valid token, false otherwise
 */
static bool httpd_ws_is_valid_token(const char *str)
{
    if (!str || !*str) {
        return false;
    }

    for (const char *p = str; *p; p++) {
        char c = *p;
        // RFC 6455 token characters: any char except ctl, separators, DQUOTE
        // separators: ()<>@,;:\"/[]?={} space tab
        if (c <= 31 || c >= 127 || // control chars and non-ASCII
            c == '(' || c == ')' || c == '<' || c == '>' || c == '@' ||
            c == ',' || c == ';' || c == ':' || c == '\\' || c == '"' ||
            c == '/' || c == '[' || c == ']' || c == '?' || c == '=' ||
            c == '{' || c == '}' || c == ' ' || c == '\t') {
            return false;
        }
    }
    return true;
}

/**
 * @brief Validate parameter value format and optionally check ranges
 *
 * Values can be tokens, quoted strings, or empty strings.
 * For known parameters, validates value ranges.
 *
 * @param key The parameter key
 * @param value The parameter value (with quotes if present)
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG if invalid
 */
static esp_err_t httpd_ws_validate_param_value(const char *key, const char *value)
{
    if (!key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    // Handle quoted values (basic validation)
    if (value[0] == '"') {
        // Must be properly quoted
        size_t len = strlen(value);
        if (len < 2 || value[len - 1] != '"') {
            LOGW(TAG, "Malformed quoted parameter value: %s", value);
            return ESP_ERR_INVALID_ARG;
        }
        // Additional validation could go here (escape sequences, etc.)
    } else {
        // Unquoted value - should be a token or valid sequence
        if (!httpd_ws_is_valid_token(value) && strcmp(value, "") != 0) {
            // Allow empty strings, but non-empty must be valid tokens
            LOGW(TAG, "Invalid unquoted parameter value: %s", value);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Range checking for known parameters
    if (strcmp(key, "client_max_window_bits") == 0 ||
        strcmp(key, "server_max_window_bits") == 0) {
        // For permessage-deflate, values should be 8-15 inclusive
        char unquoted[32] = {0};
        if (value[0] == '"' && value[strlen(value)-1] == '"') {
            size_t len = strlen(value) - 2;
            if (len >= sizeof(unquoted)) {
                return ESP_ERR_INVALID_ARG;
            }
            memcpy(unquoted, value + 1, len);
            unquoted[len] = '\0';
        } else {
            if (strlen(value) >= sizeof(unquoted)) {
                return ESP_ERR_INVALID_ARG;
            }
            strcpy(unquoted, value);
        }

        char *endptr;
        long num = strtol(unquoted, &endptr, 10);
        if (*endptr != '\0' || num < 8 || num > 15) {
            LOGW(TAG, "Invalid window bits value: %s (must be 8-15)", unquoted);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // LOGD(TAG, "httpd_ws_send_frame_async: Returning ESP_OK");
    return ESP_OK;
}

/**
 * @brief Parse WebSocket extension parameters from a parameter string
 *
 * @param param_str The parameter string (e.g. "client_max_window_bits=15; server_max_window_bits=15")
 * @param params Array to store parsed parameters
 * @param num_params Pointer to store number of parameters found
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for malformed parameters, ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t httpd_ws_parse_extension_params(const char *param_str, extension_param_t **params, size_t *num_params)
{
    if (!param_str || param_str[0] == '\0') {
        *params = NULL;
        *num_params = 0;
        return ESP_OK;
    }

    /* Create a copy of param_str since strtok_r modifies the string */
    char *param_copy = strdup(param_str);
    if (!param_copy) {
        return ESP_ERR_NO_MEM;
    }

    /* Count parameters first */
    size_t param_count = 0;
    char *temp = param_copy;
    while (*temp) {
        if (*temp == ';') {
            param_count++;
        }
        temp++;
    }
    param_count++; /* Add one for the last parameter */

    /* Allocate memory for parameters - limit to prevent DoS */
    if (param_count > 10) {
        LOGW(TAG, "Too many extension parameters: %"NEWLIB_NANO_COMPAT_FORMAT, NEWLIB_NANO_COMPAT_CAST(param_count));
        free(param_copy);
        return ESP_ERR_INVALID_ARG;
    }

    extension_param_t *param_array = (extension_param_t *)malloc(param_count * sizeof(extension_param_t));
    if (!param_array) {
        free(param_copy);
        return ESP_ERR_NO_MEM;
    }

    /* Parse each parameter */
    char *saveptr = NULL;
    char *token = strtok_r(param_copy, ";", &saveptr);
    size_t parsed_count = 0;

    while (token && parsed_count < param_count) {
        /* Skip leading whitespace */
        while (*token && (*token == ' ' || *token == '\t')) {
            token++;
        }

        /* Find key=value */
        char *eq_pos = strchr(token, '=');
        if (!eq_pos) {
            LOGW(TAG, "Malformed parameter, missing '=': '%s'", token);
            free(param_copy);
            free(param_array);
            return ESP_ERR_INVALID_ARG;
        }

        /* Split key and value */
        *eq_pos = '\0';
        const char *key = token;
        const char *value = eq_pos + 1;

        /* Trim whitespace */
        while (*value && (*value == ' ' || *value == '\t')) {
            value++;
        }

        /* Validate parameter key and value */
        if (!httpd_ws_is_valid_token(key)) {
            LOGW(TAG, "Invalid parameter key: '%s'", key);
            free(param_copy);
            free(param_array);
            return ESP_ERR_INVALID_ARG;
        }

        if (httpd_ws_validate_param_value(key, value) != ESP_OK) {
            LOGW(TAG, "Parameter validation failed for '%s'='%s'", key, value);
            free(param_copy);
            free(param_array);
            return ESP_ERR_INVALID_ARG;
        }

        /* Handle quoted values (basic unquoting) - create a copy for unquoting since value may be in param_copy */
        const char *original_value = value;
        if (*value == '"') {
            value++;
            size_t len = strlen(value);
            if (len > 0 && value[len - 1] == '"') {
                ((char *)value)[len - 1] = '\0';
            }
        }

        /* Duplicate strings */
        param_array[parsed_count].key = strdup(key);
        param_array[parsed_count].value = strdup(value);

        if (!param_array[parsed_count].key || !param_array[parsed_count].value) {
            LOGE(TAG, "Failed to allocate memory for parameter key/value");
            free(param_copy);
            free(param_array);
            return ESP_ERR_NO_MEM;
        }

        parsed_count++;
        token = strtok_r(NULL, ";", &saveptr);
    }

    free(param_copy);
    *params = param_array;
    *num_params = parsed_count;
    return ESP_OK;
}

/**
 * @brief Parse the Sec-WebSocket-Extensions header
 *
 * @param header The header value (e.g. "permessage-deflate; client_max_window_bits=15, x-compress")
 * @param extensions Pointer to store parsed extensions array
 * @param num_extensions Pointer to store number of extensions found
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for malformed header, ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t httpd_ws_parse_extensions(const char *header, ws_extension_t **extensions, size_t *num_extensions)
{
    if (!header || header[0] == '\0') {
        *extensions = NULL;
        *num_extensions = 0;
        return ESP_OK;
    }

    /* Limit header size to prevent DoS attacks */
    if (strlen(header) > 1024) {
        LOGW(TAG, "Extension header too long");
        return ESP_ERR_INVALID_ARG;
    }

    /* Create a copy since strtok_r modifies the string */
    char *header_copy = strdup(header);
    if (!header_copy) {
        return ESP_ERR_NO_MEM;
    }

    /* Count extensions first */
    size_t ext_count = 0;
    char *temp = header_copy;
    while (*temp) {
        if (*temp == ',') {
            ext_count++;
        }
        temp++;
    }
    ext_count++; /* Add one for the last extension */

    /* Limit number of extensions */
    if (ext_count > 10) {
        LOGW(TAG, "Too many extensions requested: %"NEWLIB_NANO_COMPAT_FORMAT, NEWLIB_NANO_COMPAT_CAST(ext_count));
        free(header_copy);
        return ESP_ERR_INVALID_ARG;
    }

    /* Allocate extensions array */
    ws_extension_t *ext_array = (ws_extension_t *)malloc(ext_count * sizeof(ws_extension_t));
    if (!ext_array) {
        free(header_copy);
        return ESP_ERR_NO_MEM;
    }
    memset(ext_array, 0, ext_count * sizeof(ws_extension_t));

    /* Parse each extension */
    char *saveptr = NULL;
    char *token = strtok_r(header_copy, ",", &saveptr);
    size_t parsed_count = 0;

    while (token && parsed_count < ext_count) {
        /* Skip leading whitespace */
        while (*token && (*token == ' ' || *token == '\t')) {
            token++;
        }

        /* Find extension name and parameters */
        char *semicolon_pos = strchr(token, ';');
        char *ext_name = token;

        if (semicolon_pos) {
            /* Has parameters */
            *semicolon_pos = '\0';
            const char *params_str = semicolon_pos + 1;

            /* Parse parameters */
            esp_err_t ret = httpd_ws_parse_extension_params(params_str, &ext_array[parsed_count].params, &ext_array[parsed_count].num_params);
            if (ret != ESP_OK) {
                LOGW(TAG, "Failed to parse extension parameters for: %s", ext_name);
                free(header_copy);
                httpd_ws_free_extensions(ext_array, parsed_count + 1);
                return ret;
            }
        } else {
            /* No parameters */
            ext_array[parsed_count].params = NULL;
            ext_array[parsed_count].num_params = 0;
        }

        /* Trim whitespace from name */
        char *end = ext_name + strlen(ext_name) - 1;
        while (end > ext_name && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }

    /* Validate extension name (RFC 6455 token validation) */
    if (strlen(ext_name) == 0 || strlen(ext_name) > 50 || !httpd_ws_is_valid_token(ext_name)) {
        LOGW(TAG, "Invalid extension name: '%s'", ext_name);
        free(header_copy);
        httpd_ws_free_extensions(ext_array, parsed_count + 1);
        *extensions = NULL;
        *num_extensions = 0;
        return ESP_ERR_INVALID_ARG;
    }

        /* Duplicate name */
        ext_array[parsed_count].name = strdup(ext_name);
        if (!ext_array[parsed_count].name) {
            LOGE(TAG, "Failed to allocate memory for extension name");
            free(header_copy);
            httpd_ws_free_extensions(ext_array, parsed_count + 1);
            return ESP_ERR_NO_MEM;
        }

        parsed_count++;
        token = strtok_r(NULL, ",", &saveptr);
    }

    free(header_copy);
    *extensions = ext_array;
    *num_extensions = parsed_count;
    return ESP_OK;
}

/**
 * @brief Free memory allocated for extension structures
 */
void httpd_ws_free_extensions(ws_extension_t *extensions, size_t num_extensions)
{
    if (!extensions) {
        return;
    }

    for (size_t i = 0; i < num_extensions; i++) {
        if (extensions[i].name) {
            free(extensions[i].name);
        }
        if (extensions[i].params) {
            for (size_t j = 0; j < extensions[i].num_params; j++) {
                if (extensions[i].params[j].key) {
                    free(extensions[i].params[j].key);
                }
                if (extensions[i].params[j].value) {
                    free(extensions[i].params[j].value);
                }
            }
            free(extensions[i].params);
        }
    }
    free(extensions);
}

/**
 * @brief Negotiate WebSocket extensions between client offers and server support
 *
 * @param client_extensions Array of extensions offered by client
 * @param client_count Number of extensions in client array
 * @param server_extensions Array of extensions supported by server
 * @param server_count Number of extensions in server array
 * @param negotiated Pointer to store negotiated extensions array
 * @param negotiated_count Pointer to store count of negotiated extensions
 * @return ESP_OK on success, ESP_ERR_NO_MEM on allocation failure
 *
 * @note Currently implements simple exact name matching. Future enhancement could negotiate parameters
 */
esp_err_t httpd_ws_negotiate_extensions(const ws_extension_t *client_extensions, size_t client_count,
                                               const ws_extension_t *server_extensions, size_t server_count,
                                               ws_extension_t **negotiated, size_t *negotiated_count)
{
    if (client_count == 0 || server_count == 0) {
        *negotiated = NULL;
        *negotiated_count = 0;
        return ESP_OK;
    }

    /* Allocate array for potential negotiated extensions (worst case: all match) */
    ws_extension_t *negotiated_exts = (ws_extension_t *)malloc(client_count * sizeof(ws_extension_t));
    if (!negotiated_exts) {
        return ESP_ERR_NO_MEM;
    }
    memset(negotiated_exts, 0, client_count * sizeof(ws_extension_t));

    size_t neg_count = 0;

    /* Find intersection of client offers and server support */
    for (size_t cidx = 0; cidx < client_count; cidx++) {
        const ws_extension_t *client_ext = &client_extensions[cidx];

        for (size_t sidx = 0; sidx < server_count; sidx++) {
            const ws_extension_t *server_ext = &server_extensions[sidx];

            /* Simple exact name matching for negotiation */
            if (strcmp(client_ext->name, server_ext->name) == 0) {
                /* Copy the negotiated extension - use server parameters if present, otherwise client */
                const ws_extension_t *chosen_ext = server_ext->num_params > 0 ? server_ext : client_ext;

                /* Duplicate name */
                negotiated_exts[neg_count].name = strdup(chosen_ext->name);
                if (!negotiated_exts[neg_count].name) {
                    LOGE(TAG, "Failed to allocate memory for negotiated extension name");
                    httpd_ws_free_extensions(negotiated_exts, neg_count);
                    return ESP_ERR_NO_MEM;
                }

                /* Duplicate parameters if present */
                if (chosen_ext->num_params > 0) {
                    negotiated_exts[neg_count].params = (extension_param_t *)malloc(chosen_ext->num_params * sizeof(extension_param_t));
                    if (!negotiated_exts[neg_count].params) {
                        LOGE(TAG, "Failed to allocate memory for negotiated extension parameters");
                        free(negotiated_exts[neg_count].name);
                        httpd_ws_free_extensions(negotiated_exts, neg_count);
                        return ESP_ERR_NO_MEM;
                    }

                    for (size_t pidx = 0; pidx < chosen_ext->num_params; pidx++) {
                        negotiated_exts[neg_count].params[pidx].key = strdup(chosen_ext->params[pidx].key);
                        negotiated_exts[neg_count].params[pidx].value = strdup(chosen_ext->params[pidx].value);

                        if (!negotiated_exts[neg_count].params[pidx].key || !negotiated_exts[neg_count].params[pidx].value) {
                            LOGE(TAG, "Failed to allocate memory for negotiated parameter");
                            httpd_ws_free_extensions(negotiated_exts, neg_count + 1);
                            return ESP_ERR_NO_MEM;
                        }
                    }
                    negotiated_exts[neg_count].num_params = chosen_ext->num_params;
                } else {
                    negotiated_exts[neg_count].params = NULL;
                    negotiated_exts[neg_count].num_params = 0;
                }

                neg_count++;

                LOGD(TAG, "Negotiated extension: %s", chosen_ext->name);
                break; /* Move to next client extension */
            }
        }
    }

    *negotiated = negotiated_exts;
    *negotiated_count = neg_count;

    if (neg_count > 0) {
        LOGD(TAG, "Negotiated %"NEWLIB_NANO_COMPAT_FORMAT" extension(s)", NEWLIB_NANO_COMPAT_CAST(neg_count));
    } else {
        LOGD(TAG, "No extensions negotiated");
    }

    return ESP_OK;
}

/**
 * @brief Build Sec-WebSocket-Extensions header from negotiated extensions
 *
 * @param extensions Array of negotiated extensions
 * @param count Number of extensions
 * @return Header string or NULL if no extensions. Caller must free the returned string.
 */
char *httpd_ws_build_extension_header(const ws_extension_t *extensions, size_t count)
{
    if (count == 0) {
        return NULL;
    }

    /* Calculate required buffer size */
    size_t buf_size = 0;
    for (size_t i = 0; i < count; i++) {
        buf_size += strlen(extensions[i].name) + 2; /* +2 for comma/space or terminal characters */

        for (size_t pidx = 0; pidx < extensions[i].num_params; pidx++) {
            buf_size += strlen(extensions[i].params[pidx].key) + strlen(extensions[i].params[pidx].value) + 3; /* key=value; */
        }

        if (extensions[i].num_params > 0) {
            buf_size += 3; /* Adjust for semicolon and spaces: "ext;param=value, " */
        }
    }

    /* Limit header size to prevent excessively large responses */
    if (buf_size > 512) {
        LOGW(TAG, "Negotiated extension header too long (%zu), truncating", buf_size);
        return NULL; /* Fail gracefully - better to not negotiate than to create huge headers */
    }

    char *header = (char *)malloc(buf_size);
    if (!header) {
        return NULL;
    }

    /* Build the header */
    char *ptr = header;
    size_t remaining = buf_size;

    for (size_t i = 0; i < count; i++) {
        int written = 0;

        /* Extension name */
        written = snprintf(ptr, remaining, "%s", extensions[i].name);
        if (written < 0 || (size_t)written >= remaining) {
            free(header);
            return NULL;
        }
        ptr += written;
        remaining -= written;

        /* Parameters */
        for (size_t pidx = 0; pidx < extensions[i].num_params; pidx++) {
            if (pidx == 0) {
                written = snprintf(ptr, remaining, ";%s=%s", extensions[i].params[pidx].key, extensions[i].params[pidx].value);
            } else {
                written = snprintf(ptr, remaining, ";%s=%s", extensions[i].params[pidx].key, extensions[i].params[pidx].value);
            }
            if (written < 0 || (size_t)written >= remaining) {
                free(header);
                return NULL;
            }
            ptr += written;
            remaining -= written;
        }

        /* Add separator between extensions (except for last one) */
        if (i < count - 1) {
            written = snprintf(ptr, remaining, ", ");
            if (written < 0 || (size_t)written >= remaining) {
                free(header);
                return NULL;
            }
            ptr += written;
            remaining -= written;
        }
    }

    return header;
}

static void httpd_ws_free_fragmentation_ctx(void *ctx)
{
    if (ctx) {
        ws_fragmentation_ctx_t *frag_ctx = (ws_fragmentation_ctx_t *)ctx;
        if (frag_ctx->reassembled_buffer) {
            free(frag_ctx->reassembled_buffer);
        }
        free(ctx);
    }
}

esp_err_t httpd_ws_respond_server_handshake(httpd_req_t *req, const char *supported_subprotocol, const char *supported_extensions)
{
    /* Probe if input parameters are valid or not */
    if (!req || !req->aux) {
        LOGW(TAG, LOG_FMT("Argument is invalid"));
        return ESP_ERR_INVALID_ARG;
    }

    /* Detect handshake - reject if handshake was ALREADY performed */
    struct httpd_req_aux *req_aux = req->aux;
    if (req_aux->sd->ws_handshake_done) {
        LOGW(TAG, LOG_FMT("State is invalid - Handshake has been performed"));
        return ESP_ERR_INVALID_STATE;
    }

    /* Detect WS version existence */
    char version_val[3] = { '\0' };
    if (httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Version", version_val, sizeof(version_val)) != ESP_OK) {
        LOGW(TAG, LOG_FMT("\"Sec-WebSocket-Version\" is not found"));
        return ESP_ERR_NOT_FOUND;
    }

    /* Detect if WS version is "13" or not.
     * WS version must be 13 for now. Please refer to RFC6455 Section 4.1, Page 18 for more details. */
    if (strcasecmp(version_val, "13") != 0) {
        LOGW(TAG, LOG_FMT("\"Sec-WebSocket-Version\" is not \"13\", it is: %s"), version_val);
        return ESP_ERR_INVALID_VERSION;
    }

    /* Grab Sec-WebSocket-Key (client key) from the header */
    /* Size of base64 coded string is equal '((input_size * 4) / 3) + (input_size / 96) + 6' including Z-term */
    char sec_key_encoded[28] = { '\0' };
    if (httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Key", sec_key_encoded, sizeof(sec_key_encoded)) != ESP_OK) {
        LOGW(TAG, LOG_FMT("Cannot find client key"));
        return ESP_ERR_NOT_FOUND;
    }

    /* Prepare server key (Sec-WebSocket-Accept), concat the string */
    char server_key_encoded[33] = { '\0' };
    uint8_t server_key_hash[20] = { 0 };
    char server_raw_text[sizeof(sec_key_encoded) + sizeof(ws_magic_uuid) + 1] = { '\0' };

    strcpy(server_raw_text, sec_key_encoded);
    strcat(server_raw_text, ws_magic_uuid);

    LOGD(TAG, LOG_FMT("Server key before encoding: %s"), server_raw_text);

    /* Generate SHA-1 first and then encode to Base64 */
    size_t key_len = strlen(server_raw_text);
    sha1_context_t sha1_ctx;
    sha1_init(&sha1_ctx);
    sha1_update(&sha1_ctx, (uint8_t *)server_raw_text, key_len);
    sha1_final(&sha1_ctx, server_key_hash);

    base64_encode(server_key_hash, sizeof(server_key_hash),
                          server_key_encoded, sizeof(server_key_encoded));

    LOGD(TAG, LOG_FMT("Generated server key: %s"), server_key_encoded);

    char subprotocol[50] = { '\0' };
    if (httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Protocol", subprotocol, sizeof(subprotocol) - 1) == ESP_ERR_HTTPD_RESULT_TRUNC) {
        LOGW(TAG, "Sec-WebSocket-Protocol length exceeded buffer size of %"NEWLIB_NANO_COMPAT_FORMAT", was trunctated", NEWLIB_NANO_COMPAT_CAST(sizeof(subprotocol)));
    }

    /* Parse client extensions if present */
    ws_extension_t *client_extensions = NULL;
    size_t client_extensions_count = 0;
    char client_ext_header[256] = { '\0' };
    esp_err_t ext_parse_ret = ESP_OK;

    if (httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Extensions", client_ext_header, sizeof(client_ext_header)) == ESP_OK) {
        ext_parse_ret = httpd_ws_parse_extensions(client_ext_header, &client_extensions, &client_extensions_count);
    if (ext_parse_ret != ESP_OK) {
        LOGW(TAG, "Failed to parse client WebSocket extensions: %d %s", ext_parse_ret, client_ext_header);
        /* Continue handshake - malformed extensions should not fail the connection per RFC */
    }
    }

    /* Parse server supported extensions if any */
    ws_extension_t *server_extensions = NULL;
    size_t server_extensions_count = 0;
    if (supported_extensions && strlen(supported_extensions) > 0) {
        ext_parse_ret = httpd_ws_parse_extensions(supported_extensions, &server_extensions, &server_extensions_count);
    if (ext_parse_ret != ESP_OK) {
        LOGW(TAG, "Failed to parse server supported extensions: %d %s", ext_parse_ret, supported_extensions);
        /* Continue - this is a server configuration error, but handshake should proceed */
    }
    }

    /* Negotiate extensions */
    ws_extension_t *negotiated_extensions = NULL;
    size_t negotiated_count = 0;
    if (client_extensions_count > 0 && server_extensions_count > 0) {
        ext_parse_ret = httpd_ws_negotiate_extensions(client_extensions, client_extensions_count,
                                                     server_extensions, server_extensions_count,
                                                     &negotiated_extensions, &negotiated_count);
        if (ext_parse_ret != ESP_OK) {
            LOGW(TAG, "Extension negotiation failed, proceeding without extensions");
        }
    }

    /* Build extension response header */
    char *extension_header = NULL;
    if (negotiated_count > 0) {
        extension_header = httpd_ws_build_extension_header(negotiated_extensions, negotiated_count);
        if (!extension_header) {
            LOGW(TAG, "Failed to build extension response header, proceeding without extensions");
            negotiated_count = 0; /* Don't include extensions header */
        }
    }


    /* Prepare the Switching Protocol response */
    char tx_buf[192] = { '\0' };
    int fmt_len = snprintf(tx_buf, sizeof(tx_buf),
                           "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: %s\r\n", server_key_encoded);

    if (fmt_len < 0 || fmt_len > sizeof(tx_buf)) {
        LOGW(TAG, LOG_FMT("Failed to prepare Tx buffer"));
        /* Cleanup allocated memory */
        httpd_ws_free_extensions(client_extensions, client_extensions_count);
        httpd_ws_free_extensions(server_extensions, server_extensions_count);
        httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
        free(extension_header);
        return ESP_FAIL;
    }

    if ( httpd_ws_get_response_subprotocol(supported_subprotocol, subprotocol, sizeof(subprotocol))) {
        LOGD(TAG, "subprotocol: %s", subprotocol);
        int r = snprintf(tx_buf + fmt_len, sizeof(tx_buf) - fmt_len, "Sec-WebSocket-Protocol: %s\r\n", supported_subprotocol);
        if (r <= 0) {
            LOGE(TAG, "Error in response generation"
                          "(snprintf of subprotocol returned %d, buffer size: %"NEWLIB_NANO_COMPAT_FORMAT, r, NEWLIB_NANO_COMPAT_CAST(sizeof(tx_buf)));
            /* Cleanup allocated memory */
            httpd_ws_free_extensions(client_extensions, client_extensions_count);
            httpd_ws_free_extensions(server_extensions, server_extensions_count);
            httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
            free(extension_header);
            return ESP_FAIL;
        }

        fmt_len += r;

        if (fmt_len >= sizeof(tx_buf)) {
            LOGE(TAG, "Error in response generation"
                          "(snprintf of subprotocol returned %d, desired response len: %d, buffer size: %"NEWLIB_NANO_COMPAT_FORMAT, r, fmt_len, NEWLIB_NANO_COMPAT_CAST(sizeof(tx_buf)));
            /* Cleanup allocated memory */
            httpd_ws_free_extensions(client_extensions, client_extensions_count);
            httpd_ws_free_extensions(server_extensions, server_extensions_count);
            httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
            free(extension_header);
            return ESP_FAIL;
        }
    }

    /* Add negotiated extensions header if any */
    if (extension_header && strlen(extension_header) > 0) {
        LOGD(TAG, "Including negotiated extensions in response: %s", extension_header);
        int r = snprintf(tx_buf + fmt_len, sizeof(tx_buf) - fmt_len, "Sec-WebSocket-Extensions: %s\r\n", extension_header);
        if (r <= 0) {
            LOGE(TAG, "Error in response generation"
                          "(snprintf of extensions returned %d, buffer size: %"NEWLIB_NANO_COMPAT_FORMAT, r, NEWLIB_NANO_COMPAT_CAST(sizeof(tx_buf)));
            /* Cleanup allocated memory */
            httpd_ws_free_extensions(client_extensions, client_extensions_count);
            httpd_ws_free_extensions(server_extensions, server_extensions_count);
            httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
            free(extension_header);
            return ESP_FAIL;
        }

        fmt_len += r;

        if (fmt_len >= sizeof(tx_buf)) {
            LOGE(TAG, "Error in response generation"
                          "(snprintf of extensions returned %d, desired response len: %d, buffer size: %"NEWLIB_NANO_COMPAT_FORMAT, r, fmt_len, NEWLIB_NANO_COMPAT_CAST(sizeof(tx_buf)));
            /* Cleanup allocated memory */
            httpd_ws_free_extensions(client_extensions, client_extensions_count);
            httpd_ws_free_extensions(server_extensions, server_extensions_count);
            httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
            free(extension_header);
            return ESP_FAIL;
        }
    }

    int r = snprintf(tx_buf + fmt_len, sizeof(tx_buf) - fmt_len, "\r\n");
    if (r <= 0) {
        LOGE(TAG, "Error in response generation"
                        "(snprintf of header terminal returned %d, buffer size: %"NEWLIB_NANO_COMPAT_FORMAT, r, NEWLIB_NANO_COMPAT_CAST(sizeof(tx_buf)));
        /* Cleanup allocated memory */
        httpd_ws_free_extensions(client_extensions, client_extensions_count);
        httpd_ws_free_extensions(server_extensions, server_extensions_count);
        httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
        free(extension_header);
        return ESP_FAIL;
    }
    fmt_len += r;
    if (fmt_len >= sizeof(tx_buf)) {
        LOGE(TAG, "Error in response generation"
                       "(snprintf of header terminal returned %d, desired response len: %d, buffer size: %"NEWLIB_NANO_COMPAT_FORMAT, r, fmt_len, NEWLIB_NANO_COMPAT_CAST(sizeof(tx_buf)));
        /* Cleanup allocated memory */
        httpd_ws_free_extensions(client_extensions, client_extensions_count);
        httpd_ws_free_extensions(server_extensions, server_extensions_count);
        httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
        free(extension_header);
        return ESP_FAIL;
    }

    /* Cleanup allocated memory before sending response */
    httpd_ws_free_extensions(client_extensions, client_extensions_count);
    httpd_ws_free_extensions(server_extensions, server_extensions_count);
    httpd_ws_free_extensions(negotiated_extensions, negotiated_count);
    free(extension_header);

    /* Send off the response */
    if (httpd_send(req, tx_buf, fmt_len) < 0) {
        LOGW(TAG, LOG_FMT("Failed to send the response"));
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t httpd_ws_check_req(httpd_req_t *req)
{
    /* Probe if input parameters are valid or not */
    if (!req || !req->aux) {
        LOGW(TAG, LOG_FMT("Argument is null"));
        return ESP_ERR_INVALID_ARG;
    }

    /* Detect handshake - reject if handshake was NOT YET performed */
    struct httpd_req_aux *req_aux = req->aux;
    if (!req_aux->sd->ws_handshake_done) {
        LOGW(TAG, LOG_FMT("State is invalid - No handshake performed"));
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static esp_err_t httpd_ws_unmask_payload(uint8_t *payload, size_t len, const uint8_t *mask_key)
{
    if (len < 1 || !payload) {
        LOGW(TAG, LOG_FMT("Invalid payload provided"));
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t idx = 0; idx < len; idx++) {
        payload[idx] = (payload[idx] ^ mask_key[idx % 4]);
    }

    return ESP_OK;
}

/**
 * @brief Validate UTF-8 encoding in a byte sequence
 *
 * Validates UTF-8 byte sequence according to RFC 3629.
 * Returns ESP_OK if valid UTF-8, ESP_ERR_INVALID_ARG if invalid.
 *
 * @param data Pointer to the data to validate
 * @param len Length of the data in bytes
 * @return ESP_OK if valid UTF-8, ESP_ERR_INVALID_ARG otherwise
 */
esp_err_t httpd_ws_validate_utf8(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_OK; /* Empty data is valid */
    }

    size_t i = 0;
    while (i < len) {
        uint8_t byte1 = data[i++];

        /* Single byte ASCII character (0x00-0x7F) - RFC 3629 Section 1 */
        if ((byte1 & 0x80) == 0x00) {
            continue;
        }

        /* Invalid continuation byte in single byte position */
        if ((byte1 & 0xC0) == 0x80) {
            LOGD(TAG, "Invalid UTF-8: continuation byte without start byte at position %zu", i-1);
            return ESP_ERR_INVALID_ARG;
        }

        /* 2-byte sequence (0xC0-0xDF) */
        if ((byte1 & 0xE0) == 0xC0) {
            /* Overlong encoding or surrogate check - RFC 3629 Section 3 */
            if ((byte1 & 0xFE) == 0xC0) {
                LOGD(TAG, "Invalid UTF-8: overlong 2-byte encoding at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            if (i + 1 > len || (data[i] & 0xC0) != 0x80) {
                LOGD(TAG, "Invalid UTF-8: incomplete or invalid 2-byte sequence at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            i += 1;
            continue;
        }

        /* 3-byte sequence (0xE0-0xEF) */
        if ((byte1 & 0xF0) == 0xE0) {
            /* Overlong encoding and surrogate checks - RFC 3629 Section 3 */
            if (byte1 == 0xE0 && (data[i] & 0xE0) == 0x80) {
                LOGD(TAG, "Invalid UTF-8: overlong 3-byte encoding at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            /* U+D800 to U+DFFF surrogate check */
            if (byte1 == 0xED && (data[i] & 0xE0) == 0xA0) {
                LOGD(TAG, "Invalid UTF-8: surrogate character at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            if (i + 2 > len ||
                (data[i] & 0xC0) != 0x80 ||
                (data[i+1] & 0xC0) != 0x80) {
                LOGD(TAG, "Invalid UTF-8: incomplete or invalid 3-byte sequence at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            i += 2;
            continue;
        }

        /* 4-byte sequence (0xF0-0xF4) */
        if ((byte1 & 0xF8) == 0xF0) {
            /* Overlong encoding check */
            if (byte1 == 0xF0 && (data[i] & 0xF0) == 0x80) {
                LOGD(TAG, "Invalid UTF-8: overlong 4-byte encoding at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            /* RFC 3629 Section 3: 4-byte sequences range: U+10000 to U+10FFFF */
            if (byte1 > 0xF4) {
                LOGD(TAG, "Invalid UTF-8: out-of-range 4-byte sequence at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            if (i + 3 > len ||
                (data[i] & 0xC0) != 0x80 ||
                (data[i+1] & 0xC0) != 0x80 ||
                (data[i+2] & 0xC0) != 0x80) {
                LOGD(TAG, "Invalid UTF-8: incomplete or invalid 4-byte sequence at position %zu", i-1);
                return ESP_ERR_INVALID_ARG;
            }
            i += 3;
            continue;
        }

        /* Invalid start byte */
        LOGD(TAG, "Invalid UTF-8: invalid start byte 0x%02X at position %zu", byte1, i-1);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t httpd_ws_recv_frame(httpd_req_t *req, httpd_ws_frame_t *frame, size_t max_len)
{
    LOGD(TAG, "httpd_ws_recv_frame: Entry. max_len: %" NEWLIB_NANO_COMPAT_FORMAT, NEWLIB_NANO_COMPAT_CAST(max_len));
    esp_err_t ret = httpd_ws_check_req(req);
    if (ret != ESP_OK) {
        return ret;
    }

    struct httpd_req_aux *aux = req->aux;
    if (aux == NULL) {
        LOGW(TAG, LOG_FMT("Invalid Aux pointer"));
        return ESP_ERR_INVALID_ARG;
    }

    if (!frame) {
        LOGW(TAG, LOG_FMT("Frame pointer is invalid"));
        return ESP_ERR_INVALID_ARG;
    }

    struct sock_db *sd = aux->sd;
    if (sd == NULL) {
        LOGW(TAG, LOG_FMT("Invalid sd pointer"));
        return ESP_ERR_INVALID_ARG;
    }
    ws_fragmentation_ctx_t *ws_frg_ctx = (ws_fragmentation_ctx_t *)sd->ws_fragment_ctx;
    ws_fragmentation_ctx_t *frag_ctx = ws_frg_ctx;

    size_t current_frame_fragment_len = 0;
    if (frame->len != 0) {
        current_frame_fragment_len = frame->len;
    }
    uint8_t current_frame_type = 0; // The frame type of the current *fragment*
    if (frame->len != 0) {
        current_frame_type = aux->ws_type;
    }
    bool current_frame_final = false; // FIN flag of the current *fragment*
    if (frame->len != 0) {
        current_frame_final = aux->ws_final;
    }

    /* If frame len is 0, will get frame len from req. Otherwise regard frame len already achieved by calling httpd_ws_recv_frame before */
    if (frame->len == 0) {
        /* Assign the frame info from the previous reading. These are the values from httpd_ws_get_frame_type */
        current_frame_type = aux->ws_type;
        current_frame_final = aux->ws_final;
        LOGD(TAG, "httpd_ws_recv_frame: Inside if (frame->len == 0). current_frame_type: %d, aux->ws_type: %d", current_frame_type, aux->ws_type);

        /* Grab the second byte */
        uint8_t second_byte = 0;
        int recv_ret = httpd_recv_with_opt(req, (char *)&second_byte, sizeof(second_byte), false);
        if (recv_ret <= 0) {
            LOGW(TAG, LOG_FMT("Failed to receive the second byte. Ret: %d"), recv_ret);
            return ESP_FAIL;
        }

        /* Parse the second byte */
        /* Please refer to RFC6455 Section 5.2 for more details */
        bool masked = (second_byte & HTTPD_WS_MASK_BIT) != 0;

        /* Interpret length */
        uint8_t init_len = second_byte & HTTPD_WS_LENGTH_BITS;
        if (init_len < 126) {
            /* Case 1: If length is 0-125, then this length bit is 7 bits */
            current_frame_fragment_len = init_len;
        } else if (init_len == 126) {
            /* Case 2: If length byte is 126, then this frame's length bit is 16 bits */
            uint8_t length_bytes[2] = { 0 };
            recv_ret = httpd_recv_with_opt(req, (char *)length_bytes, sizeof(length_bytes), false);
            if (recv_ret <= 0) {
                LOGW(TAG, LOG_FMT("Failed to receive 2 bytes length. Ret: %d"), recv_ret);
                // RFC 6455 Section 7.4.1: Send Close frame with protocol error (1002) before closing
                httpd_ws_frame_t close_frame = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_CLOSE,
                    .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 in network byte order
                    .len = 2
                };
                httpd_ws_send_frame(req, &close_frame);
                /* Give client some time to receive the close frame */
                httpd_os_thread_sleep(100); // 100ms delay
                return ESP_FAIL;
            }
            current_frame_fragment_len = ((uint32_t)(length_bytes[0] << 8U) | (length_bytes[1]));
        } else if (init_len == 127) {
            /* Case 3: If length is byte 127, then this frame's length bit is 64 bits */
            uint8_t length_bytes[8] = { 0 };
            recv_ret = httpd_recv_with_opt(req, (char *)length_bytes, sizeof(length_bytes), false);
            if (recv_ret <= 0) {
                LOGW(TAG, LOG_FMT("Failed to receive 8 bytes length. Ret: %d"), recv_ret);
                // RFC 6455 Section 7.4.1: Send Close frame with protocol error (1002) before closing
                httpd_ws_frame_t close_frame = {
                    .final = true,
                    .fragmented = false,
                    .type = HTTPD_WS_TYPE_CLOSE,
                    .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 in network byte order
                    .len = 2
                };
                httpd_ws_send_frame(req, &close_frame);
                /* Give client some time to receive the close frame */
                httpd_os_thread_sleep(100); // 100ms delay
                return ESP_FAIL;
            }
            current_frame_fragment_len = (((uint64_t)length_bytes[0] << 56U) |
                    ((uint64_t)length_bytes[1] << 48U) |
                    ((uint64_t)length_bytes[2] << 40U) |
                    ((uint64_t)length_bytes[3] << 32U) |
                    ((uint64_t)length_bytes[4] << 24U) |
                    ((uint64_t)length_bytes[5] << 16U) |
                    ((uint64_t)length_bytes[6] <<  8U) |
                    ((uint64_t)length_bytes[7]));
        }

        /* If this frame is masked, dump the mask as well */
        if (masked) {
            recv_ret = httpd_recv_with_opt(req, (char *)aux->mask_key, sizeof(aux->mask_key), false);
            if (recv_ret <= 0) {
                LOGW(TAG, LOG_FMT("Failed to receive mask key. Ret: %d"), recv_ret);
                return ESP_FAIL;
            }
        } else {
            /* If the WS frame from client to server is not masked, it should be rejected.
             * Please refer to RFC6455 Section 5.2 for more details. */
            LOGW(TAG, LOG_FMT("WS frame is not properly masked."));
            // RFC 6455 Section 7.4.1: Send Close frame with protocol error (1002) before closing
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 in network byte order
                .len = 2
            };
            httpd_ws_send_frame(req, &close_frame);
            return ESP_ERR_INVALID_STATE;
        }

        /* Validate mask key is not all zeros if configured (predictable mask is security risk)
         * RFC 6455 Section 5.3: Masking keys should be unpredictable */
        if (((struct httpd_data *)req->handle)->config.ws_validate_mask_key &&
            aux->mask_key[0] == 0 && aux->mask_key[1] == 0 &&
            aux->mask_key[2] == 0 && aux->mask_key[3] == 0) {
            LOGW(TAG, LOG_FMT("WS frame has all-zero mask key (predictable mask)."));
            // RFC 6455 Section 7.4.1: Send Close frame with protocol error (1002) before closing
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 in network byte order
                .len = 2
            };
            httpd_ws_send_frame(req, &close_frame);
            return ESP_ERR_INVALID_STATE;
        }
    }

    /* Populate frame structure with parsed header information */
    frame->len = current_frame_fragment_len;
    frame->type = current_frame_type;
    frame->final = current_frame_final;
    frame->api_allocated_payload = false;  // Initialize flag

    /* Handle fragmentation */
    if (ws_frg_ctx && ws_frg_ctx->in_fragmentation) {
        // Control frames (0x8-0xF) are handled independently and do not affect fragmentation reassembly
        if (current_frame_type >= HTTPD_WS_TYPE_CLOSE) { // Control frame
            if (frame->payload == NULL) {
                frame->payload = (uint8_t *)malloc(current_frame_fragment_len + 1);
                if (!frame->payload) {
                    LOGE(TAG, LOG_FMT("Failed to allocate payload buffer for WebSocket control frame"));
                    return ESP_ERR_NO_MEM;
                }
                frame->api_allocated_payload = true;
            }

            // Receive control frame payload
            size_t left_len_ctrl = current_frame_fragment_len;
            size_t offset_ctrl = 0;
            while (left_len_ctrl > 0) {
                int read_ctrl = httpd_recv_with_opt(req, (char *)frame->payload + offset_ctrl,
                                                    left_len_ctrl, false);
                if (read_ctrl <= 0) {
                    LOGW(TAG, LOG_FMT("Failed to receive control frame payload. Ret: %d"), read_ctrl);
                    return ESP_FAIL;
                }
                offset_ctrl += read_ctrl;
                left_len_ctrl -= read_ctrl;
            }

            /* Unmask control frame payload */
            httpd_ws_unmask_payload(frame->payload, current_frame_fragment_len, aux->mask_key);

            // Set frame fields for control frame
            frame->len = current_frame_fragment_len;
            frame->type = current_frame_type;
            frame->final = current_frame_final;
            frame->fragmented = false; // Control frames are not fragmented

            LOGD_BUFFER_HEXDUMP(TAG, frame->payload, current_frame_fragment_len,
                               "Server received control frame during fragmentation: type=%d, final=%d, len=%zu",
                               current_frame_type, current_frame_final, current_frame_fragment_len);

            // Do not affect fragmentation state - control frames are independent
            return ESP_OK;
        }

        // Validate fragment size limits to prevent DoS
        if (frag_ctx && current_frame_fragment_len > frag_ctx->buffer_size) {
            LOGD(TAG, LOG_FMT("Buffer overflow detected, sending close frame (status 1009)"));
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = (uint8_t[]){0x03, 0xF1},  // Status code 1009 (Message Too Big) in network byte order
                .len = 2
            };
            httpd_ws_send_frame(req, &close_frame);
            LOGW(TAG, LOG_FMT("Fragment too large: %"NEWLIB_NANO_COMPAT_FORMAT" bytes, max allowed: %"NEWLIB_NANO_COMPAT_FORMAT),
                 NEWLIB_NANO_COMPAT_CAST(current_frame_fragment_len), NEWLIB_NANO_COMPAT_CAST(frag_ctx->buffer_size));

            /* Drain the socket to prevent RST when closing */
            if (current_frame_fragment_len > 0) {
                size_t bytes_to_discard = current_frame_fragment_len;
                /* Put a cap on how much we drain to prevent DoS (slow clients) */
                if (bytes_to_discard > 10 * 1024) {
                     bytes_to_discard = 10 * 1024;
                }
                uint8_t discard_buf[128];
                while (bytes_to_discard > 0) {
                     size_t to_read = (bytes_to_discard > sizeof(discard_buf)) ? sizeof(discard_buf) : bytes_to_discard;
                     int ret = httpd_recv_with_opt(req, (char *)discard_buf, to_read, false);
                     if (ret <= 0) {
                         break;
                     }
                     bytes_to_discard -= ret;
                }
            }

            ws_frg_ctx->in_fragmentation = false;
            ws_frg_ctx->reassembled_len = 0;
            return ESP_ERR_INVALID_SIZE;
        }

        // Check for buffer overflow before receiving payload
        if ((ws_frg_ctx->reassembled_len + current_frame_fragment_len) > frag_ctx->buffer_size) {
            LOGD(TAG, LOG_FMT("Buffer overflow detected, sending close frame (status 1009)"));
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = (uint8_t[]){0x03, 0xF1},  // Status code 1009 (Message Too Big) in network byte order
                .len = 2
            };
            httpd_ws_send_frame(req, &close_frame);
            LOGW(TAG, LOG_FMT("Reassembly buffer overflow! Size: %"NEWLIB_NANO_COMPAT_FORMAT", current frame: %"NEWLIB_NANO_COMPAT_FORMAT", max: %"NEWLIB_NANO_COMPAT_FORMAT),
                 NEWLIB_NANO_COMPAT_CAST(ws_frg_ctx->reassembled_len),
                 NEWLIB_NANO_COMPAT_CAST(current_frame_fragment_len),
                 NEWLIB_NANO_COMPAT_CAST(frag_ctx->buffer_size));

            /* Drain the socket to prevent RST when closing */
            if (current_frame_fragment_len > 0) {
                size_t bytes_to_discard = current_frame_fragment_len;
                /* Put a cap on how much we drain to prevent DoS (slow clients) */
                if (bytes_to_discard > 10 * 1024) {
                     bytes_to_discard = 10 * 1024;
                }
                uint8_t discard_buf[128];
                while (bytes_to_discard > 0) {
                     size_t to_read = (bytes_to_discard > sizeof(discard_buf)) ? sizeof(discard_buf) : bytes_to_discard;
                     int ret = httpd_recv_with_opt(req, (char *)discard_buf, to_read, false);
                     if (ret <= 0) {
                         break;
                     }
                     bytes_to_discard -= ret;
                }
            }

            ws_frg_ctx->in_fragmentation = false; // Reset state
            ws_frg_ctx->reassembled_len = 0;
            return ESP_ERR_INVALID_SIZE; // Or appropriate error for buffer overflow
        }

        // Receive payload directly into the reassembly buffer
        size_t left_len = current_frame_fragment_len;
        size_t offset = 0;

        while (left_len > 0) {
            int read_len_frag = httpd_recv_with_opt(req, (char *)ws_frg_ctx->reassembled_buffer + ws_frg_ctx->reassembled_len + offset, left_len, false);
            if (read_len_frag <= 0) {
                LOGW(TAG, LOG_FMT("Failed to receive fragmented payload. Ret: %d"), read_len_frag);
                ws_frg_ctx->in_fragmentation = false; // Reset state on error
                ws_frg_ctx->reassembled_len = 0;
                return ESP_FAIL;
            }
            offset += read_len_frag;
            left_len -= read_len_frag;
        }

        /* Unmask payload */
        httpd_ws_unmask_payload((uint8_t *)ws_frg_ctx->reassembled_buffer + ws_frg_ctx->reassembled_len,
                                current_frame_fragment_len, aux->mask_key);

        LOGD_BUFFER_HEXDUMP(TAG, (uint8_t *)ws_frg_ctx->reassembled_buffer + ws_frg_ctx->reassembled_len, current_frame_fragment_len, "Server received fragment: type=%d, final=%d, len=%zu", current_frame_type, current_frame_final, current_frame_fragment_len);

        ws_frg_ctx->reassembled_len += current_frame_fragment_len;

        if (current_frame_final) {
            // End of fragmented message
            LOGD(TAG, LOG_FMT("Reassembled message complete. Total length: %"NEWLIB_NANO_COMPAT_FORMAT), NEWLIB_NANO_COMPAT_CAST(ws_frg_ctx->reassembled_len));

            // Prepare the 'frame' structure for the user handler with the reassembled message
            frame->payload = (uint8_t *)ws_frg_ctx->reassembled_buffer;
            frame->len = ws_frg_ctx->reassembled_len;
            LOGD(TAG, "httpd_ws_recv_frame: Before assigning frame->type. ws_frg_ctx->message_type: %d", ws_frg_ctx->message_type);
            frame->type = ws_frg_ctx->message_type; // Original opcode (TEXT/BINARY)
            LOGD(TAG, "httpd_ws_recv_frame: After assigning frame->type. frame->type: %d", frame->type);
            frame->final = true; // Overall message is final
            frame->fragmented = false; // Not a fragmented message anymore from user's perspective

            // Reset fragmentation state for the next message
            ws_frg_ctx->in_fragmentation = false;
            ws_frg_ctx->reassembled_len = 0;
            ws_frg_ctx->message_type = 0; // Clear stored type

            return ESP_OK; // Return the reassembled message
        } else {
            // More fragments expected
            return ESP_ERR_HTTPD_WS_PENDING_FRAGMENT; // Inform caller that more fragments are expected
        }
    } else { // Not in fragmentation
        /* Validate frame type sequencing according to RFC 6455 Section 5.4 */
        if (aux->ws_type == HTTPD_WS_TYPE_CONTINUE) {
            LOGW(TAG, LOG_FMT("Invalid CONTINUATION frame (0x%x) received without active fragmentation"), aux->ws_type);
            return ESP_ERR_HTTPD_WS_ERR_FRAGMENT_PROTOCOL;
        }

        /* We only accept the incoming packet length that is smaller than the max_len (or it will overflow the buffer!) */
        /* If max_len is 0 and frame->payload is NULL, API will allocate buffer for payload */
        if (current_frame_fragment_len > max_len && (max_len != 0 || frame->payload != NULL)) {
            LOGW(TAG, LOG_FMT("WS Message too long"));
            return ESP_ERR_INVALID_SIZE;
        }

        /* Receive buffer */
        /* If there's nothing to receive, return and stop here. */
        if (current_frame_fragment_len == 0) {
            frame->len = 0; // Ensure frame->len is 0 for no payload
            return ESP_OK;
        }

        if (frame->payload == NULL) {
            frame->payload = (uint8_t *)malloc(current_frame_fragment_len + 1);
            if (!frame->payload) {
                LOGE(TAG, LOG_FMT("Failed to allocate payload buffer for WebSocket frame"));
                return ESP_ERR_NO_MEM;
            }
            frame->api_allocated_payload = true;
        }

        size_t left_len = current_frame_fragment_len;
        size_t offset = 0;

        while (left_len > 0) {
            int read_len_non_frag = httpd_recv_with_opt(req, (char *)frame->payload + offset, left_len, false);
            if (read_len_non_frag <= 0) {
                LOGW(TAG, LOG_FMT("Failed to receive payload. Ret: %d"), read_len_non_frag);
                return ESP_FAIL;
            }
            offset += read_len_non_frag;
            left_len -= read_len_non_frag;

            LOGD(TAG, "Frame length: %"NEWLIB_NANO_COMPAT_FORMAT", Bytes Read: %"NEWLIB_NANO_COMPAT_FORMAT, NEWLIB_NANO_COMPAT_CAST(current_frame_fragment_len), NEWLIB_NANO_COMPAT_CAST(offset));
        }

        /* Unmask payload */
        httpd_ws_unmask_payload(frame->payload, current_frame_fragment_len, aux->mask_key);

        frame->len = current_frame_fragment_len;
        frame->type = current_frame_type;
        frame->final = current_frame_final;

        return ESP_OK;
    }
}

esp_err_t httpd_ws_send_frame(httpd_req_t *req, httpd_ws_frame_t *frame)
{
    esp_err_t ret = httpd_ws_check_req(req);
    if (ret != ESP_OK) {
        return ret;
    }
    return httpd_ws_send_frame_async(req->handle, httpd_req_to_sockfd(req), frame);
}

esp_err_t httpd_ws_send_frame_async(httpd_handle_t hd, int fd, httpd_ws_frame_t *frame)
{
    if (!frame) {
        LOGW(TAG, LOG_FMT("Argument is invalid"));
        return ESP_ERR_INVALID_ARG;
    }

    /* Prepare Tx buffer - maximum length is 14, which includes 2 bytes header, 8 bytes length, 4 bytes mask key */
    uint8_t tx_len = 0;
    uint8_t header_buf[10] = {0 };
    LOGD(TAG, "httpd_ws_send_frame_async: Frame details - FD: %d, Final: %d, Fragmented: %d, Type: %d, Len: %zu",
         fd, frame->final, frame->fragmented, frame->type, frame->len);
    
    /* Set the `FIN` bit if the frame is final, then add the opcode */
    header_buf[0] = (frame->final ? HTTPD_WS_FIN_BIT : 0) | frame->type;
    LOGD(TAG, "httpd_ws_send_frame_async: Constructed header_buf[0]: 0x%02x with final: %d and type: %d", header_buf[0], frame->final, frame->type);

    if (frame->len <= 125) {
        header_buf[1] = frame->len & 0x7fU; /* Length for 7 bits */
        tx_len = 2;
    } else if (frame->len > 125 && frame->len < UINT16_MAX) {
        header_buf[1] = 126;                /* Length for 16 bits */
        header_buf[2] = (frame->len >> 8U) & 0xffU;
        header_buf[3] = frame->len & 0xffU;
        tx_len = 4;
    } else {
        header_buf[1] = 127;                /* Length for 64 bits */
        uint8_t shift_idx = sizeof(uint64_t) - 1; /* Shift index starts at 7 */
        uint64_t len64 = frame->len; /* Raise variable size to make sure we won't shift by more bits
                                      * than the length has (to avoid undefined behaviour) */
        for (int8_t idx = 2; idx <= 9; idx++) {
            /* Now do shifting (be careful of endianness, i.e. when buffer index is 2, frame length shift index is 7) */
            header_buf[idx] = (len64 >> (shift_idx * 8)) & 0xffU;
            shift_idx--;
        }
        tx_len = 10;
    }

    /* WebSocket server does not required to mask response payload, so leave the MASK bit as 0. */
    header_buf[1] &= (~HTTPD_WS_MASK_BIT);

    struct sock_db *sess = httpd_sess_get(hd, fd);
    if (!sess) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Send off header */
    int header_send_ret = sess->send_fn(hd, fd, (const char *)header_buf, tx_len, 0);
    if (header_send_ret < 0) {
        LOGW(TAG, "Failed to send WS header. Ret: %d, FD: %d, Type: %d, Len: %zu", header_send_ret, fd, frame->type, frame->len);
        return ESP_FAIL;
    }
    LOGD(TAG, "Sent WS header. FD: %d, Type: %d, Len: %d (payload len: %zu)", fd, frame->type, tx_len, frame->len);

    /* Send off payload */
    if(frame->len > 0 && frame->payload != NULL) {
        int payload_send_ret = sess->send_fn(hd, fd, (const char *)frame->payload, frame->len, 0);
        if (payload_send_ret < 0) {
            LOGW(TAG, "Failed to send WS payload. Ret: %d, FD: %d, Type: %d, Len: %zu", payload_send_ret, fd, frame->type, frame->len);
            return ESP_FAIL;
        }
        LOGD(TAG, "Sent WS payload. FD: %d, Type: %d, Len: %d, Payload: %.*s", fd, frame->type, frame->len, (frame->len > 32 ? 32 : (int)frame->len), (char*)frame->payload);
    }

    return ESP_OK;
}

esp_err_t httpd_ws_get_frame_type(httpd_req_t *req)
{
    LOGD(TAG, "httpd_ws_get_frame_type: Entry.");
    esp_err_t ret = httpd_ws_check_req(req);
    if (ret != ESP_OK) {
        return ret;
    }

    struct httpd_req_aux *aux = req->aux;
    if (aux == NULL) {
        LOGW(TAG, LOG_FMT("Invalid Aux pointer"));
        return ESP_ERR_INVALID_ARG;
    }

    struct sock_db *sd = aux->sd;
    if (sd == NULL) {
        LOGW(TAG, LOG_FMT("Invalid sd pointer"));
        return ESP_ERR_INVALID_ARG;
    }

    /* Get/Create fragmentation context */
    if (!sd->ws_fragment_ctx) {
        sd->ws_fragment_ctx = calloc(1, sizeof(ws_fragmentation_ctx_t));
        if (!sd->ws_fragment_ctx) {
            LOGE(TAG, LOG_FMT("Failed to allocate WS fragmentation context"));
            return ESP_ERR_NO_MEM;
        }
        // Allocate dynamic buffer for fragmentation
        ws_fragmentation_ctx_t *frag_ctx = (ws_fragmentation_ctx_t *)sd->ws_fragment_ctx;
        size_t buf_size = ((struct httpd_data *)req->handle)->config.ws_max_fragment_size;
        frag_ctx->reassembled_buffer = malloc(buf_size);
        if (!frag_ctx->reassembled_buffer) {
            LOGE(TAG, LOG_FMT("Failed to allocate WS fragmentation buffer"));
            free(sd->ws_fragment_ctx);
            return ESP_ERR_NO_MEM;
        }
        frag_ctx->buffer_size = buf_size;
        // Store the context pointer and set a free function to clean it up when the session closes
        httpd_sess_set_ctx(req->handle, httpd_req_to_sockfd(req), sd->ws_fragment_ctx, httpd_ws_free_fragmentation_ctx);
    }
    ws_fragmentation_ctx_t *ws_frg_ctx = (ws_fragmentation_ctx_t *)sd->ws_fragment_ctx;

    /* Read the first byte from the frame to get the FIN flag and Opcode */
    /* Please refer to RFC6455 Section 5.2 for more details */
    uint8_t first_byte = 0;
    if (httpd_recv_with_opt(req, (char *)&first_byte, sizeof(first_byte), false) <= 0) {
        /* If the recv() return code is <= 0, then this socket FD is invalid (i.e. a broken connection) */
        /* Here we mark it as a Close message and close it later. */
        LOGW(TAG, LOG_FMT("Failed to read header byte (socket FD invalid), closing socket now"));

        // If we were in fragmentation, reset state to avoid issues on next connection
        ws_frg_ctx->in_fragmentation = false;
        ws_frg_ctx->reassembled_len = 0;
        aux->ws_final = true;
        aux->ws_type = HTTPD_WS_TYPE_CLOSE;
        return ESP_OK;
    }

    LOGD(TAG, LOG_FMT("First byte received: 0x%02X"), first_byte);

    /* Decode the FIN flag and Opcode from the byte */
    aux->ws_final = (first_byte & HTTPD_WS_FIN_BIT) != 0;
    aux->ws_type = (first_byte & HTTPD_WS_OPCODE_BITS);
    LOGD(TAG, "httpd_ws_get_frame_type: aux->ws_type (decoded): %d (0x%02X)", aux->ws_type, aux->ws_type);

    /* Control frames must not be fragmented. Mask is NOT_FINAL & (CLOSE || PING || PONG). */
    if (!aux->ws_final && (aux->ws_type == HTTPD_WS_TYPE_PING || aux->ws_type == HTTPD_WS_TYPE_PONG || aux->ws_type == HTTPD_WS_TYPE_CLOSE)) {
        LOGW(TAG, LOG_FMT("Received fragmented control frame (type %d), protocol violation"), aux->ws_type);
        return ESP_ERR_INVALID_STATE;
    }

    if (ws_frg_ctx->in_fragmentation) {
        // Allow CONTINUATION and CONTROL frames (PING/PONG/CLOSE) during fragmentation as per RFC 6455 Section 5.4
        if (aux->ws_type != HTTPD_WS_TYPE_CONTINUE && !(aux->ws_type >= 0x8 && aux->ws_type <= 0xF)) {
            LOGW(TAG, LOG_FMT("Expected CONTINUATION or CONTROL frame but got (0x%x) during fragmentation"), aux->ws_type);
            ws_frg_ctx->in_fragmentation = false; // Reset fragmentation state
            ws_frg_ctx->reassembled_len = 0;
            // Send CLOSE frame with protocol error (1002) as per RFC 6455 Section 7.4.1
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 in network byte order
                .len = 2
            };
            httpd_ws_send_frame(req, &close_frame);
            return ESP_OK; // CLOSE frame sent, normal completion
        }
    } else {
        // Not in fragmentation, expect TEXT or BINARY (or control frame)
        if (aux->ws_type == HTTPD_WS_TYPE_CONTINUE) {
            LOGW(TAG, LOG_FMT("Received CONTINUATION frame (0x%x) but no active fragmentation"), aux->ws_type);
            // Send CLOSE frame with protocol error (1002) as per RFC 6455 Section 7.4.1
            httpd_ws_frame_t close_frame = {
                .final = true,
                .fragmented = false,
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = (uint8_t[]){0x03, 0xEA}, // Status code 1002 in network byte order
                .len = 2
            };
            httpd_ws_send_frame(req, &close_frame);
            return ESP_OK; // CLOSE frame sent, normal completion
        }

        // If it's a non-final TEXT or BINARY frame, start fragmentation
        if (!aux->ws_final && (aux->ws_type == HTTPD_WS_TYPE_TEXT || aux->ws_type == HTTPD_WS_TYPE_BINARY)) {
            ws_frg_ctx->in_fragmentation = true;
            // Store the initial frame type (TEXT or BINARY) for the reassembled message
            ws_frg_ctx->message_type = aux->ws_type;
            LOGD(TAG, LOG_FMT("Started fragmentation with message_type=%d"), ws_frg_ctx->message_type);
        }
    }

    LOGD(TAG, LOG_FMT("Fragmentation state: active=%d, length=%zu"), ws_frg_ctx->in_fragmentation, ws_frg_ctx->reassembled_len);

     /* If userspace requests control frames, do not deal with the control frames */
    if (!sd->ws_control_frames) {
        LOGD(TAG, LOG_FMT("Handler not requests control frames"));

        /* Reply to PING. For PONG and CLOSE, it will be handled elsewhere. */
        if (aux->ws_type == HTTPD_WS_TYPE_PING) {
            LOGD(TAG, LOG_FMT("Got a WS PING frame, Replying PONG..."));

            /* Read the rest of the PING frame, for PONG to reply back. */
            /* Please refer to RFC6455 Section 5.5.2 for more details */
            httpd_ws_frame_t frame;
            uint8_t frame_buf[128] = { 0 };
            memset(&frame, 0, sizeof(httpd_ws_frame_t));
            frame.payload = frame_buf;

            if (httpd_ws_recv_frame(req, &frame, 126) != ESP_OK) {
                LOGD(TAG, LOG_FMT("Cannot receive the full PING frame"));
                return ESP_ERR_INVALID_STATE;
            }

            /* Now turn the frame to PONG */
            frame.type = HTTPD_WS_TYPE_PONG;
            return httpd_ws_send_frame(req, &frame);
        } else if (aux->ws_type == HTTPD_WS_TYPE_CLOSE) {
            LOGD(TAG, LOG_FMT("Got a WS CLOSE frame, Replying CLOSE..."));

            /* Read the rest of the CLOSE frame and response */
            /* Please refer to RFC6455 Section 5.5.1 for more details */
            httpd_ws_frame_t frame;
            uint8_t frame_buf[128] = { 0 };
            memset(&frame, 0, sizeof(httpd_ws_frame_t));
            frame.payload = frame_buf;

            if (httpd_ws_recv_frame(req, &frame, 126) != ESP_OK) {
                LOGD(TAG, LOG_FMT("Cannot receive the full CLOSE frame"));
                return ESP_ERR_INVALID_STATE;
            }

            frame.len = 0;
            frame.type = HTTPD_WS_TYPE_CLOSE;
            frame.payload = NULL;
            return httpd_ws_send_frame(req, &frame);
        }
    }
    return ESP_OK;
}

httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t hd, int fd)
{
    struct sock_db *sess = httpd_sess_get(hd, fd);

    if (sess == NULL) {
        return HTTPD_WS_CLIENT_INVALID;
    }
    bool is_active_ws = sess->ws_handshake_done && (!sess->ws_close);
    return is_active_ws ? HTTPD_WS_CLIENT_WEBSOCKET : HTTPD_WS_CLIENT_HTTP;
}

static void httpd_ws_send_cb(void *arg)
{
    async_transfer_t *trans = arg;

    esp_err_t err = httpd_ws_send_frame_async(trans->handle, trans->socket, &trans->frame);

    if (trans->blocking) {
        event_group_set_bits(trans->transfer_done, err ? WS_SEND_FAILED : WS_SEND_OK);
    } else if (trans->callback) {
        trans->callback(err, trans->socket, trans->arg);
    }

    free(trans);
}

esp_err_t httpd_ws_send_data(httpd_handle_t handle, int socket, httpd_ws_frame_t *frame)
{
    async_transfer_t *transfer = calloc(1, sizeof(async_transfer_t));
    if (transfer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    event_group_handle_t transfer_done = event_group_create();
    if (!transfer_done) {
        free(transfer);
        return ESP_ERR_NO_MEM;
    }

    transfer->blocking = true;
    transfer->handle = handle;
    transfer->socket = socket;
    transfer->transfer_done = transfer_done;
    memcpy(&transfer->frame, frame, sizeof(httpd_ws_frame_t));

    esp_err_t err = httpd_queue_work(handle, httpd_ws_send_cb, transfer);
    if (err != ESP_OK) {
        event_group_delete(transfer_done);
        free(transfer);
        return err;
    }

    event_group_bits_t status = event_group_wait_bits(transfer_done, WS_SEND_OK | WS_SEND_FAILED,
                                             true, false, (uint32_t)-1);
    LOGD(TAG, "httpd_ws_send_data: Event group status for FD %d: 0x%lx", socket, status);

    event_group_delete(transfer_done);

    return (status & WS_SEND_OK) ? ESP_OK : (status & WS_SEND_FAILED ? ESP_FAIL : ESP_ERR_TIMEOUT); // Return timeout if neither OK nor FAILED
}

esp_err_t httpd_ws_send_data_async(httpd_handle_t handle, int socket, httpd_ws_frame_t *frame,
                                   transfer_complete_cb callback, void *arg)
{
    async_transfer_t *transfer = calloc(1, sizeof(async_transfer_t));
    if (transfer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    transfer->arg = arg;
    transfer->callback = callback;
    transfer->handle = handle;
    transfer->socket = socket;
    memcpy(&transfer->frame, frame, sizeof(httpd_ws_frame_t));

    esp_err_t err = httpd_queue_work(handle, httpd_ws_send_cb, transfer);

    if (err) {
        free(transfer);
        return err;
    }

    return ESP_OK;
}

#endif /* CONFIG_HTTPD_WS_SUPPORT */
