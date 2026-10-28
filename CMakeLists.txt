cmake_minimum_required(VERSION 3.28)

project(rfc862 VERSION 0.0.0 DESCRIPTION "RFC 862 Echo Protocol server" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(STATIC "Link the executable fully statically" OFF)
set(SANITIZE "" CACHE STRING "Sanitizer to enable: address (ASan+UBSan) or thread (TSan)")
set_property(CACHE SANITIZE PROPERTY STRINGS "" address thread)

add_library(rfc862_options INTERFACE)
target_compile_options(rfc862_options INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)

if(SANITIZE STREQUAL "address")
  target_compile_options(rfc862_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(rfc862_options INTERFACE -fsanitize=address,undefined)
elseif(SANITIZE STREQUAL "thread")
  target_compile_options(rfc862_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(rfc862_options INTERFACE -fsanitize=thread)
elseif(NOT SANITIZE STREQUAL "")
  message(FATAL_ERROR "SANITIZE must be empty, 'address' or 'thread', got '${SANITIZE}'")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output LANGUAGES CXX)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()

add_library(rfc862_lib STATIC)
target_sources(rfc862_lib
  PUBLIC FILE_SET CXX_MODULES FILES
    src/rfc862.cppm
    src/common.cppm
    src/fd.cppm
    src/socket.cppm
    src/epoll.cppm
    src/tcp_echo.cppm
    src/udp_echo.cppm
    src/server.cppm
    src/signals.cppm
)
target_link_libraries(rfc862_lib PUBLIC rfc862_options)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
