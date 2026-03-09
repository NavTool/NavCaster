project(caster_proto LANGUAGES CXX)

file(GLOB PROTO_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/context/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/context/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mutual/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mutual/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/service/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/service/*.pb.h"
)


add_library(${PROJECT_NAME} STATIC ${PROTO_SOURCES})
target_link_libraries(${PROJECT_NAME} libprotobuf)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
