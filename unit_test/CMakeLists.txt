cmake_minimum_required(VERSION 3.10)

project(R2DTWO_Testing)

include_directories(../)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Og -std=gnu99 -Wall -Wextra -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wshadow -Wvla -Wc++-compat -Werror")

#TODO enable these to compile as C++
#file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/../*.c")
#SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX )
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Og -std=gnu++03 -Wall -Wextra -Wmissing-declarations -Wwrite-strings -Wshadow -Wvla -Werror")

function(add_test_seqrcvy name)
    add_executable(${name} ../seq_recov.c ../lib/parsers/json.c ../log.c ../packet.c ../pipeline.c ../thread_utils.c ../utils.c ../lib/parsers/hashmap.c ${name}.c)
    target_include_directories(${name} PUBLIC ../lib/parsers)
    target_link_libraries(${name} pthread)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_seqrcvy(test_seq_rcvy_match)
add_test_seqrcvy(test_seq_rcvy_oam)
add_test_seqrcvy(test_seq_rcvy_vector)
#TODO seamless

function(add_test_conf name)
    add_executable(${name} ../conf_utils.c ../inet_utils.c ../log.c ../protocol.c ../utils.c ../lib/parsers/hashmap.c ${name}.c)
    target_include_directories(${name} PUBLIC ../lib/parsers)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_conf(test_conf_utils)

add_custom_command(
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gen_conf_utils_dat.pl > conf_utils.dat
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_conf_utils_dat.pl
    OUTPUT conf_utils.dat
    COMMENT "Generating conf_utils.dat"
    VERBATIM)
add_custom_target(gen_conf_utils_dat DEPENDS conf_utils.dat)
add_dependencies(test_conf_utils gen_conf_utils_dat)
target_include_directories(test_conf_utils PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

function(add_test_ifutil name)
    add_executable(${name} ../if_utils.c ../log.c ../packet.c ../utils.c ../lib/parsers/hashmap.c ../lib/parsers/json.c ${name}.c)
    target_include_directories(${name} PUBLIC ../lib/parsers)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_ifutil(test_if_utils)

function(add_test_inet name)
    add_executable(${name} ../inet_utils.c ${name}.c)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_inet(test_inet_utils)

function(add_test_header name)
    add_executable(${name} ../header.c ../log.c ../packet.c ../utils.c ../lib/parsers/hashmap.c ../lib/parsers/json.c ${name}.c)
    target_include_directories(${name} PUBLIC ../lib/parsers)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_header(test_header_api)
add_test_header(test_header_compare)
add_test_header(test_header_read)
add_test_header(test_header_write)

add_custom_command(
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gen_header_compare_dat.pl > header_compare.dat
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_header_compare_dat.pl
    OUTPUT header_compare.dat
    COMMENT "Generating header_compare.dat"
    VERBATIM)
add_custom_target(gen_header_compare_dat DEPENDS header_compare.dat)
add_dependencies(test_header_compare gen_header_compare_dat)
target_include_directories(test_header_compare PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/gen_header_write_dat.pl > header_write.dat
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_header_write_dat.pl
    OUTPUT header_write.dat
    COMMENT "Generating header_write.dat"
    VERBATIM)
add_custom_target(gen_header_write_dat DEPENDS header_write.dat)
add_dependencies(test_header_write gen_header_write_dat)
target_include_directories(test_header_write PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

function(add_test_packet name)
    add_executable(${name} ../packet.c ../lib/parsers/hashmap.c ../lib/parsers/json.c ../log.c ../utils.c ${name}.c)
    target_include_directories(${name} PUBLIC ../lib/parsers)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_packet(test_packet)

function(add_test_time name)
    add_executable(${name} ${name}.c)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_time(test_time)

function(add_test_thread name)
    add_executable(${name} ../log.c ../thread_utils.c ../utils.c ../lib/parsers/hashmap.c ${name}.c)
    target_include_directories(${name} PUBLIC ../lib/parsers)
    target_link_libraries(${name} pthread)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_thread(test_thread)

function(add_test_util name)
    add_executable(${name} ../utils.c ../lib/parsers/hashmap.c ${name}.c)
    target_include_directories(${name} PUBLIC ../lib/parsers)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_util(test_utils)

