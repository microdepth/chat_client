cmake_minimum_required(VERSION 3.26)
project(chat_client)

set(CMAKE_CXX_STANDARD 20)

add_executable(chat_client main.cpp)

target_link_libraries(chat_client ws2_32)