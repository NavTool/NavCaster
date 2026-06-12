cmake_minimum_required(VERSION 3.5)

project(schema_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(${PROJECT_NAME}
    main.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/context
    ${CMAKE_SOURCE_DIR}/src/http
)

target_sources(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/context/redis_keys.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_schema.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/account_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/config_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/source_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/alias_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/access_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/relay_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/context/runtime_state_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/http/config_controller.cpp
)

include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})
