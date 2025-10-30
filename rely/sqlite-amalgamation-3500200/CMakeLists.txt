

cmake_minimum_required(VERSION 3.5)

project(sqlite3 LANGUAGES CXX)

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # -fPIC

# # 1. 创建静态库（供主程序使用）
add_library(sqlite3 STATIC sqlite3.h sqlite3.c sqlite3ext.h)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


# # 2. 创建 sqlite3 可执行程序（命令行工具）
add_executable(sqlite3_exec shell.c sqlite3.h sqlite3.c sqlite3ext.h)
# target_compile_definitions(sqlite3_exec PRIVATE SQLITE_THREADSAFE=1)
# target_compile_definitions(sqlite3_exec PRIVATE SQLITE_ENABLE_REPL)
# target_compile_options(sqlite3_exec PRIVATE -w)  # 忽略 warning
IF(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")

ELSE()
  target_link_libraries(sqlite3_exec m pthread dl)
ENDIF()






