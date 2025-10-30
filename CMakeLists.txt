cmake_minimum_required(VERSION 3.21)
project(NavCaster)


#libevent option
# set(EVENT_LIBRARY_TYPE STATIC)
set(EVENT__LIBRARY_TYPE STATIC CACHE STRING "Set library type to STATIC" FORCE)
set(EVENT__DISABLE_OPENSSL ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_TESTS ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_REGRESS ON CACHE BOOL "force set this option" FORCE)
set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "force set this option" FORCE)
add_subdirectory(rely/libevent)

#hiredis option
set(BUILD_SHARED_LIBS OFF CACHE BOOL "force set this option"  FORCE)
set(DISABLE_TESTS ON CACHE BOOL "force set this option"  FORCE)
add_subdirectory(rely/hiredis)

#yaml-cpp option
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "force set this option"  FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "force set this option"  FORCE)
add_subdirectory(rely/yaml-cpp)

#json
set(JSON_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/rely/json/include")

#spdlog
set(SPDLOG_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/rely/spdlog/include")

#rtklib
add_subdirectory(rely/rtklib)


add_subdirectory(app)
add_subdirectory(lib)




