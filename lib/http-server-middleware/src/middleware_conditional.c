/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "../include/middleware_conditional.h"
#include <log.h>

static const char *TAG = "middleware_conditional";

// Helper function for HTTP date parsing: check if year is leap year
static bool is_utc_leap_year(int year) {
    if (year % 4 != 0) return false;
    if (year % 100 != 0) return true;
    return year % 400 == 0;
}

// Calculate UTC timestamp from date components (since 1970-01-01 UTC)
// This avoids timezone issues and works correctly for HTTP dates (always GMT)
static long long calculate_utc_timestamp(int year, int mon, int mday, int hour, int min, int sec) {
    // Month days (non-leap year) - index 0=Jan, 1=Feb, etc.
    int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    long long days = 0;

    // Add days for full years since 1970
    for (int y = 1970; y < year; y++) {
        days += is_utc_leap_year(y) ? 366 : 365;
    }

    // Adjust February for leap year in current year
    if (is_utc_leap_year(year) && mon > 1) {
        month_days[1] = 29;
    }

    // Add days for full months in current year
    for (int m = 0; m < mon; m++) {
        days += month_days[m];
    }

    // Add days in current month (mday starts from 1, but we add day-1)
    days += mday - 1;

    // Calculate total seconds
    long long timestamp = days * 86400LL + hour * 3600LL + min * 60LL + sec;

    return timestamp;
}

// For testing - store test header values
static char test_if_match_header[256] = {0};
static char test_if_none_match_header[256] = {0};
static char test_if_modified_since_header[256] = {0};
static char test_if_unmodified_since_header[256] = {0};
static char test_if_range_header[256] = {0};

// Simple hash function for ETag generation
static unsigned int simple_hash(const char *data, size_t len) {
    unsigned int hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + data[i]; /* hash * 33 + c */
    }
    return hash;
}

void httpd_set_test_conditional_header(const char *header_name, const char *header_value) {
    if (!header_name) return;

    if (strcmp(header_name, "If-Match") == 0) {
        if (header_value) {
            strncpy(test_if_match_header, header_value, sizeof(test_if_match_header) - 1);
        } else {
            memset(test_if_match_header, 0, sizeof(test_if_match_header));
        }
    } else if (strcmp(header_name, "If-None-Match") == 0) {
        if (header_value) {
            strncpy(test_if_none_match_header, header_value, sizeof(test_if_none_match_header) - 1);
        } else {
            memset(test_if_none_match_header, 0, sizeof(test_if_none_match_header));
        }
    } else if (strcmp(header_name, "If-Modified-Since") == 0) {
        if (header_value) {
            strncpy(test_if_modified_since_header, header_value, sizeof(test_if_modified_since_header) - 1);
        } else {
            memset(test_if_modified_since_header, 0, sizeof(test_if_modified_since_header));
        }
    } else if (strcmp(header_name, "If-Unmodified-Since") == 0) {
        if (header_value) {
            strncpy(test_if_unmodified_since_header, header_value, sizeof(test_if_unmodified_since_header) - 1);
        } else {
            memset(test_if_unmodified_since_header, 0, sizeof(test_if_unmodified_since_header));
        }
    } else if (strcmp(header_name, "If-Range") == 0) {
        if (header_value) {
            strncpy(test_if_range_header, header_value, sizeof(test_if_range_header) - 1);
        } else {
            memset(test_if_range_header, 0, sizeof(test_if_range_header));
        }
    }
}

