project(navcaster-caster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

file(GLOB_RECURSE NAVCASTER_CASTER_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(navcaster-caster ${NAVCASTER_CASTER_SOURCES})

target_include_directories(navcaster-caster PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}
    ${JSON_INCLUDE_PATH}
)

target_link_libraries(navcaster-caster PRIVATE
    event
    event_core
    event_extra
    hiredis
    Threads::Threads
)

if(NOT WIN32)
    target_link_libraries(navcaster-caster PRIVATE event_pthreads)
endif()

if(WIN32)
    target_link_libraries(navcaster-caster PRIVATE ws2_32)
endif()
