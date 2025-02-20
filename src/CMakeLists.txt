add_executable(
  ${PROJECT_NAME} 
  main.cpp
  server/server.cpp
  server/shard.cpp
  server/conn_manager.hpp
  server/sockutils.cpp
  hash/MurmurHash3.cpp
  hash/hash.cpp
  primegen/primegen.cpp
  kvs/kvs.cpp
  metrics/metrics.cpp
  compressor/gzip_compressor.cpp
)

if(PROMETHEUS_CPP_ENABLE_PUSH)
  target_link_libraries(${PROJECT_NAME} PRIVATE prometheus-cpp::push $<$<BOOL:${WIN32}>:Ws2_32>)
endif()

if(PROMETHEUS_CPP_ENABLE_PULL)
  target_link_libraries(${PROJECT_NAME} PRIVATE prometheus-cpp::pull)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ZLIB::ZLIB)