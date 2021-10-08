cmake_minimum_required(VERSION 3.20)

project(forward)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS "ON")

add_definitions(-DBUFFER_SIZE_CODEC)

find_package(Threads REQUIRED)

include_directories("/home/gyl/fs/code/asio/asio/include")

add_executable(server server.cpp)
add_executable(client client.cpp)

target_link_libraries(server
        Threads::Threads
        )
target_link_libraries(client
        Threads::Threads
        )

