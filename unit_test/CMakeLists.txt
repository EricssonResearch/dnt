cmake_minimum_required(VERSION 3.10)

project(R2DTWO_Testing)

include_directories(../)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Og -std=gnu99 -Wall -Wextra -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wshadow -Werror")

function(add_test_seqrcvy name)
    add_executable(${name} ../seq_recov.c ../json.c ../log.c ../packet.c ../pipeline.c ../thread_utils.c ../utils.c ../inifile/hashmap.c ${name}.c)
    target_include_directories(${name} PUBLIC ../inifile)
    target_link_libraries(${name} pthread)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_seqrcvy(test_seq_rcvy_match)
add_test_seqrcvy(test_seq_rcvy_vector)
#TODO seamless

function(add_test_json name)
    add_executable(${name} ../json.c ../inifile/hashmap.c ../utils.c ${name}.c)
    target_include_directories(${name} PUBLIC ../inifile)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_json(test_json_read)
add_test_json(test_json_write)

function(add_test_time name)
    add_executable(${name} ${name}.c)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_time(test_time)

function(add_test_thread name)
    add_executable(${name} ../log.c ../thread_utils.c ../utils.c ../inifile/hashmap.c ${name}.c)
    target_include_directories(${name} PUBLIC ../inifile)
    target_link_libraries(${name} pthread)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_thread(test_thread)

