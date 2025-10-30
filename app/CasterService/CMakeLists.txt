project(Caster_Service LANGUAGES CXX)

# 默认版本号
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(VERSION_EXTRA 0)

find_package(Git)

if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE PROJECT_GIT_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(PROJECT_GIT_VERSION "unknown")
endif()

# 使用git describe命令获取标签版本号
execute_process(
    COMMAND git describe --tags --dirty=-dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PROJECT_TAG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 检查是否成功获取了标签版本
if(NOT PROJECT_TAG_VERSION)
    message(WARNING "Failed to get Git tag.")
else()
    # 使用正则表达式提取标签中的版本号和更新次数
    string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" VERSION_REGEX "${PROJECT_TAG_VERSION}")
    set(VERSION_MAJOR ${CMAKE_MATCH_1})
    set(VERSION_MINOR ${CMAKE_MATCH_2})
    set(VERSION_PATCH ${CMAKE_MATCH_3})
endif()

# 使用git describe命令获取更新次数
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE LATEST_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT LATEST_TAG)
    message(WARNING "Failed to get latest tag.")
else()
    # 使用git describe命令获取距离最近标签的更新次数
    execute_process(
        COMMAND git rev-list ${LATEST_TAG}..HEAD --count
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE UPDATE_COUNT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(VERSION_EXTRA ${UPDATE_COUNT})
endif()


# 设置项目的版本号
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_EXTRA}")

message(STATUS "")
message(STATUS "------- ${PROJECT_NAME} -------")

message(STATUS "DEV VERSION: ${PROJECT_VERSION}")
message(STATUS "GIT VERSION: ${PROJECT_GIT_VERSION}")
message(STATUS "TAG VERSION: ${PROJECT_TAG_VERSION}")
message(STATUS "BINARY_NAME: ${PROJECT_NAME}-${PROJECT_VERSION}")

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


#包含的源文件和头文件
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/Carrier)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/Compontent)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/Connector)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/DB)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/extra)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/extra/heart_beat)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/extra/license_check)

include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src)

 set(DB_TABLE_SOURCE
    include/DB/relay_account_tb.h
    src/DB/relay_account_tb.cpp
)
set(COMPONTENT_TYPE_SOURCE
    include/Compontent/process_queue.h
    src/Compontent/process_queue.cpp
)

set(CARRIER_TYPE_SOURCE
    include/Carrier/client_ntrip.h
    include/Carrier/server_ntrip.h
    # include/Carrier/server_relay.h
    src/Carrier/client_ntrip.cpp
    src/Carrier/server_ntrip.cpp
    # src/Carrier/server_relay.cpp
    include/Carrier/source_ntrip.h
    src/Carrier/source_ntrip.cpp
    include/Carrier/server_grid.h
    src/Carrier/server_grid.cpp
    include/Carrier/client_grid.h
    src/Carrier/client_grid.cpp
)

set(CONNECTOR_TYPE_SOURCE
    include/Connector/ntrip_compat_listener.h
    include/Connector/ntrip_relay_connector.h
    src/Connector/ntrip_compat_listener.cpp
    src/Connector/ntrip_relay_connector.cpp
)

set(EXTRA_SOURCE
    #心跳模块
    # extra/heart_beat/heart_beat.h
    # extra/heart_beat/heart_beat.cpp
    #注册码模块
    extra/license_check/license_check.h
    extra/license_check/license_check.cpp
    extra/license_check/register.h
    extra/license_check/register.cpp
)

set(MAIN_SOURCES
    include/ntrip_global.h
    include/ntrip_caster.h
    src/main.cpp
    src/ntrip_caster.cpp
)

add_executable(${PROJECT_NAME} 
                ${MAIN_SOURCES} 
                ${CARRIER_TYPE_SOURCE}  
                ${CONNECTOR_TYPE_SOURCE}
                ${COMPONTENT_TYPE_SOURCE}    
                ${DB_TABLE_SOURCE} 
                ${EXTRA_SOURCE}
                ${VERSION_SOURCES})

#生成的可执行文件名称和包含的文件

#基于version.h.in 生成 version.h
configure_file(${CMAKE_SOURCE_DIR}/.cmake/version.h.in version.h @ONLY)
target_include_directories(${PROJECT_NAME}  PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

#基于工程目录下的配置文件 生成 执行目录下的配置文件
set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
configure_file(${CMAKE_SOURCE_DIR}/.cmake/Auth_Verify.yml.in ${CMAKE_SOURCE_DIR}/bin/conf/Auth_Verify.yml @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/.cmake/Caster_Core.yml.in ${CMAKE_SOURCE_DIR}/bin/conf/Caster_Core.yml @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/.cmake/Service_Setting.yml.in ${CMAKE_SOURCE_DIR}/bin/conf/Service_Setting.yml @ONLY)
# configure_file(${CMAKE_SOURCE_DIR}/.cmake/redis.conf.in ${CMAKE_SOURCE_DIR}/bin/env/redis.conf @ONLY)

message(STATUS "BINARY_DIR : ${CMAKE_SOURCE_DIR}/bin")


# target_link_libraries(${PROJECT_NAME} openssl)

#Koro_NavTool
target_link_libraries(${PROJECT_NAME} knt)
#caster_core
target_link_libraries(${PROJECT_NAME} castercore)
#auth_verify
target_link_libraries(${PROJECT_NAME} authverify)

#libevent 
target_link_libraries(${PROJECT_NAME} event)
target_link_libraries(${PROJECT_NAME} event_core)
target_link_libraries(${PROJECT_NAME} event_extra)

#nlohmann/json
include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})
#spdlog
# target_link_libraries(${PROJECT_NAME} spdlog)
include_directories(${PROJECT_NAME} ${SPDLOG_INCLUDE_PATH})
#yaml
target_link_libraries(${PROJECT_NAME} yaml-cpp)