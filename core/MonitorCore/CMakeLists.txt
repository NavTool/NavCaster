cmake_minimum_required(VERSION 3.21)

project(MonitorCore)

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)



#包含的源文件和头文件
include_directories(${PROJECT_NAME} ${CMAKE_SOURCE_DIR})
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/context)
include_directories(${PROJECT_NAME}  ${SPDLOG_INCLUDE_PATH})
include_directories(${PROJECT_NAME}  ${JSON_INCLUDE_PATH})

#静态库
include_directories(${PROJECT_NAME} ${CMAKE_BINARY_DIR})
set(CORE_SOURCES
   include/MonitorCore.h src/MonitorCore.cpp
   include/util.h

   include/context/ntrip_server.h
   include/context/ntrip_client.h
   include/context/auth_user.h


   include/excute/connectRedis.h
   include/excute/disconnectRedis.h

)
add_library(${PROJECT_NAME} STATIC ${CORE_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_options(${PROJECT_NAME} PRIVATE "/NODEFAULTLIB:library")
target_link_libraries(${PROJECT_NAME} 
    knt
    castercore
    authverify
    event
    event_core
    event_extra
)


add_subdirectory(test)


set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")
set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")
set_target_properties(${PROJECT_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")

message(STATUS "CMAKE_CXX_FLAGS_DEBUG   : ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE : ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_BUILD_TYPE        : ${CMAKE_BUILD_TYPE}")
 

