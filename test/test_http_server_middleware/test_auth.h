#ifndef _TEST_AUTH_H_
#define _TEST_AUTH_H_


/**
 * @brief Test function for middleware authentication functionality
 *
 * Tests auth middleware behavior with various authorization headers,
 * missing headers, public endpoints, and edge cases.
 *
 * @return Test result (number of failed tests)
 */
int test_auth(void);


#endif // _TEST_AUTH_H_
