cmake_minimum_required(VERSION 3.21)

project(NavCaster)



# #版本14以下可能会导致abseil不支持
# set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # 这会为所有配置设置路径
# foreach(CONFIG_TYPE IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
#     string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
#     set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
#     set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
#     set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
# endforeach()

# # 设置 Abseil 的选项
# set(ABSL_PROPAGATE_CXX_STD ON CACHE INTERNAL "")

# option(FLUENTUI_BUILD_STATIC_LIB "Build FluentUI static library." ON)
# option(FLUENTUI_UNIVERSAL_BUILD_MACOS "Universal build FluentUI for macOS." ON)
# set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "force set this option"  FORCE)
# set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "force set this option"  FORCE)

# # # 启用全程序优化和链接时间代码生成
# # if(MSVC)
# #     add_compile_options(/GL)  # 编译时启用全程序优化
# #     add_link_options(/LTCG)   # 链接时启用链接时间代码生成
# # endif()
# if (MSVC)
#     add_compile_options(/utf-8)
#     # 启用 C++ 异常展开（针对 MSVC 编译器）
#     add_compile_options(/EHsc)
#     add_compile_options(/bigobj)

#     add_definitions(-DNOMINMAX)    # 禁用 windows.h 定义的 min/max 宏

# else()
#     # set(CMAKE_POSITION_INDEPENDENT_CODE ON)
#     # add_definitions(-D__USE_TIME_BITS64)

# endif()

# if(APPLE AND FLUENTUI_UNIVERSAL_BUILD_MACOS)
#     set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
# else()
#     set(FLUENTUI_UNIVERSAL_BUILD_MACOS OFF)
# endif()

# message("---------------------------- FluentUI ----------------------------")
# message("Build FluentUI static library: ${FLUENTUI_BUILD_STATIC_LIB}")
# message("Universal build FluentUI for macOS: ${FLUENTUI_UNIVERSAL_BUILD_MACOS}")
# message("------------------------------------------------------------------")

# option(HOTLOAD_ENABLED "Enable hot reload" OFF)
# message("---------------------------- NavTool ----------------------------")
# message("Enable hot reload: ${HOTLOAD_ENABLED}")
# message("------------------------------------------------------------------")

# # 设置默认版本号
# set(VERSION_MAJOR 1)
# set(VERSION_MINOR 0)
# set(VERSION_PATCH 0)
# set(VERSION_EXTRA 0)


# # -------------------------设置软件基本信息-------------------------------------------------
# # 设置可执行程序名称
# set(PROJECT_COMPANY "NavCaster")
# set(PROJECT_COPYRIGHT "Copyright (c) 2025 NavTool's Lab. All rights reserved.")
# set(PROJECT_DOMAIN "https://github.com/NavTool/NavCaster")
# set(PROJECT_DESCRIPTION "A Navigation Tool")

# # 设置编构建时间
# string(TIMESTAMP PROJECT_BUILD_DATE "%Y-%m-%d")

# #----------------------使用git describe命令获取标签版本号 -------------------------------------------
# find_package(Git)

# if(GIT_FOUND)
#     execute_process(
#         COMMAND git rev-parse --abbrev-ref HEAD
#         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#         OUTPUT_VARIABLE PROJECT_GIT_BRANCH
#         OUTPUT_STRIP_TRAILING_WHITESPACE
#     )
# else()
#     set(PROJECT_GIT_BRANCH "unknown")
# endif()

# if(GIT_FOUND)
#     execute_process(
#       COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
#       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#       OUTPUT_VARIABLE PROJECT_GIT_VERSION
#       OUTPUT_STRIP_TRAILING_WHITESPACE
#     )
# else()
#     set(PROJECT_GIT_VERSION "unknown")
# endif()

# execute_process(
#     COMMAND git describe --tags --dirty=-dev
#     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#     OUTPUT_VARIABLE PROJECT_TAG_VERSION
#     OUTPUT_STRIP_TRAILING_WHITESPACE
# )

# # 检查是否成功获取了标签版本
# if(NOT PROJECT_TAG_VERSION)
#     message(WARNING "Failed to get Git tag.")
# else()
#     # 使用正则表达式提取标签中的版本号和更新次数
#     string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" VERSION_REGEX "${PROJECT_TAG_VERSION}")
#     set(VERSION_MAJOR ${CMAKE_MATCH_1})
#     set(VERSION_MINOR ${CMAKE_MATCH_2})
#     set(VERSION_PATCH ${CMAKE_MATCH_3})
# endif()

