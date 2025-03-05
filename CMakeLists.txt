cmake_minimum_required(VERSION 3.8)

project(lob)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

message(${CMAKE_CURRENT_BINARY_DIR})
add_executable(${PROJECT_NAME} 
    "../src/main.cpp" 
    "../src/limitorderbook.cpp"
    "../src/tradingstrategy.cpp"
    # "../src/eventqueue.cpp"
    # "../src/feedproducer.cpp"
    # "../src/api/feedapi.cpp"
    # "../src/api/orderapi.cpp"
    "../src/server.cpp"
)