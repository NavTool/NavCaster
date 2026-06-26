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
    ${CMAKE_SOURCE_DIR}/src/core/context/services
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories
    ${CMAKE_SOURCE_DIR}/src/http
    ${CMAKE_SOURCE_DIR}/src/service
    ${CMAKE_SOURCE_DIR}/src/auth/include
    ${CMAKE_SOURCE_DIR}/src/auth/src
    ${CMAKE_BINARY_DIR}
    ${SPDLOG_INCLUDE_PATH}
    ${CMAKE_SOURCE_DIR}/third_party/hiredis
    ${CMAKE_SOURCE_DIR}/third_party/yaml-cpp/include
    ${CMAKE_SOURCE_DIR}/third_party/libevent/include
    ${CMAKE_BINARY_DIR}/third_party/libevent/include
)

target_sources(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/context/context_util.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/redis_keys.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_domain.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_domain_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_schema.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories/audit_log_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories/cluster_monitor_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/config_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories/connection_history_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories/mountpoint_subscriber_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories/node_history_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories/system_event_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/source_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/alias_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/access_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/services/access_policy_service.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/services/source_table_service.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/services/relay_scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/services/master_lease_service.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/services/node_history_recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/relay_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/repositories/redis_monitor_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/runtime_state_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/http/account_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/audit_log_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/auth_session_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/access_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/alias_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/cluster_monitor_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/config_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/connection_history_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/mountpoint_subscriber_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/node_log_level_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/node_history_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/relay_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/redis_monitor_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/ring_log_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/runtime_command_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/runtime_state_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/sourcetable_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/statistics_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/statistics_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/status_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/source_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/http/sse_snapshot_service.cpp
    ${CMAKE_SOURCE_DIR}/src/http/system_event_service.cpp
    ${CMAKE_SOURCE_DIR}/src/service/ntrip_config.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE casterbase caster_proto libprotobuf yaml-cpp)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})

if(BUILD_TESTING)
    add_test(NAME schema_smoke COMMAND ${PROJECT_NAME})
endif()
