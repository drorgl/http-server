#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_auth.h"
#include "test_cors.h"
#include "test_framework.h"
#include "test_logging.h"

void setUp(void) {
    // Global test setup
}

void tearDown(void) {
    // Global test cleanup
}

int test_middleware() {
    return test_auth() + test_cors() + test_framework() + test_logging();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering for stdout
    setvbuf(stderr, NULL, _IONBF, 0); // Disable buffering for stderr
    return test_middleware();
}

void app_main(void) {
    main();
}
