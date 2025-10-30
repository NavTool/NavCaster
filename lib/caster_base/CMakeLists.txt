cmake_minimum_required(VERSION 3.5)

project(knt LANGUAGES CXX)

#版本14以下可能会导致abseil不支持
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# add_subdirectory(third/)
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
elseif(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
endif()

include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include)
include_directories(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/include/knt)
#包含的源文件和头文件
set(SOURCES
    include/base64.h      src/base64.cpp
    include/knt.h      src/knt.cpp
    include/rtcm_decode.h src/rtcm_decode.cpp
    )
    

add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
