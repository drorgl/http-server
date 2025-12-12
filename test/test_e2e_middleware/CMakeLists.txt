idf_component_register(SRCS "test_e2e_middleware.c"
                       INCLUDE_DIRS "."
                       REQUIRES http-server http-server-middleware http-test-client unity)
