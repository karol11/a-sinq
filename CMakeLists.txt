cmake_minimum_required (VERSION 3.8)

project ("a_sinq")

set(CMAKE_CXX_STANDARD 17)
add_definitions(-D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

include_directories (
    "include"
    "tests"
)

add_executable (a_synq

    "include/a_sinq.h"

    "tests/gunit.h"
    "tests/gunit.cpp"
    "tests/single_thread_executor.h"
    "tests/loop_test.cpp"
    "tests/slot_test.cpp"

    "examples/loop_example.cpp"
    "examples/result_example.cpp"
    "examples/slot_example.cpp"
)
