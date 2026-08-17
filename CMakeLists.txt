cmake_minimum_required(VERSION 3.20)

project(miniasyncnetsockets VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

option(MINIASYNCNETSOCKETS_BUILD_TESTS "Build miniasyncnetsockets tests" ON)
option(MINIASYNCNETSOCKETS_BUILD_EXAMPLES "Build miniasyncnetsockets examples" OFF)
option(ENABLE_ASAN_UBSAN "Build with ASAN+UBSAN sanitizers" OFF)
option(ENABLE_TSAN "Build with TSAN sanitizer" OFF)

if (ENABLE_ASAN_UBSAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASAN+UBSAN and TSAN can not be combined")
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

# Dependencies are embedded in this project; their standalone tests and examples
# must not become part of the root build.
set(BUILD_TESTS OFF)
set(BUILD_EXAMPLES OFF)
set(ENABLE_TESTS OFF)
set(ENABLE_EXAMPLES OFF)
add_subdirectory(external/miniruntime)
add_subdirectory(external/mininetsockets)
unset(BUILD_TESTS)
unset(BUILD_EXAMPLES)
unset(ENABLE_TESTS)
unset(ENABLE_EXAMPLES)

add_library(miniasyncnetsockets_sanitizers INTERFACE)
if (ENABLE_ASAN_UBSAN)
    target_compile_options(miniasyncnetsockets_sanitizers INTERFACE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(miniasyncnetsockets_sanitizers INTERFACE
        -fsanitize=address,undefined
    )
elseif (ENABLE_TSAN)
    target_compile_options(miniasyncnetsockets_sanitizers INTERFACE
        -fsanitize=thread
        -fno-omit-frame-pointer
    )
    target_link_options(miniasyncnetsockets_sanitizers INTERFACE
        -fsanitize=thread
    )
endif()

add_library(miniasyncnetsockets STATIC
    src/miniasyncnetsockets.cpp
    src/detail/framecodec.cpp
    src/detail/writequeue.cpp
    src/detail/connectionstate.cpp
    src/tcpconnection.cpp
    src/detail/serverstate.cpp
    src/tcpserver.cpp
    src/detail/clientstate.cpp
    src/tcpclient.cpp
)
target_compile_features(miniasyncnetsockets PUBLIC cxx_std_20)
target_compile_options(miniasyncnetsockets PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(miniasyncnetsockets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(miniasyncnetsockets
    PUBLIC
        miniruntime
        mininetsockets
        miniasyncnetsockets_sanitizers
)

if (MINIASYNCNETSOCKETS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (MINIASYNCNETSOCKETS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(
    TARGETS
        miniasyncnetsockets
        miniasyncnetsockets_sanitizers
        miniruntime
        sanitizers_options
        mininetsockets
        mininetsockets_options
    EXPORT miniasyncnetsocketsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY include/miniasyncnetsockets
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT miniasyncnetsocketsTargets
    FILE miniasyncnetsocketsTargets.cmake
    NAMESPACE miniasyncnetsockets::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/miniasyncnetsockets
)
