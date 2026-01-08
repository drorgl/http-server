#ifndef TEST_WEBSOCKET_SECURITY_H
#define TEST_WEBSOCKET_SECURITY_H

/**
 * @brief Main WebSocket security test runner function
 *
 * Runs all WebSocket security vulnerability tests required for RFC 6455 compliance.
 * These tests verify prevention of critical WebSocket vulnerabilities including:
 * - Cross-Site WebSocket Hijacking (CSWSH)
 * - Unmasked frame attacks
 * - Connection exhaustion attacks
 * - Fragment flooding attacks
 * - Missing authentication handshake protection
 *
 * This addresses the "WebSocket Security" gap identified in vulnerability analysis.
 *
 * @return 0 on success, non-zero on failure
 */
int test_websocket_security(void);

#endif /* TEST_WEBSOCKET_SECURITY_H */
