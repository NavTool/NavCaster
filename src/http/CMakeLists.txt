project(casterhttp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    http_server.h      http_server.cpp
    http_handler.h     http_handler.cpp
    redis_adapter.h    redis_adapter.cpp
    sse_manager.h      sse_manager.cpp
    ring_log_view.h    ring_log_view.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(${PROJECT_NAME}
    casterbase
    castercore
    caster_proto
    event
    event_core
    event_extra
    hiredis
)

#nlohmann/json
include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})
#spdlog
include_directories(${PROJECT_NAME} ${SPDLOG_INCLUDE_PATH})
