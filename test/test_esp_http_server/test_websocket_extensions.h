/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TEST_WEBSOCKET_EXTENSIONS_H_
#define _TEST_WEBSOCKET_EXTENSIONS_H_

/**
 * @brief Test WebSocket extensions infrastructure API changes
 */

/* Test API Design Changes */
void test_websocket_extensions_api_structure_changes();
void test_websocket_extensions_function_signature_update();
void test_websocket_extensions_backward_compatibility();

/* Test Internal Structures */
void test_websocket_extensions_internal_structures();

/* Test URI Registration with Extensions */
void test_websocket_extensions_uri_registration();

/* Test Core Functionality - Phase 2 */
void test_extension_header_parsing(void);
void test_multiple_extensions_parsing(void);
void test_extension_parameters_parsing(void);
void test_extension_negotiation_exact_match(void);
void test_extension_negotiation_no_match(void);
void test_extension_response_header_building(void);

/* Test Error Handling - Phase 2 */
void test_malformed_extension_header(void);
void test_extension_header_too_long(void);
void test_empty_or_missing_extensions(void);

/* Test Security Validation - Phase 4 */
void test_extension_security_validation(void);

/* Test Integration - Phase 4 */
void test_handshake_with_extensions_negotiated(void);
void test_handshake_without_extensions(void);
void test_handshake_no_common_extensions(void);

/* Test runner */
int test_websocket_extensions_api(void);
int test_websocket_extensions_parser(void);
int test_websocket_extensions_integration(void);

#endif /* _TEST_WEBSOCKET_EXTENSIONS_H_ */
