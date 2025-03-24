cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME demo)
project(${PROJECT_NAME} C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


# Unity Testing Framework
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.5.2
)
FetchContent_MakeAvailable(unity)

# Test executable (create tests directory and test files as needed)
add_executable(unit_tests EXCLUDE_FROM_ALL tests/mmlog.c)
target_include_directories(unit_tests
    PRIVATE include
    PRIVATE ${unity_SOURCE_DIR}/src
)
target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(unit_tests
    PRIVATE mmlog
    PRIVATE unity
)

# Run tests target
add_custom_target(test
    COMMAND unit_tests
    DEPENDS unit_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

set(CLANG_TIDY_COMMAND "clang-tidy" "-checks=bugprone-*,cert-*,misc-*,performance-*,portability-*,readability-*,-clang-analyzer-*")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(${PROJECT_NAME} src/main.c)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(${PROJECT_NAME}
    PRIVATE include
)
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)

# Compile mmlog.h as a static library for other targets that can't include a C11 header
add_library(mmlog STATIC src/libmmlog.c)
target_include_directories(mmlog
    PRIVATE include
)
target_compile_options(mmlog PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(mmlog PRIVATE pthread)

# VM Reader example
add_executable(vm_reader eg/vm_reader/src/main.cc)
set_target_properties(vm_reader PROPERTIES CXX_STANDARD 17)
target_compile_options(vm_reader PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(vm_reader
    PRIVATE include
    PRIVATE eg/vm_reader/include
)
target_link_libraries(vm_reader
    PRIVATE pthread
    PRIVATE mmlog
)

# Forks example
add_executable(forks eg/forks/src/main.c)
target_compile_options(forks PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(forks
    PRIVATE include
    PRIVATE eg/vm_reader/include
)
target_link_libraries(forks
    PRIVATE mmlog
)

# Threads example
add_executable(threads eg/threads/src/main.c)
target_compile_options(threads PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(threads
    PRIVATE include
    PRIVATE eg/vm_reader/include
)
target_link_libraries(threads
    PRIVATE mmlog
)

# Finally, let's make sure we save the compile_commands.json
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)
