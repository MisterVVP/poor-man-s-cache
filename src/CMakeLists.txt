add_executable(${PROJECT_NAME} main.cpp kvs.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE highwayhash)

if(PROMETHEUS_CPP_ENABLE_PUSH)
  target_link_libraries(${PROJECT_NAME} PRIVATE prometheus-cpp::push $<$<BOOL:${WIN32}>:Ws2_32>)
endif()

if(PROMETHEUS_CPP_ENABLE_PULL)
  target_link_libraries(${PROJECT_NAME} PRIVATE prometheus-cpp::pull)
endif()
