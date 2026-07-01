cmake_minimum_required(VERSION 3.16)

project(expression_calculator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(expression_calculator_core
    src/evaluator.cpp
    src/thread_pool.cpp
    src/tcp_server.cpp
)

target_include_directories(expression_calculator_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(expression_calculator_core
    PRIVATE -Wall -Wextra -Wpedantic
)

target_link_libraries(expression_calculator_core
    PUBLIC pthread
)

add_executable(expression_server src/main.cpp)
target_link_libraries(expression_server PRIVATE expression_calculator_core)

add_executable(evaluator_tests tests/evaluator_tests.cpp)
target_link_libraries(evaluator_tests PRIVATE expression_calculator_core)

add_executable(generate_expressions tools/generate_expressions.cpp)
target_link_libraries(generate_expressions PRIVATE expression_calculator_core)

enable_testing()
add_test(NAME evaluator_tests COMMAND evaluator_tests)
