cmake_minimum_required(VERSION 3.28.0 FATAL_ERROR)
project(poor-man-s-cache VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")

find_package(prometheus-cpp CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

if(NOT DEFINED prometheus-cpp_VERSION)
  message(FATAL_ERROR "prometheus-cpp_VERSION is not defined")
endif()

add_subdirectory(src)