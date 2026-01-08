#ifndef _TEST_WEBSOCKET_EXTENSIONS_E2E_H_
#define _TEST_WEBSOCKET_EXTENSIONS_E2E_H_

/**
 * @brief End-to-End tests for WebSocket Extensions using http-test-client
 * This tests the complete integration of extension negotiation with real client-server connections
 */

/* E2E Test Functions */
void test_e2e_websocket_extensions_single_negotiation(void);
void test_e2e_websocket_extensions_multiple_negotiation(void);
void test_e2e_websocket_extensions_no_common_extensions(void);

/* Test runner */
int test_websocket_extensions_e2e(void);

#endif /* _TEST_WEBSOCKET_EXTENSIONS_E2E_H_ */
