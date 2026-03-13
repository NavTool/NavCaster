project(caster_proto LANGUAGES CXX)

file(GLOB PROTO_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/auth/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/auth/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/*.pb.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/service/*.pb.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/service/*.pb.h"
)


add_library(${PROJECT_NAME} STATIC ${PROTO_SOURCES})
target_link_libraries(${PROJECT_NAME} libprotobuf)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
