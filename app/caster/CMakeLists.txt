project(navcaster-caster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

file(GLOB_RECURSE NAVCASTER_CASTER_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

set(NAVCASTER_CASTER_CORE_SOURCES ${NAVCASTER_CASTER_SOURCES})
list(REMOVE_ITEM NAVCASTER_CASTER_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp)
file(GLOB_RECURSE NAVCASTER_CASTER_TEST_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
)
foreach(TEST_SOURCE IN LISTS NAVCASTER_CASTER_TEST_SOURCES)
    list(REMOVE_ITEM NAVCASTER_CASTER_CORE_SOURCES ${TEST_SOURCE})
endforeach()

add_library(navcaster-caster-core STATIC ${NAVCASTER_CASTER_CORE_SOURCES})

target_include_directories(navcaster-caster-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(navcaster-caster-core PUBLIC
    event
    event_core
    event_extra
    hiredis
    nlohmann_json::nlohmann_json
    spdlog::spdlog_header_only
    Threads::Threads
)

if(NOT WIN32)
    target_link_libraries(navcaster-caster-core PUBLIC event_pthreads)
endif()

if(WIN32)
    target_link_libraries(navcaster-caster-core PUBLIC ws2_32)
endif()

add_executable(navcaster-caster app/main.cpp)
target_link_libraries(navcaster-caster PRIVATE navcaster-caster-core)

if(BUILD_TESTING)
    add_executable(navcaster-caster-basic-smoke tests/caster_basic_smoke.cpp)
    target_link_libraries(navcaster-caster-basic-smoke PRIVATE navcaster-caster-core)
    add_test(NAME navcaster-caster-basic-smoke COMMAND navcaster-caster-basic-smoke)
endif()
