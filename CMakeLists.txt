cmake_minimum_required(VERSION 3.20)
project(arm_cpu_emulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

# --- Dependencies via FetchContent ---
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ankerl::unordered_dense (optional, high-performance hash map)
find_package(ankerl_unordered_dense QUIET)
if(NOT ankerl_unordered_dense_FOUND)
    FetchContent_Declare(
        unordered_dense
        GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
        GIT_TAG v4.4.0
    )
    FetchContent_MakeAvailable(unordered_dense)
    add_library(ankerl::unordered_dense ALIAS unordered_dense)
endif()

# --- Library ---
file(GLOB_RECURSE ARM_CPU_SOURCES
    src/*.cpp
    src/**/*.cpp
)

# Exclude main.cpp from the library (it has its own executable target)
list(FILTER ARM_CPU_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

add_library(arm_cpu_emulator STATIC ${ARM_CPU_SOURCES})

target_include_directories(arm_cpu_emulator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(arm_cpu_emulator PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# Check for liblzma (ChampSim XZ support)
find_package(LibLZMA QUIET)
if(LibLZMA_FOUND)
    target_compile_definitions(arm_cpu_emulator PUBLIC HAS_LIBLZMA=1)
    target_link_libraries(arm_cpu_emulator PUBLIC LibLZMA::LibLZMA)
endif()

# --- Executable ---
add_executable(arm_cpu_sim src/main.cpp)
target_link_libraries(arm_cpu_sim PRIVATE arm_cpu_emulator)

# --- Tests ---
enable_testing()

file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
foreach(test_file ${TEST_SOURCES})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE arm_cpu_emulator GTest::gtest_main)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# --- Examples (optional) ---
option(BUILD_EXAMPLES "Build example programs" OFF)
if(BUILD_EXAMPLES)
    file(GLOB_RECURSE EXAMPLE_SOURCES examples/*.cpp)
    foreach(ex_file ${EXAMPLE_SOURCES})
        get_filename_component(ex_name ${ex_file} NAME_WE)
        add_executable(${ex_name} ${ex_file})
        target_link_libraries(${ex_name} PRIVATE arm_cpu_emulator)
    endforeach()
endif()
