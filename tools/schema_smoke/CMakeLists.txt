cmake_minimum_required(VERSION 3.5)

project(schema_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/base/include
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/core/context
    ${CMAKE_SOURCE_DIR}/src/http
    ${CMAKE_SOURCE_DIR}/third_party/libevent/include
    ${CMAKE_BINARY_DIR}/third_party/libevent/include
)

target_sources(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/context/redis_keys.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_schema.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/audit_log_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/config_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/connection_history_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/node_history_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/system_event_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/source_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/alias_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/access_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/relay_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/runtime_state_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/http/account_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/audit_log_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/access_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/alias_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/config_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/connection_history_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/node_history_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/relay_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/ring_log_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/runtime_command_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/runtime_state_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/sourcetable_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/statistics_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/source_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/sse_snapshot_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/system_event_service.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE casterbase caster_proto libprotobuf)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})
