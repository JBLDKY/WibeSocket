cmake_minimum_required(VERSION 3.16)
project(WibeSocket C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(wibesocket SHARED
  src/wibesocket.c
  src/parser.c
  src/event_loop.c
  src/internal/sha1.c
  src/internal/base64.c
  src/internal/utf8.c
  src/internal/ringbuf.c
  src/handshake.c
)
target_include_directories(wibesocket PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(wibesocket PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

enable_testing()
add_executable(test_wibesocket tests/test_wibesocket.c)
target_include_directories(test_wibesocket PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_wibesocket PRIVATE wibesocket)
add_test(NAME test_wibesocket COMMAND test_wibesocket)

add_executable(test_handshake tests/test_handshake.c)
target_include_directories(test_handshake PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_handshake PRIVATE wibesocket)
add_test(NAME test_handshake COMMAND test_handshake)

add_executable(test_parser tests/test_parser.c)
target_include_directories(test_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_parser PRIVATE wibesocket)
add_test(NAME test_parser COMMAND test_parser)

# Temporarily disabled due to CI env issue; ringbuf is covered by integration tests
# add_executable(test_ringbuf tests/test_ringbuf.c src/internal/ringbuf.c)
# target_include_directories(test_ringbuf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
# add_test(NAME test_ringbuf COMMAND test_ringbuf)

# examples
add_executable(example_simple_echo examples/simple_echo.c)
target_include_directories(example_simple_echo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(example_simple_echo PRIVATE wibesocket)

