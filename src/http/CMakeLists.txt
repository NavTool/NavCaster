project(casterhttp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    http_server.h      http_server.cpp
    http_handler.h     http_handler.cpp
    cluster_monitor_service.h cluster_monitor_service.cpp
    connection_history_service.h connection_history_service.cpp
    controller_response.h
    controller_helpers.h
    mountpoint_subscriber_service.h mountpoint_subscriber_service.cpp
    node_log_level_service.h node_log_level_service.cpp
    node_history_service.h node_history_service.cpp
    audit_log_service.h audit_log_service.cpp
    account_controller.h account_controller.cpp
    access_controller.h access_controller.cpp
    alias_controller.h alias_controller.cpp
    config_controller.h config_controller.cpp
    relay_controller.h relay_controller.cpp
    redis_monitor_service.h redis_monitor_service.cpp
    runtime_command_service.h runtime_command_service.cpp
    runtime_state_controller.h runtime_state_controller.cpp
    sourcetable_service.h sourcetable_service.cpp
    statistics_service.h statistics_service.cpp
    status_service.h status_service.cpp
    source_controller.h source_controller.cpp
    system_event_service.h system_event_service.cpp
    redis_adapter.h    redis_adapter.cpp
    sse_manager.h      sse_manager.cpp
    sse_snapshot_service.h sse_snapshot_service.cpp
    ring_log_service.h ring_log_service.cpp
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

if(WIN32)
    target_link_libraries(${PROJECT_NAME} ws2_32)
endif()

#nlohmann/json
include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})
#spdlog
include_directories(${PROJECT_NAME} ${SPDLOG_INCLUDE_PATH})
