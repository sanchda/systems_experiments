project(test_exec_lib)

add_library(foo SHARED foo.cpp)
add_executable(main main.cpp)

target_link_libraries(main foo)
target_link_options(foo PRIVATE "-Wl,-e,mymain")
