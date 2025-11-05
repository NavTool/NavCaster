

add_executable(test_MonitorCore test_MonitorCore.cpp)
include_directories(test_MonitorCore  ${SPDLOG_INCLUDE_PATH})
include_directories(test_MonitorCore  ${JSON_INCLUDE_PATH})
target_link_libraries(test_MonitorCore PRIVATE MonitorCore)
