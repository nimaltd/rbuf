cmake_minimum_required(VERSION 3.16)

project(rbuf
    VERSION     3.0.0
    LANGUAGES   C
    DESCRIPTION "Ring buffer for items of any type, safe between interrupts and threads, for STM32"
    HOMEPAGE_URL "https://github.com/nimaltd/rbuf")

option(RBUF_BUILD_TESTS "Build the host unit tests" OFF)

# rbuf.c needs one header it does not ship: the platform main.h. Point
# RBUF_CONFIG_DIR at the folder holding it, normally Core/Inc in a CubeMX
# project. The tests supply a stub instead.
if(RBUF_BUILD_TESTS AND NOT DEFINED RBUF_CONFIG_DIR)
    set(RBUF_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/stub")
endif()

add_library(rbuf STATIC src/rbuf.c)
add_library(nimaltd::rbuf ALIAS rbuf)

target_include_directories(rbuf PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

target_compile_features(rbuf PUBLIC c_std_11)

if(DEFINED RBUF_CONFIG_DIR)
    target_include_directories(rbuf PRIVATE ${RBUF_CONFIG_DIR})
endif()

if(RBUF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
