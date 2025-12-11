#ifndef _TEST_AUTHENTICATION_H_
#define _TEST_AUTHENTICATION_H_


/**
 * @brief Test function for HTTP authentication functionality
 *
 * This test suite covers RFC 9110 Part 11 HTTP Authentication features:
 * - WWW-Authenticate header field (Section 11.6.1)
 * - Authorization header field (Section 11.6.2)
 * - Authentication-Info header field (Section 11.6.3)
 * - Proxy-Authenticate/Proxy-Authorization (Section 11.7)
 * - Authentication scheme extensibility (Section 16.4)
 *
 * @return Test result (0 on success)
 */
int test_authentication(void);


#endif // _TEST_AUTHENTICATION_H_
