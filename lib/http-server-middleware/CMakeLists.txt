# Middleware library CMakeLists.txt
cmake_minimum_required(VERSION 3.5)

idf_component_register(
    SRCS "src/http_server_middleware.c" "src/middleware_logging.c" "src/middleware_auth.c" "src/middleware_cors.c" "src/middleware_websocket_xss.c" "src/xss_detector.c"
    INCLUDE_DIRS "include"
    REQUIRES http-server
)

# For cross-platform testing (when not on ESP platform)
if(NOT ESP_PLATFORM)
    # Simple static library for unit testing
    add_library(http-server-middleware STATIC
        src/http_server_middleware.c
        src/middleware_logging.c
        src/middleware_auth.c
        src/middleware_cors.c
        src/middleware_websocket_xss.c
        src/xss_detector.c
    )
    target_include_directories(http-server-middleware PUBLIC include)
    # Use PlatformIO auto-dependencies instead of relative paths
    target_link_libraries(http-server-middleware http-server)
endif()
