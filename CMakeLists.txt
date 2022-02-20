cmake_minimum_required(VERSION 3.0)

project(tcp_proxy LANGUAGES C)

add_executable(tcp_proxy main.c)

target_link_libraries(tcp_proxy pthread)

install(TARGETS tcp_proxy RUNTIME DESTINATION bin)
