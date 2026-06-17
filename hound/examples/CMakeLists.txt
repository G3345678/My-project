# Example programs — each demonstrates one aspect of the library

# 1. TCP Echo Server (raw blocking I/O — Module 1 demo)
add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE hound)
target_include_directories(echo_server PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 2. Non-blocking Reactor Echo Server (Module 2 demo)
add_executable(echo_server_reactor echo_server_reactor.cpp)
target_link_libraries(echo_server_reactor PRIVATE hound)
target_include_directories(echo_server_reactor PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 3. Full HTTP/1.1 Server (all modules demo)
add_executable(http_server http_server.cpp)
target_link_libraries(http_server PRIVATE hound)
target_include_directories(http_server PRIVATE ${CMAKE_SOURCE_DIR}/src)
