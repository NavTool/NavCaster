cmake_minimum_required(VERSION 3.21)
project(NavCaster)

set(CMAKE_POLICY_VERSION_MINIMUM 3.21)

option(BUILD_CASTER_SERVICE "Build Caster Service." ON)
option(HOTLOAD_ENABLED "Enable hot reload" OFF)

include(CTest)

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 这会为所有配置设置路径
foreach(CONFIG_TYPE IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
endforeach()

# # 启用全程序优化和链接时间代码生成
# if(MSVC)
#     add_compile_options(/GL)  # 编译时启用全程序优化
#     add_link_options(/LTCG)   # 链接时启用链接时间代码生成
# endif()
if (MSVC)
    add_compile_options(/utf-8)
    # 启用 C++ 异常展开（针对 MSVC 编译器）
    add_compile_options(/EHsc)
    add_compile_options(/bigobj)

    add_definitions(-DNOMINMAX)    # 禁用 windows.h 定义的 min/max 宏

else()
    # set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    # add_definitions(-D__USE_TIME_BITS64)

endif()



# 设置默认版本号
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(VERSION_EXTRA 0)


# -------------------------设置软件基本信息-------------------------------------------------
# 设置可执行程序名称
set(PROJECT_COMPANY "NavTool")
set(PROJECT_COPYRIGHT "Copyright (c) 2026 NavTool's Lab. All rights reserved.")
set(PROJECT_DOMAIN "https://github.com/NavTool/NavTool")
set(PROJECT_DESCRIPTION "A Navigation Tool")

# 设置编构建时间
string(TIMESTAMP PROJECT_BUILD_DATE "%Y-%m-%d")

#----------------------使用git describe命令获取标签版本号 -------------------------------------------
find_package(Git)

if(GIT_FOUND)
    execute_process(
        COMMAND git rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(PROJECT_GIT_BRANCH "unknown")
endif()

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

    execute_process(
        COMMAND git log -1 --format=%cd --date=iso
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE PROJECT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

# 设置项目的版本号
set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_EXTRA}")



# 获取编译器信息
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (MINGW)
        set(Compiler "MinGW ${CMAKE_CXX_COMPILER_VERSION}")
    else()
        set(Compiler "GCC ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(Compiler "MSVC ${CMAKE_CXX_COMPILER_VERSION}")
    add_definitions(-DWIN32)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(Compiler "Clang ${CMAKE_CXX_COMPILER_VERSION}")
else()
    set(Compiler "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()


# 获取系统架构
set(Arch ${CMAKE_SYSTEM_PROCESSOR})

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(WINDOWS_X64_PLATFORM TRUE)
    set(SYSTEM_PLATFORM "WINDOWS_X64_PLATFORM")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
     set(LINUX_X64_PLATFORM TRUE)
     set(SYSTEM_PLATFORM "LINUX_X64_PLATFORM")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(LINUX_ARM_PLATFORM TRUE)
    set(SYSTEM_PLATFORM "LINUX_ARM_PLATFORM")
endif()

#------------------------------------------------------------------------------



#abseil-cpp
if(MSVC)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "build shared libs" FORCE)
endif()
set(ABSL_ENABLE_INSTALL ON CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE INTERNAL "")
add_subdirectory(third_party/abseil-cpp/)

#protobuf
if(MSVC)
    set(protobuf_BUILD_SHARED_LIBS ON CACHE BOOL "build shared libs" FORCE)
endif()
set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "Prevent downloading any dependencies from GitHub. If this option is set, the dependency must be available locally as an installed package." FORCE)
add_subdirectory(third_party/protobuf/)
if(MSVC)
    target_compile_options(libupb PRIVATE /wd5287)    # 忽略编译libupb时候的警告
endif()

include_directories(${CMAKE_SOURCE_DIR}/proto/src)
add_subdirectory(proto)



#libevent option
# set(EVENT_LIBRARY_TYPE STATIC)
set(EVENT__LIBRARY_TYPE STATIC CACHE STRING "Set library type to STATIC" FORCE)
set(EVENT__DISABLE_OPENSSL ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_TESTS ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_REGRESS ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "force set this option" FORCE)
add_subdirectory(third_party/libevent)

#hiredis option
set(BUILD_SHARED_LIBS OFF CACHE BOOL "force set this option"  FORCE)
set(DISABLE_TESTS ON CACHE BOOL "force set this option"  FORCE)
add_subdirectory(third_party/hiredis)

#yaml-cpp option
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "force set this option"  FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "force set this option"  FORCE)
add_subdirectory(third_party/yaml-cpp)

#json
set(JSON_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/third_party/json/include")

#spdlog
set(SPDLOG_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")

#rtklib
add_subdirectory(third_party/rtklib)


add_subdirectory(src)
add_subdirectory(tools)






#基于version.h.in 生成 version.h
configure_file(${CMAKE_SOURCE_DIR}/cmake/version.h.in ${CMAKE_BINARY_DIR}/version.h @ONLY)



message("---------------------------- NavTool ----------------------------")
message("Enable hot reload   : ${HOTLOAD_ENABLED}")
message("Build Caster Service: ${BUILD_CASTER_SERVICE}")
message("------------------------------------------------------------------")




# -------------------------输出软件的定义-------------------------------------------------
message(STATUS "")
message(STATUS "---------------- ${PROJECT_NAME} ----------------------")
message(STATUS "PROJECT_NAME          : ${PROJECT_NAME}")
message(STATUS "PROJECT_VERSION       : ${PROJECT_VERSION}")
message(STATUS "PROJECT_GIT_BRANCH    : ${PROJECT_GIT_BRANCH}")
message(STATUS "PROJECT_GIT_VERSION   : ${PROJECT_GIT_VERSION}")
message(STATUS "PROJECT_TAG_VERSION   : ${PROJECT_TAG_VERSION}")
message(STATUS "PROJECT_COMMIT_DATE   : ${PROJECT_COMMIT_DATE}")
message(STATUS "")
message(STATUS "CMAKE_SYSTEM          : ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "SYSTEM_PLATFORM       : ${SYSTEM_PLATFORM}")
message(STATUS "CMAKE_BUILD_TYPE      : ${CMAKE_BUILD_TYPE}")
message(STATUS "PROJECT_BUILD_DATE    : ${PROJECT_BUILD_DATE}")
message(STATUS "")
message(STATUS "PROJECT_COMPANY       : ${PROJECT_COMPANY}")
message(STATUS "PROJECT_COPYRIGHT     : ${PROJECT_COPYRIGHT}")
message(STATUS "PROJECT_DOMAIN        : ${PROJECT_DOMAIN}")
message(STATUS "PROJECT_DESCRIPTION   : ${PROJECT_DESCRIPTION}")
message(STATUS "")

message(STATUS "CMAKE_CXX_FLAGS_DEBUG   : ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE : ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_BUILD_TYPE        : ${CMAKE_BUILD_TYPE}")



