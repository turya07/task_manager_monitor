cmake_minimum_required(VERSION 3.24)

project("task_monitor")

find_package(SFML 2.6 REQUIRED COMPONENTS window graphics system)
add_executable(task_monitor main.cpp)
target_link_libraries(task_monitor sfml-graphics sfml-window sfml-system)
