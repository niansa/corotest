cmake_minimum_required(VERSION 3.5)

project(corotest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(corotest main.cpp)

target_include_directories(corotest PRIVATE libasync/include/ frigg/include)

target_link_libraries(corotest PRIVATE uv)
