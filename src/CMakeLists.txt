add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE "${uwebsockets_SOURCE_DIR}/src" "${uwebsockets_SOURCE_DIR}/uSockets/src")

target_link_libraries(${PROJECT_NAME} PRIVATE "${uwebsockets_SOURCE_DIR}/uSockets/uSockets.a" ZLIB::ZLIB)