esp_err_t httpd_generate_strong_etag(const char *content, size_t content_len, char *etag, size_t etag_len) {
    if (!content || !etag || etag_len < 10) {
        return ESP_ERR_INVALID_ARG;
    }
    
    unsigned int hash = simple_hash(content, content_len);
    int written = snprintf(etag, etag_len, "\"%08x\"", hash);
    
    if (written < 0 || (size_t)written >= etag_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    return ESP_OK;
}

esp_err_t httpd_generate_weak_etag(long long timestamp, char *etag, size_t etag_len) {
    if (!etag || etag_len < 20) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int written = snprintf(etag, etag_len, "W/\"%lld\"", timestamp);
    
    if (written < 0 || (size_t)written >= etag_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    return ESP_OK;
}

esp_err_t httpd_parse_http_date(const char *date_str, long long *timestamp) {
    if (!date_str || !timestamp) {
        return ESP_ERR_INVALID_ARG;
    }

    // Parse HTTP date format: "Day, DD Mon YYYY HH:MM:SS GMT"
    // Example: "Thu, 31 Dec 2020 23:59:59 GMT"

    char day_name[4] = {0};
    int day, hour, min, sec, year;
    char month_name[4] = {0};
    char tz[4] = {0};

    int parsed = sscanf(date_str, "%3s, %d %3s %d %d:%d:%d %3s",
                        day_name, &day, month_name, &year, &hour, &min, &sec, tz);

    if (parsed != 8 || strcmp(tz, "GMT") != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Convert month name to number (0-based)
    int month = -1;
    const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_name, months[i]) == 0) {
            month = i;
            break;
        }
    }
    if (month == -1) {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate UTC timestamp directly (HTTP dates are always GMT)
    *timestamp = calculate_utc_timestamp(year, month, day, hour, min, sec);
    return ESP_OK;
}

esp_err_t httpd_format_http_date(long long timestamp, char *date_str, size_t date_len) {
    if (!date_str || date_len < 30) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // For simplicity, we'll just return a fixed format
    // In a real implementation, this would format according to RFC 7231
    time_t t = (time_t)timestamp;
    struct tm *tm_info = gmtime(&t);
    
    // Format as RFC 7231 IMF-fixdate
    int written = strftime(date_str, date_len, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    if (written == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    return ESP_OK;
}

// Parse ETag list from If-Match or If-None-Match header
static esp_err_t parse_etag_list(const char *header_value, char etags[][HTTPD_MAX_ETAG_LEN], size_t *etag_count, size_t max_etags) {
    if (!header_value || !etags || !etag_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *etag_count = 0;
    const char *ptr = header_value;
    
    // Handle special case: "*" matches any resource
    if (strcmp(ptr, "*") == 0) {
        strncpy(etags[0], "*", HTTPD_MAX_ETAG_LEN - 1);
        etags[0][HTTPD_MAX_ETAG_LEN - 1] = '\0';
        *etag_count = 1;
        return ESP_OK;
    }
    
    // Parse comma-separated ETags
    while (*ptr && *etag_count < max_etags) {
        // Skip whitespace
        while (*ptr && isspace(*ptr)) ptr++;
        
        if (!*ptr) break;
        
        // Find end of current ETag
        const char *start = ptr;
        while (*ptr && *ptr != ',') ptr++;
        
        size_t len = ptr - start;
        if (len > 0 && len < HTTPD_MAX_ETAG_LEN) {
            // Copy ETag, trimming whitespace
            const char *etag_start = start;
            const char *etag_end = start + len - 1;
            
            // Trim leading whitespace
            while (etag_start <= etag_end && isspace(*etag_start)) etag_start++;
            
            // Trim trailing whitespace
            while (etag_end > etag_start && isspace(*etag_end)) etag_end--;
            
            size_t etag_len = etag_end - etag_start + 1;
            if (etag_len > 0 && etag_len < HTTPD_MAX_ETAG_LEN) {
                memcpy(etags[*etag_count], etag_start, etag_len);
                etags[*etag_count][etag_len] = '\0';
                (*etag_count)++;
            }
        }
        
        // Skip comma
        if (*ptr == ',') ptr++;
    }
    
    return ESP_OK;
}

// Extract the opaque-tag from an ETag (removing W/ prefix if present)
static const char* get_opaque_tag(const char *etag) {
    if (!etag) return NULL;

    // Strong ETag: "opaque-tag"
    // Weak ETag: W/"opaque-tag"

    if (strncmp(etag, "W/\"", 3) == 0) {
        // Weak ETag: skip "W/" prefix
        return etag + 2;
    } else if (etag[0] == '"') {
        // Strong ETag: starts with quote
        return etag;
    }

    // Malformed ETag
    return NULL;
}

// Strong ETag comparison (RFC 9110 Section 8.8.3.2)
static bool etag_strong_match(const char *etag1, const char *etag2) {
    if (!etag1 || !etag2) return false;
    return strcmp(etag1, etag2) == 0;
}

// Weak ETag comparison (RFC 9110 Section 8.8.3.2)
static bool etag_weak_match(const char *etag1, const char *etag2) {
    const char *opaque1 = get_opaque_tag(etag1);
    const char *opaque2 = get_opaque_tag(etag2);

    if (!opaque1 || !opaque2) return false;
    return strcmp(opaque1, opaque2) == 0;
}

// Check if ETag matches any in the list using specified comparison mode
static bool etag_matches(const char *etag, char etags[][HTTPD_MAX_ETAG_LEN], size_t etag_count, bool use_weak_comparison) {
    if (!etag) return false;

    // Handle special case: "*" matches any resource
    if (etag_count == 1 && strcmp(etags[0], "*") == 0) {
        return true;
    }

    // Check if ETag matches any in the list
    for (size_t i = 0; i < etag_count; i++) {
        bool matches = use_weak_comparison ?
                       etag_weak_match(etag, etags[i]) :
                       etag_strong_match(etag, etags[i]);
        if (matches) {
            return true;
        }
    }

    return false;
}

// Compare timestamp with HTTP date for If-Modified-Since
static bool timestamp_matches_if_modified(long long timestamp, const char *http_date_str) {
    if (!http_date_str) return false;

    long long date_timestamp;
    if (httpd_parse_http_date(http_date_str, &date_timestamp) != ESP_OK) {
        return false;
    }

    // For If-Modified-Since: resource is not modified if timestamp is at or before the date
    // Allow small tolerance for clock skew
    return timestamp <= date_timestamp + 1;
}

// Compare timestamp with HTTP date for If-Unmodified-Since
static bool timestamp_matches_if_unmodified(long long timestamp, const char *http_date_str) {
    if (!http_date_str) return false;

    long long date_timestamp;
    if (httpd_parse_http_date(http_date_str, &date_timestamp) != ESP_OK) {
        return false;
    }

    // For If-Unmodified-Since: resource is unmodified if timestamp is at or before the date
    // Strict comparison - no tolerance, as per HTTP spec for this header
    return timestamp <= date_timestamp;
}

// Parse If-Range header value (RFC 9110 Section 13.1.5)
esp_err_t httpd_parse_if_range_header(const char *header_value, httpd_if_range_condition_t *condition) {
    if (!header_value || !condition) {
        return ESP_ERR_INVALID_ARG;
    }

    // If-Range = entity-tag / HTTP-date

    // Check if it's an ETag (starts with quote or W/)
    if (header_value[0] == '"' || strncmp(header_value, "W/\"", 3) == 0) {
        // It's an ETag
        condition->is_etag = true;
        size_t len = strlen(header_value);
        if (len >= HTTPD_MAX_ETAG_LEN) {
            return ESP_ERR_INVALID_SIZE;
        }
        strncpy(condition->etag, header_value, len);
        condition->etag[len] = '\0';
        condition->http_date = 0; // Not used for ETag
        return ESP_OK;
    } else {
        // Assume it's an HTTP-date
        condition->is_etag = false;
        memset(condition->etag, 0, sizeof(condition->etag)); // Not used for HTTP-date
        return httpd_parse_http_date(header_value, &condition->http_date);
    }
}

// Evaluate If-Range precondition against resource state
httpd_if_range_result_t httpd_evaluate_if_range_precondition(httpd_req_t *req,
                                                           const char *resource_etag,
                                                           long long resource_last_modified,
                                                           const httpd_if_range_condition_t *if_range) {
    if (!if_range) {
        LOGD(TAG, "If-Range precondition: invalid if_range, ignoring range");
        return HTTPD_IF_RANGE_IGNORE_RANGE;
    }

    if (if_range->is_etag) {
        // Compare ETag using weak comparison
        LOGD(TAG, "If-Range precondition: ETag comparison - resource_etag=%s, condition_etag=%s",
             resource_etag ? resource_etag : "NULL", if_range->etag);
        if (!resource_etag || !etag_weak_match(resource_etag, if_range->etag)) {
            LOGD(TAG, "If-Range precondition: ETag mismatch or no resource ETag, ignoring range");
            return HTTPD_IF_RANGE_IGNORE_RANGE;
        }
        LOGD(TAG, "If-Range precondition: ETag match, proceeding with range");
    } else {
        // Compare HTTP-date: if resource was modified after the condition date, ignore range
        LOGD(TAG, "If-Range precondition: HTTP-date comparison - resource_last_modified=%lld, condition_http_date=%lld",
             resource_last_modified, if_range->http_date);
        LOGD(TAG, "Hex dump resource_last_modified: %p", (void*)&resource_last_modified);
        LOGD(TAG, "Hex dump condition_http_date: %p", (void*)&if_range->http_date);
        if (resource_last_modified > if_range->http_date) {
            LOGD(TAG, "If-Range precondition: resource modified after condition date, ignoring range");
            return HTTPD_IF_RANGE_IGNORE_RANGE;
        }
        LOGD(TAG, "If-Range precondition: resource not modified after condition date, proceeding with range");
    }

    // Condition met - proceed with range request
    return HTTPD_IF_RANGE_PROCESS_RANGE;
}

esp_err_t middleware_conditional(httpd_req_t *req, const httpd_uri_t *uri, void *ctx) {
    if (!req || !ctx) {
        return ESP_OK; // Continue processing
    }
    
    httpd_conditional_middleware_config_t *config = (httpd_conditional_middleware_config_t *)ctx;
    
    // Get ETag for the resource if generator is provided
    char resource_etag[HTTPD_MAX_ETAG_LEN] = {0};
    bool has_etag = false;
    
    if (config->etag_generator) {
        if (config->etag_generator(req, resource_etag, sizeof(resource_etag)) == ESP_OK) {
            has_etag = true;
        }
    }
    
    // Get Last-Modified timestamp if function is provided
    long long last_modified = 0;
    bool has_last_modified = false;

    if (config->last_modified_fn) {
        if (config->last_modified_fn(req, &last_modified) == ESP_OK) {
            has_last_modified = true;
        }
    }

    // Check If-Range header (must be done before adding response headers)
    char if_range_value[256] = {0};
    bool has_if_range = false;
    bool should_skip_range = false;

    // Use test header if set, otherwise get from request
    if (strlen(test_if_range_header) > 0) {
        strncpy(if_range_value, test_if_range_header, sizeof(if_range_value) - 1);
        if_range_value[sizeof(if_range_value) - 1] = '\0';
        has_if_range = true;
    } else if (config->req_get_hdr_value_len ?
               config->req_get_hdr_value_len(req, "If-Range") > 0 :
               httpd_req_get_hdr_value_len(req, "If-Range") > 0) {
        esp_err_t ret = config->req_get_hdr_value_str ?
                        config->req_get_hdr_value_str(req, "If-Range", if_range_value, sizeof(if_range_value)) :
                        httpd_req_get_hdr_value_str(req, "If-Range", if_range_value, sizeof(if_range_value));
        if (ret == ESP_OK) {
            has_if_range = true;
        }
    }

    if (has_if_range) {
        httpd_if_range_condition_t if_range_condition;
        if (httpd_parse_if_range_header(if_range_value, &if_range_condition) == ESP_OK) {
            httpd_if_range_result_t result = httpd_evaluate_if_range_precondition(req, resource_etag, last_modified, &if_range_condition);
            if (result == HTTPD_IF_RANGE_IGNORE_RANGE) {
                should_skip_range = true;
            }
        }
    }

    // Store skip_range flag for range middleware (using a simple approach - modify req user_ctx)
    // In a real implementation, this would be a more structured approach
    if (should_skip_range) {
        // Set a flag in the request that range middleware can check
        // For now, we'll use a simple pointer hack - this should be improved
        req->user_ctx = (void*)0x1; // Non-null indicates skip range
    }
    
    // Check If-Match header
    char if_match_value[256] = {0};
    bool has_if_match = false;
    
    // Use test header if set, otherwise get from request
    if (strlen(test_if_match_header) > 0) {
        strncpy(if_match_value, test_if_match_header, sizeof(if_match_value) - 1);
        if_match_value[sizeof(if_match_value) - 1] = '\0';
        has_if_match = true;
    } else if (config->req_get_hdr_value_len ? 
               config->req_get_hdr_value_len(req, "If-Match") > 0 :
               httpd_req_get_hdr_value_len(req, "If-Match") > 0) {
        esp_err_t ret = config->req_get_hdr_value_str ?
                        config->req_get_hdr_value_str(req, "If-Match", if_match_value, sizeof(if_match_value)) :
                        httpd_req_get_hdr_value_str(req, "If-Match", if_match_value, sizeof(if_match_value));
        if (ret == ESP_OK) {
            has_if_match = true;
        }
    }
    
    if (has_if_match) {
        char etags[10][HTTPD_MAX_ETAG_LEN];
        size_t etag_count = 0;
        
        if (parse_etag_list(if_match_value, etags, &etag_count, 10) == ESP_OK) {
            // If no ETag for resource, condition cannot be met
            if (!has_etag) {
                // Send 412 Precondition Failed
                esp_err_t ret = config->resp_set_status ?
                                config->resp_set_status(req, "412 Precondition Failed") :
                                httpd_resp_set_status(req, "412 Precondition Failed");
                if (ret == ESP_OK) {
                    ret = config->resp_send ?
                          config->resp_send(req, NULL, 0) :
                          httpd_resp_send(req, NULL, 0);
                }
                return ESP_FAIL; // Stop processing
            }
            
            // Check if resource ETag matches any in If-Match list (strong comparison only)
            if (!etag_matches(resource_etag, etags, etag_count, false)) {
                // Send 412 Precondition Failed
                esp_err_t ret = config->resp_set_status ?
                                config->resp_set_status(req, "412 Precondition Failed") :
                                httpd_resp_set_status(req, "412 Precondition Failed");
                if (ret == ESP_OK) {
                    ret = config->resp_send ?
                          config->resp_send(req, NULL, 0) :
                          httpd_resp_send(req, NULL, 0);
                }
                return ESP_FAIL; // Stop processing
            }
        }
    }
    
    // Check If-Unmodified-Since header
    char if_unmodified_since_value[256] = {0};
    bool has_if_unmodified_since = false;
    
    // Use test header if set, otherwise get from request
    if (strlen(test_if_unmodified_since_header) > 0) {
        strncpy(if_unmodified_since_value, test_if_unmodified_since_header, sizeof(if_unmodified_since_value) - 1);
        if_unmodified_since_value[sizeof(if_unmodified_since_value) - 1] = '\0';
        has_if_unmodified_since = true;
    } else if (config->req_get_hdr_value_len ? 
               config->req_get_hdr_value_len(req, "If-Unmodified-Since") > 0 :
               httpd_req_get_hdr_value_len(req, "If-Unmodified-Since") > 0) {
        esp_err_t ret = config->req_get_hdr_value_str ?
                        config->req_get_hdr_value_str(req, "If-Unmodified-Since", if_unmodified_since_value, sizeof(if_unmodified_since_value)) :
                        httpd_req_get_hdr_value_str(req, "If-Unmodified-Since", if_unmodified_since_value, sizeof(if_unmodified_since_value));
        if (ret == ESP_OK) {
            has_if_unmodified_since = true;
        }
    }
    
    if (has_if_unmodified_since && has_last_modified) {
        if (!timestamp_matches_if_unmodified(last_modified, if_unmodified_since_value)) {
            // Send 412 Precondition Failed
            esp_err_t ret = config->resp_set_status ?
                            config->resp_set_status(req, "412 Precondition Failed") :
                            httpd_resp_set_status(req, "412 Precondition Failed");
            if (ret == ESP_OK) {
                ret = config->resp_send ?
                      config->resp_send(req, NULL, 0) :
                      httpd_resp_send(req, NULL, 0);
            }
            return ESP_FAIL; // Stop processing
        }
    }
    
    // Check If-None-Match header
    char if_none_match_value[256] = {0};
    bool has_if_none_match = false;
    
    // Use test header if set, otherwise get from request
    if (strlen(test_if_none_match_header) > 0) {
        strncpy(if_none_match_value, test_if_none_match_header, sizeof(if_none_match_value) - 1);
        if_none_match_value[sizeof(if_none_match_value) - 1] = '\0';
        has_if_none_match = true;
    } else if (config->req_get_hdr_value_len ? 
               config->req_get_hdr_value_len(req, "If-None-Match") > 0 :
               httpd_req_get_hdr_value_len(req, "If-None-Match") > 0) {
        esp_err_t ret = config->req_get_hdr_value_str ?
                        config->req_get_hdr_value_str(req, "If-None-Match", if_none_match_value, sizeof(if_none_match_value)) :
                        httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match_value, sizeof(if_none_match_value));
        if (ret == ESP_OK) {
            has_if_none_match = true;
        }
    }
    
    if (has_if_none_match) {
        char etags[10][HTTPD_MAX_ETAG_LEN];
        size_t etag_count = 0;
        
        if (parse_etag_list(if_none_match_value, etags, &etag_count, 10) == ESP_OK) {
            bool match_found = false;
            
            // Handle special case: "*" matches any resource
            if (etag_count == 1 && strcmp(etags[0], "*") == 0) {
                match_found = true;
            } else if (has_etag) {
                // Check if resource ETag matches any in If-None-Match list
                // RFC 9110: Use weak comparison for GET/HEAD, strong for other methods
                bool use_weak = (req->method == HTTP_GET || req->method == HTTP_HEAD);
                match_found = etag_matches(resource_etag, etags, etag_count, use_weak);
            }
            
            if (match_found) {
                // For GET/HEAD requests, send 304 Not Modified
                // For other methods, send 412 Precondition Failed
                if (req->method == HTTP_GET || req->method == HTTP_HEAD) {
                    // Send 304 Not Modified
                    esp_err_t ret = config->resp_set_status ?
                                    config->resp_set_status(req, "304 Not Modified") :
                                    httpd_resp_set_status(req, "304 Not Modified");
                    
                    // Add ETag header if available
                    if (ret == ESP_OK && has_etag) {
                        ret = config->resp_set_hdr ?
                              config->resp_set_hdr(req, "ETag", resource_etag) :
                              httpd_resp_set_hdr(req, "ETag", resource_etag);
                    }
                    
                    // Add Last-Modified header if available
                    if (ret == ESP_OK && has_last_modified) {
                        char last_modified_str[64];
                        if (httpd_format_http_date(last_modified, last_modified_str, sizeof(last_modified_str)) == ESP_OK) {
                            ret = config->resp_set_hdr ?
                                  config->resp_set_hdr(req, "Last-Modified", last_modified_str) :
                                  httpd_resp_set_hdr(req, "Last-Modified", last_modified_str);
                        }
                    }
                    
                    if (ret == ESP_OK) {
                        ret = config->resp_send ?
                              config->resp_send(req, NULL, 0) :
                              httpd_resp_send(req, NULL, 0);
                    }
                    return ESP_FAIL; // Stop processing
                } else {
                    // For other methods, send 412 Precondition Failed
                    esp_err_t ret = config->resp_set_status ?
                                    config->resp_set_status(req, "412 Precondition Failed") :
                                    httpd_resp_set_status(req, "412 Precondition Failed");
                    if (ret == ESP_OK) {
                        ret = config->resp_send ?
                              config->resp_send(req, NULL, 0) :
                              httpd_resp_send(req, NULL, 0);
                    }
                    return ESP_FAIL; // Stop processing
                }
            }
        }
    }
    
    // Check If-Modified-Since header (only for GET/HEAD requests)
    if (req->method == HTTP_GET || req->method == HTTP_HEAD) {
        char if_modified_since_value[256] = {0};
        bool has_if_modified_since = false;
        
        // Use test header if set, otherwise get from request
        if (strlen(test_if_modified_since_header) > 0) {
            strncpy(if_modified_since_value, test_if_modified_since_header, sizeof(if_modified_since_value) - 1);
            if_modified_since_value[sizeof(if_modified_since_value) - 1] = '\0';
            has_if_modified_since = true;
        } else if (config->req_get_hdr_value_len ? 
                   config->req_get_hdr_value_len(req, "If-Modified-Since") > 0 :
                   httpd_req_get_hdr_value_len(req, "If-Modified-Since") > 0) {
            esp_err_t ret = config->req_get_hdr_value_str ?
                            config->req_get_hdr_value_str(req, "If-Modified-Since", if_modified_since_value, sizeof(if_modified_since_value)) :
                            httpd_req_get_hdr_value_str(req, "If-Modified-Since", if_modified_since_value, sizeof(if_modified_since_value));
            if (ret == ESP_OK) {
                has_if_modified_since = true;
            }
        }
        
        if (has_if_modified_since && has_last_modified) {
            if (timestamp_matches_if_modified(last_modified, if_modified_since_value)) {
                // Send 304 Not Modified
                esp_err_t ret = config->resp_set_status ?
                                config->resp_set_status(req, "304 Not Modified") :
                                httpd_resp_set_status(req, "304 Not Modified");

                // Add Last-Modified header
                if (ret == ESP_OK) {
                    char last_modified_str[64];
                    if (httpd_format_http_date(last_modified, last_modified_str, sizeof(last_modified_str)) == ESP_OK) {
                        ret = config->resp_set_hdr ?
                              config->resp_set_hdr(req, "Last-Modified", last_modified_str) :
                              httpd_resp_set_hdr(req, "Last-Modified", last_modified_str);
                    }
                }

                // Add ETag header if available
                if (ret == ESP_OK && has_etag) {
                    ret = config->resp_set_hdr ?
                          config->resp_set_hdr(req, "ETag", resource_etag) :
                          httpd_resp_set_hdr(req, "ETag", resource_etag);
                }

                if (ret == ESP_OK) {
                    ret = config->resp_send ?
                          config->resp_send(req, NULL, 0) :
                          httpd_resp_send(req, NULL, 0);
                }
                return ESP_FAIL; // Stop processing
            }
        }
    }
    
    // If we have an ETag, add it to the response headers for future conditional requests
    if (has_etag) {
        config->resp_set_hdr ?
        config->resp_set_hdr(req, "ETag", resource_etag) :
        httpd_resp_set_hdr(req, "ETag", resource_etag);
    }
    
    // If we have Last-Modified, add it to the response headers
    if (has_last_modified) {
        char last_modified_str[64];
        if (httpd_format_http_date(last_modified, last_modified_str, sizeof(last_modified_str)) == ESP_OK) {
            config->resp_set_hdr ?
            config->resp_set_hdr(req, "Last-Modified", last_modified_str) :
            httpd_resp_set_hdr(req, "Last-Modified", last_modified_str);
        }
    }
    
    return ESP_OK; // Continue processing
}
