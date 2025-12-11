#ifndef _TEST_CORS_H_
#define _TEST_CORS_H_


/**
 * @brief Test function for middleware CORS functionality
 *
 * Tests CORS middleware with allowed origins, preflight OPTIONS,
 * wildcard origins, rejected origins, and no Origin headers.
 *
 * @return Test result (number of failed tests)
 */
int test_cors(void);


#endif // _TEST_CORS_H_
