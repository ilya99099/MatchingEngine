cmake_minimum_required(VERSION 3.20)
project(MatchingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(MatchingEngine
        src/main.cpp
)
target_include_directories(MatchingEngine PRIVATE src)

add_executable(me_bench
        src/bench.cpp
)
target_include_directories(me_bench PRIVATE src)