cmake_minimum_required(VERSION 3.12.0)
project(mylib VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

add_executable(mylib test.cpp main.cpp)
add_executable(encoder encoder.cpp)
