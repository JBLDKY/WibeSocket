cmake_minimum_required(VERSION 3.16)
project(WibeSocket C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(wibesocket SHARED
  src/wibesocket.c
)
target_include_directories(wibesocket PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()
add_executable(test_wibesocket tests/test_wibesocket.c)
target_include_directories(test_wibesocket PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_wibesocket PRIVATE wibesocket)
add_test(NAME test_wibesocket COMMAND test_wibesocket)

