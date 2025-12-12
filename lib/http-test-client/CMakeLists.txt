idf_component_register(SRCS "src/http_test_client.c"
                       INCLUDE_DIRS "include"
                       REQUIRES http-server logger base64 hashing http-parser)
