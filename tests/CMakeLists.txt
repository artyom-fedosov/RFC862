include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  EXCLUDE_FROM_ALL
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(rfc862_tests
  epoll_test.cpp
  fd_test.cpp
  server_test.cpp
  signals_test.cpp
  socket_test.cpp
  tcp_echo_test.cpp
  udp_echo_test.cpp
)
target_sources(rfc862_tests
  PRIVATE FILE_SET CXX_MODULES FILES
    doubles/fake_datagram_io.cppm
    doubles/fake_stream_io.cppm
    support/clients.cppm
)
target_link_libraries(rfc862_tests PRIVATE rfc862_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(rfc862_tests DISCOVERY_MODE PRE_TEST)

# The program takes no arguments: RFC 862 fixes everything.
add_test(NAME program.RejectsArguments COMMAND rfc862 --port 8)
set_tests_properties(program.RejectsArguments PROPERTIES
  PASS_REGULAR_EXPRESSION "usage: rfc862\n"
)