# # 使用git describe命令获取更新次数
# execute_process(
#     COMMAND git describe --tags --abbrev=0
#     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#     OUTPUT_VARIABLE LATEST_TAG
#     OUTPUT_STRIP_TRAILING_WHITESPACE
# )

# if(NOT LATEST_TAG)
#     message(WARNING "Failed to get latest tag.")
# else()
#     # 使用git describe命令获取距离最近标签的更新次数
#     execute_process(
#         COMMAND git rev-list ${LATEST_TAG}..HEAD --count
#         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#         OUTPUT_VARIABLE UPDATE_COUNT
#         OUTPUT_STRIP_TRAILING_WHITESPACE
#     )
#     set(VERSION_EXTRA ${UPDATE_COUNT})

#     execute_process(
#         COMMAND git log -1 --format=%cd --date=iso
#         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#         OUTPUT_VARIABLE PROJECT_COMMIT_DATE
#         OUTPUT_STRIP_TRAILING_WHITESPACE
#     )
# endif()

# # 设置项目的版本号
# set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}.${VERSION_EXTRA}")

# # -------------------------获取Qt版本和编译器信息-------------------------------------------------
# find_package(Qt6 COMPONENTS Quick QUIET)
# if (NOT Qt6_FOUND)
#     find_package(Qt5 COMPONENTS Core REQUIRED)
# endif()

# # 获取并显示 Qt 版本（包括子版本）
# if (Qt6Core_VERSION)
#     set(QtVersion ${Qt6Core_VERSION})
# elseif (Qt5Core_VERSION)
#     set(QtVersion ${Qt5Core_VERSION})
# else()
#     message(FATAL_ERROR "Qt not found")
# endif()

# # 获取编译器信息
# if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
#     if (MINGW)
#         set(Compiler "MinGW ${CMAKE_CXX_COMPILER_VERSION}")
#     else()
#         set(Compiler "GCC ${CMAKE_CXX_COMPILER_VERSION}")
#     endif()
# elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
#     set(Compiler "MSVC ${CMAKE_CXX_COMPILER_VERSION}")
#     add_definitions(-DWIN32)
# elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
#     set(Compiler "Clang ${CMAKE_CXX_COMPILER_VERSION}")
# else()
#     set(Compiler "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
# endif()


# # 获取系统架构
# set(Arch ${CMAKE_SYSTEM_PROCESSOR})

# # 输出完整的版本信息
# set(QT_COMPILER_VERSION "Qt ${QtVersion} (${Compiler}, ${Arch})")

# #------------------------------------------------------------------------------



# # -------------------------输出软件的定义-------------------------------------------------
# message(STATUS "")
# message(STATUS "---------------- ${PROJECT_NAME} ----------------------")
# message(STATUS "PROJECT_NAME          : ${PROJECT_NAME}")
# message(STATUS "PROJECT_VERSION       : ${PROJECT_VERSION}")
# message(STATUS "PROJECT_GIT_BRANCH    : ${PROJECT_GIT_BRANCH}")
# message(STATUS "PROJECT_GIT_VERSION   : ${PROJECT_GIT_VERSION}")
# message(STATUS "PROJECT_TAG_VERSION   : ${PROJECT_TAG_VERSION}")
# message(STATUS "PROJECT_COMMIT_DATE   : ${PROJECT_COMMIT_DATE}")
# message(STATUS "")
# message(STATUS "CMAKE_SYSTEM          : ${CMAKE_SYSTEM}")
# message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
# if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
#     set(WINDOWS_X64_PLATFORM TRUE)
#     set(SYSTEM_PLATFORM "WINDOWS_X64_PLATFORM")
#     message(STATUS "SYSTEM_PLATFORM       : WINDOWS_X64_PLATFORM")
# elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
#      set(LINUX_X64_PLATFORM TRUE)
#      set(SYSTEM_PLATFORM "LINUX_X64_PLATFORM")
#      message(STATUS "SYSTEM_PLATFORM       : LINUX_X64_PLATFORM")
# elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
#     set(LINUX_ARM_PLATFORM TRUE)
#     set(SYSTEM_PLATFORM "LINUX_ARM_PLATFORM")
#      message(STATUS "SYSTEM_PLATFORM       : LINUX_ARM_PLATFORM")
# endif()
# message(STATUS "CMAKE_BUILD_TYPE      : ${CMAKE_BUILD_TYPE}")
# message(STATUS "PROJECT_BUILD_DATE    : ${PROJECT_BUILD_DATE}")
# message(STATUS "QT_COMPILER_VERSION   : ${QT_COMPILER_VERSION}")
# message(STATUS "")
# message(STATUS "PROJECT_APP_NAME      : ${PROJECT_APP_NAME}")
# message(STATUS "PROJECT_COMPANY       : ${PROJECT_COMPANY}")
# message(STATUS "PROJECT_COPYRIGHT     : ${PROJECT_COPYRIGHT}")
# message(STATUS "PROJECT_DOMAIN        : ${PROJECT_DOMAIN}")
# message(STATUS "PROJECT_DESCRIPTION   : ${PROJECT_DESCRIPTION}")
# message(STATUS "")



