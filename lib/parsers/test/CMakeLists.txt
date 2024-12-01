cmake_minimum_required(VERSION 3.10)

project(Parser_Testing)

include_directories(../)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -std=c99 -Wall -Wextra -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wshadow -Wvla -Wc++-compat -Werror")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fanalyzer")

#TODO enable these to compile as C++ (-Wc++-compat doesn't complain about everything)
#file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/../*.c")
#SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX )
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Og -std=gnu++03 -Wall -Wextra -Wmissing-declarations -Wwrite-strings -Wshadow -Wvla -Werror")

function(add_test_hashmap name)
    add_executable(${name} ../hashmap.c ${name}.c)
    target_link_libraries(${name} m)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_hashmap(hashmap)

function(add_test_inifile name)
    add_executable(${name} ../inifile.c ../hashmap.c ${name}.c)
    target_link_libraries(${name} m)
    target_compile_definitions(${name} PUBLIC TESTING)
    target_compile_definitions(${name} PUBLIC SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/inisamples")
    target_compile_definitions(${name} PUBLIC TEMP_DIR="${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

add_test_inifile(inifile)

function(add_test_json name)
    add_executable(${name} ../json.c ../hashmap.c ${name}.c)
    target_link_libraries(${name} m)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_json(json_api)
add_test_json(json_read)
add_test_json(json_write)

function(add_test_parserutils name)
    add_executable(${name} ${name}.c)
    target_compile_definitions(${name} PUBLIC TESTING)
endfunction()

add_test_parserutils(parserutils)
