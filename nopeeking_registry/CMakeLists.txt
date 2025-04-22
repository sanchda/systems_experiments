cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME nopeeking)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CLANG_TIDY_COMMAND "clang-tidy" "-checks=bugprone-*,cert-*,misc-*,performance-*,portability-*,readability-*,-clang-analyzer-*")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(${PROJECT_NAME} src/main.c src/nopeeking.c)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(${PROJECT_NAME}
    PRIVATE include
)

# Finally, let's make sure we save the compile_commands.json
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)
