add_executable(miniasyncnetsockets_header_test headercompilationtest.cpp)
target_link_libraries(miniasyncnetsockets_header_test PRIVATE miniasyncnetsockets)
add_test(NAME miniasyncnetsockets_header_test COMMAND miniasyncnetsockets_header_test)

include(FetchContent)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
add_executable(framecodectest framecodectest.cpp)
target_include_directories(framecodectest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(framecodectest PRIVATE miniasyncnetsockets GTest::gtest_main)
gtest_discover_tests(framecodectest)

add_executable(writequeuetest writequeuetest.cpp)
target_include_directories(writequeuetest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(writequeuetest PRIVATE miniasyncnetsockets GTest::gtest_main)
gtest_discover_tests(writequeuetest)

add_executable(tcpservertest tcpservertest.cpp)
target_link_libraries(tcpservertest PRIVATE miniasyncnetsockets GTest::gtest_main)
gtest_discover_tests(tcpservertest)

add_executable(tcpclienttest tcpclienttest.cpp)
target_link_libraries(tcpclienttest PRIVATE miniasyncnetsockets GTest::gtest_main)
gtest_discover_tests(tcpclienttest)
