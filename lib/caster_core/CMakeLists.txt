cmake_minimum_required(VERSION 3.5)

project(castercore LANGUAGES CXX)
# project(qpidclient LANGUAGES CXX)

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
elseif(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
endif()

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    #message(STATUS "This is the top-level source directory.")
    add_subdirectory(rely/)
else()
    #message(STATUS "This is a subdirectory.")
endif()

include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/context)

# 遍历目录自动添加所有源文件
file(GLOB_RECURSE CPP_SOURCE_FILES
    CONFIGURE_DEPENDS

    ${CMAKE_CURRENT_SOURCE_DIR}/context/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/context/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/context/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/context/**/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/context/**/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/context/**/*.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/**/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/**/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/**/*.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp
)


add_library(${PROJECT_NAME} STATIC ${CPP_SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME}  PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} casterbase)
target_link_libraries(${PROJECT_NAME} caster_proto)

#libevent 
target_link_libraries(${PROJECT_NAME} event)
target_link_libraries(${PROJECT_NAME} event_core)
target_link_libraries(${PROJECT_NAME} event_extra)

#hiredis 
target_link_libraries(${PROJECT_NAME} hiredis)

#yaml
target_link_libraries(${PROJECT_NAME} yaml-cpp)

#nlohmann/json
include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})

#spdlog
add_definitions(-DUSE_SPD_LOG)
include_directories(${PROJECT_NAME} ${SPDLOG_INCLUDE_PATH})








