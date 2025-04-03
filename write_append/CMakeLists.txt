cmake_minimum_required(VERSION 3.18)
set(PROJECT_NAME write_append)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER clang)
project(${PROJECT_NAME} CXX)

# Basic configuration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # for clangd
set(CLANG_TIDY_COMMAND "clang-tidy" "-checks=*,modernize-*")

# Include FetchContent and use it to get libaio (don't want to install at the system level
include(FetchContent)
FetchContent_Declare(
    libaio
    GIT_REPOSITORY https://github.com/yugabyte/libaio
    GIT_TAG master  # Consider using a specific tag/commit for stability
)
FetchContent_MakeAvailable(libaio)

# Main executable
add_executable(${PROJECT_NAME} src/main.cpp)
target_include_directories(${PROJECT_NAME}
    PRIVATE ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${libaio_SOURCE_DIR}/src
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
