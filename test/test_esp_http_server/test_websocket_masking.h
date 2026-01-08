#ifndef _TEST_WEBSOCKET_MASKING_H_
#define _TEST_WEBSOCKET_MASKING_H_

/**
 * @brief WebSocket masking compliance test suite
 *
 * Tests RFC 6455 Section 5.3 WebSocket masking enforcement including:
 * - Server rejection of unmasked client frames (close code 1002)
 * - Proper acceptance and unmasking of masked client frames
 * - XOR algorithm correctness validation
 */
int test_websocket_masking(void);

#endif
