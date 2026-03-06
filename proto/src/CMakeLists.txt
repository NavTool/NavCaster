project(caster_proto LANGUAGES CXX)

file(GLOB PROTO_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/context/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/context/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/service/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/service/*.pb.h"
)


add_library(${PROJECT_NAME} STATIC ${PROTO_SOURCES})
target_link_libraries(${PROJECT_NAME} libprotobuf)