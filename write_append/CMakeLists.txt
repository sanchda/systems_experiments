cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME write_append)
project(${PROJECT_NAME} C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# get libaio
include(ExternalProject)
set(LIBAIO_INSTALL_DIR ${CMAKE_BINARY_DIR}/libaio-install)

ExternalProject_Add(
    libaio_external
    URL https://pagure.io/libaio/archive/libaio-0.3.112/libaio-libaio-0.3.112.tar.gz
    PREFIX ${CMAKE_BINARY_DIR}/libaio
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    BUILD_COMMAND make
    INSTALL_COMMAND make prefix=${LIBAIO_INSTALL_DIR} install
    UPDATE_COMMAND ""
)

# Set the include and library paths for libaio
set(LIBAIO_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/libaio/src/libaio_external/src)
set(LIBAIO_LIBRARIES ${CMAKE_BINARY_DIR}/libaio/src/libaio_external/src/libaio.a)

set(CLANG_TIDY_COMMAND "clang-tidy" "-checks=bugprone-*,cert-*,misc-*,performance-*,portability-*,readability-*,-clang-analyzer-*")

# Compile mmlog.h as a static library for other targets that can't include a C11 header
add_library(mmlog STATIC ../fastlog/src/libmmlog.c)
target_include_directories(mmlog
    PRIVATE include
    PRIVATE ../fastlog/include
)
target_compile_options(mmlog PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(mmlog PRIVATE pthread)

# Add the main executable
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(${PROJECT_NAME} src/main.cc)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(${PROJECT_NAME}
    PRIVATE include
    PRIVATE ../fastlog/include
    PRIVATE ${LIBAIO_INCLUDE_DIRS} # Add libaio include dirs
)

# If we downloaded libaio, add a dependency
if(TARGET libaio_external)
    add_dependencies(${PROJECT_NAME} libaio_external)
endif()

# Link against libaio
target_link_libraries(${PROJECT_NAME}
    PRIVATE ${LIBAIO_LIBRARIES}
    PRIVATE mmlog
)

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

