idf_component_register(SRCS "ws_extensions_server.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server protocol_examples_common nvs_flash)