#json
set(JSON_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/rely/json/include")

#spdlog
set(SPDLOG_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/rely/spdlog/include")

add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(lib)
# add_subdirectory(rely/FluentUI-Pro/FluentUI/)
add_subdirectory(rely/rtklib)
# # add_subdirectory(rely/abseil-cpp)

# add_subdirectory(rely/spdlog)
# # add_subdirectory(prototype)

# add_subdirectory(rely/json)

add_subdirectory(rely/sqlite-amalgamation-3500200/)
# # 设置 PROJ 查找 sqlite3 库


set(SQLite3_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/rely/sqlite-amalgamation-3500200/" CACHE PATH "" FORCE)


if (MSVC)
    set(EXE_SQLITE3 "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/sqlite3_exec.exe" CACHE PATH "" FORCE)
    set(SQLITE3_LIBRARY "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/sqlite3.lib" CACHE STRING "" FORCE)
else()
    set(EXE_SQLITE3 "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/sqlite3_exec" CACHE PATH "Path to sqlite3 executable" FORCE)
    set(SQLITE3_LIBRARY "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/libsqlite3.a" CACHE STRING "" FORCE)
endif()




# set(NLOHMANN_JSON_ORIGIN external CACHE STRING "" FORCE)
# set(nlohmann_json_DIR "${CMAKE_SOURCE_DIR}/rely/json/" CACHE PATH "" FORCE)

set(ENABLE_TIFF OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL OFF CACHE BOOL "" FORCE)
set(BUILD_APPS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(PROJ_DB_CACHE_DIR "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}/data" CACHE PATH "" FORCE)
file(MAKE_DIRECTORY "${PROJ_DB_CACHE_DIR}")


add_subdirectory(rely/PROJ/)

# set(CMARK_TESTS OFF CACHE BOOL "force set this option"  FORCE)
# set(CMARK_SHARED OFF CACHE BOOL "force set this option"  FORCE)
# include_directories(md2html_demo PUBLIC ${CMAKE_SOURCE_DIR}/rely/cmark-gfm/src)
# include_directories(md2html_demo PUBLIC ${CMAKE_SOURCE_DIR}/rely/cmark-gfm/extensions)
# include_directories(md2html_demo PUBLIC ${CMAKE_BINARY_DIR}/rely/cmark-gfm/src)
# add_subdirectory(rely/cmark-gfm/)


# add_subdirectory(rely/yaml-cpp)

# message(STATUS "CMAKE_CXX_FLAGS_DEBUG   : ${CMAKE_CXX_FLAGS_DEBUG}")
# message(STATUS "CMAKE_CXX_FLAGS_RELEASE : ${CMAKE_CXX_FLAGS_RELEASE}")
# message(STATUS "CMAKE_BUILD_TYPE        : ${CMAKE_BUILD_TYPE}")


# #设定version.h中的具体信息

# #基于version.h.in 生成 version.h
# configure_file(${CMAKE_SOURCE_DIR}/.cmake/Version.h.in ${CMAKE_BINARY_DIR}/Version.h @ONLY)


# # 设置所有 DLL（无论 Qt 的还是你自己的）生成到 binary/dll/
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)     # exe + dll（运行时）
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/dll)     # dll（库）
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/lib)     # .lib 静态库、import lib
