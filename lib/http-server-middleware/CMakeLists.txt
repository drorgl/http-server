# Middleware library CMakeLists.txt
cmake_minimum_required(VERSION 3.5)

idf_component_register(
    SRCS "src/http_server_middleware.c"
    INCLUDE_DIRS "include"
    REQUIRES http-server
)

# For cross-platform testing (when not on ESP platform)
if(NOT ESP_PLATFORM)
    # Simple static library for unit testing
    add_library(http-server-middleware STATIC
        src/http_server_middleware.c
    )
    target_include_directories(http-server-middleware PUBLIC include)
    target_include_directories(http-server-middleware PRIVATE ../http-server/include)
endif()
