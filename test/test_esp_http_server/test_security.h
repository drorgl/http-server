#ifndef TEST_SECURITY_H
#define TEST_SECURITY_H

/**
 * @brief Main security test runner function
 *
 * Runs all security vulnerability tests required for RFC 9112 compliance.
 * These tests verify prevention of critical security vulnerabilities including:
 * - Response splitting attacks
 * - Header injection attacks
 * - CRLF injection protection
 * - Request smuggling prevention
 *
 * This addresses the "Security Features" gap identified in standards.md
 * for RFC 9112 Section 11 (Security Considerations).
 *
 * @return 0 on success, non-zero on failure
 */
int test_security(void);

#endif /* TEST_SECURITY_H */
