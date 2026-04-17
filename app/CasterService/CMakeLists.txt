project(CasterService LANGUAGES CXX)


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
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/Component)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/Connector)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/HttpServer)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/extra)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src)

# 遍历目录自动添加所有源文件
file(GLOB_RECURSE CPP_SOURCE_FILES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/**/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/**/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/extra/**/*.cpp

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


add_executable(${PROJECT_NAME} 
                ${CPP_SOURCE_FILES}
                ${VERSION_SOURCES})

#生成的可执行文件名称和包含的文件


target_include_directories(${PROJECT_NAME}  PRIVATE ${CMAKE_BINARY_DIR})

#基于工程目录下的配置文件 生成 执行目录下的配置文件
#设置可执行程序生成路径
# set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${PROJECT_NAME}")
#设置配置文件模板生成路径
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/Auth_Verify.yml.in ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/conf/Auth_Verify.yml @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/Caster_Core.yml.in ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/conf/Caster_Core.yml @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.cmake/Service_Setting.yml.in ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/conf/Service_Setting.yml @ONLY)
# configure_file(${CMAKE_SOURCE_DIR}/.cmake/redis.conf.in ${CMAKE_SOURCE_DIR}/bin/env/redis.conf @ONLY)



message(STATUS "BINARY_DIR : ${CMAKE_SOURCE_DIR}/bin")


# target_link_libraries(${PROJECT_NAME} openssl)

target_link_libraries(${PROJECT_NAME} 
    casterbase
    castercore
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
# target_link_libraries(${PROJECT_NAME} spdlog)
include_directories(${PROJECT_NAME} ${SPDLOG_INCLUDE_PATH})



set(REG_SOURCES
    test/reg_check.cpp
    extra/license_check/register.h
    extra/license_check/register.cpp
)
add_executable(reg_check ${REG_SOURCES}  )
include_directories(reg_check ${SPDLOG_INCLUDE_PATH})

set_target_properties(reg_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${PROJECT_NAME}/tool")
