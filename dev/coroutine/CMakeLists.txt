project(Coroutine LANGUAGES CXX)


message(STATUS "")
message(STATUS "------- ${PROJECT_NAME} -------")

message(STATUS "DEV VERSION: ${PROJECT_VERSION}")
message(STATUS "GIT VERSION: ${PROJECT_GIT_VERSION}")
message(STATUS "TAG VERSION: ${PROJECT_TAG_VERSION}")
message(STATUS "BINARY_NAME: ${PROJECT_NAME}-${PROJECT_VERSION}")

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 遍历目录自动添加所有源文件
file(GLOB_RECURSE CPP_SOURCE_FILES
    CONFIGURE_DEPENDS

    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)


add_executable(${PROJECT_NAME} 
                ${CPP_SOURCE_FILES}
                ${VERSION_SOURCES})

#生成的可执行文件名称和包含的文件

target_include_directories(${PROJECT_NAME}  PRIVATE ${CMAKE_BINARY_DIR})

message(STATUS "BINARY_DIR : ${CMAKE_SOURCE_DIR}/bin")



target_link_libraries(${PROJECT_NAME} 
    casterbase
    castercore
    authverify
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
# target_link_libraries(${PROJECT_NAME} spdlog)
include_directories(${PROJECT_NAME} ${SPDLOG_INCLUDE_PATH})

# Ensure coroutine support is enabled
# target_link_libraries(${PROJECT_NAME} stdc++ stdc++-coroutines)
