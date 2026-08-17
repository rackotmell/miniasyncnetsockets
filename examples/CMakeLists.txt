add_executable(framed-echoserver framed-echoserver.cpp)
target_link_libraries(framed-echoserver PRIVATE miniasyncnetsockets)

add_executable(framed-terminalclient framed-terminalclient.cpp)
target_link_libraries(framed-terminalclient PRIVATE miniasyncnetsockets)
