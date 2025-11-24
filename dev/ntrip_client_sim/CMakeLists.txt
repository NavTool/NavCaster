cmake_minimum_required(VERSION 3.5)

project(ntrip_client_sim_0.0.2 LANGUAGES CXX)

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#包含的源文件和头文件
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src)



set(MAIN_SOURCES
    include/ntrip_client.h
    src/main.cpp
    src/ntrip_client.cpp
)

add_executable(${PROJECT_NAME} ${MAIN_SOURCES})


#Koro_NavTool
target_link_libraries(${PROJECT_NAME} casterbase)

#Json-develop
include_directories(${PROJECT_NAME} ${JSON_INCLUDE_PATH})


#libevent 
target_link_libraries(${PROJECT_NAME} event)
target_link_libraries(${PROJECT_NAME} event_core)
target_link_libraries(${PROJECT_NAME} event_extra)