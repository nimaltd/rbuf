# Unity is vendored in test/unity. It is third party code, so it is built as its
# own target and is deliberately left out of this project's -Werror.
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)

set(RBUF_WARNINGS -Wall -Wextra -Wpedantic -Werror -Wundef)

find_package(Threads REQUIRED)

# One build of a test file against its own rbuf.c, with the stub main.h given.
function(rbuf_suite name test_file stub)
    add_library(rbuf_${name} STATIC ${PROJECT_SOURCE_DIR}/src/rbuf.c)
    target_include_directories(rbuf_${name} PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/${stub})
    target_compile_features(rbuf_${name} PUBLIC c_std_11)

    add_executable(test_rbuf_${name} ${test_file})
    target_link_libraries(test_rbuf_${name} PRIVATE rbuf_${name} unity Threads::Threads)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(rbuf_${name} PRIVATE ${RBUF_WARNINGS})
        target_compile_options(test_rbuf_${name} PRIVATE ${RBUF_WARNINGS})
    endif()

    add_test(NAME rbuf_${name} COMMAND test_rbuf_${name})
endfunction()

# The library as it ships, with an interrupt played in the middle of a copy.
add_executable(test_rbuf test_rbuf.c)
target_link_libraries(test_rbuf PRIVATE rbuf unity)
target_include_directories(test_rbuf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rbuf PRIVATE ${RBUF_WARNINGS})
    target_compile_options(test_rbuf PRIVATE ${RBUF_WARNINGS})
endif()

add_test(NAME rbuf COMMAND test_rbuf)

# Real threads on several cores at once: one pusher and one popper, then
# several of each.
rbuf_suite(threads test_rbuf_threads.c stub_threads)

# The index sums, up to the biggest buffer rbuf_init() takes. rbuf.c is
# compiled into the test itself, so there is no library to link.
add_executable(test_rbuf_index test_rbuf_index.c)
target_link_libraries(test_rbuf_index PRIVATE unity)
target_include_directories(test_rbuf_index PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/stub)
target_compile_features(test_rbuf_index PRIVATE c_std_11)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_rbuf_index PRIVATE ${RBUF_WARNINGS})
endif()

add_test(NAME rbuf_index COMMAND test_rbuf_index)
