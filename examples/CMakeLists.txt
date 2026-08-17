add_executable(framed-echoserver framed-echoserver.cpp)
target_link_libraries(framed-echoserver PRIVATE miniasyncnetsockets)
