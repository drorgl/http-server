#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_auth.h"
#include "test_cors.h"
#include "test_framework.h"
#include "test_logging.h"
#include "test_range.h"
#include "test_conditional.h"
#include "test_content_negotiation.h"
#include "test_content_negotiation_algorithm.h"
#include "test_content_negotiation_integration.h"

void setUp(void) {
    // Global test setup
}

void tearDown(void) {
    // Global test cleanup
}

int test_middleware() {
    return 
        test_auth() | 
        test_cors() |
        test_framework() |
        test_logging() |
        test_range() | 
        test_conditional() |
        run_test_content_negotiation()|
        run_test_content_negotiation_algorithm() | 
        run_test_content_negotiation_integration();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering for stdout
    setvbuf(stderr, NULL, _IONBF, 0); // Disable buffering for stderr
    UNITY_BEGIN();
    test_middleware();
    return UNITY_END();
}

void app_main(void) {
    main();
}
