project(CasterService LANGUAGES CXX)

message(STATUS "")
message(STATUS "------- ${PROJECT_NAME} -------")
message(STATUS "DEV VERSION: ${PROJECT_VERSION}")
message(STATUS "GIT VERSION: ${PROJECT_GIT_VERSION}")
message(STATUS "TAG VERSION: ${PROJECT_TAG_VERSION}")
message(STATUS "BINARY_NAME: ${PROJECT_NAME}-${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 遍历目录自动添加所有源文件
file(GLOB_RECURSE CPP_SOURCE_FILES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/session/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/**/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/**/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/**/*.cpp
)

# 排除 test/ 目录
list(FILTER CPP_SOURCE_FILES EXCLUDE REGEX ".*/test/.*")

add_executable(${PROJECT_NAME}
    ${CPP_SOURCE_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/session
    ${CMAKE_CURRENT_SOURCE_DIR}/extra
    ${CMAKE_BINARY_DIR}
)

# 配置文件生成
configure_file(${CMAKE_SOURCE_DIR}/cmake/Auth_Verify.yml.in ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/conf/Auth_Verify.yml @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/Caster_Core.yml.in ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/conf/Caster_Core.yml @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/Service_Setting.yml.in ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/conf/Service_Setting.yml @ONLY)

message(STATUS "BINARY_DIR : ${CMAKE_SOURCE_DIR}/bin")

target_link_libraries(${PROJECT_NAME}
    casterbase
    castercore
    casterhttp
    authverify
    hiredis
    event
    event_extra
    event_core
    yaml-cpp
    libprotobuf
    caster_proto
)

#nlohmann/json
include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})
#spdlog
include_directories(${PROJECT_NAME} ${SPDLOG_INCLUDE_PATH})

# reg_check tool
set(REG_SOURCES
    test/reg_check.cpp
    extra/license_check/register.h
    extra/license_check/register.cpp
)
add_executable(reg_check ${REG_SOURCES})
target_include_directories(reg_check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extra
)
include_directories(reg_check ${SPDLOG_INCLUDE_PATH})
set_target_properties(reg_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${PROJECT_NAME}/tool")
