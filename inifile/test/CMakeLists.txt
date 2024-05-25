cmake_minimum_required(VERSION 3.10)

project(INI_Testing)

include_directories(../)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -std=c99 -Wall -Wextra -Wstrict-prototypes -Wmissing-declarations -Wwrite-strings -Wshadow -Wvla -Werror")

#set(MY_EXECUTABLES "")

function(add_test_hashmap name)
    add_executable(${name} ../hashmap.c ${name}.c)
    target_link_libraries(${name} m)
    target_compile_definitions(${name} PUBLIC TESTING)
    #set(MY_EXECUTABLES ${MY_EXECUTABLES} "${name}${CMAKE_EXECUTABLE_SUFFIX}" PARENT_SCOPE)
endfunction()

add_test_hashmap(hashmap)

function(add_test_inifile name)
    add_executable(${name} ../inifile.c ../hashmap.c ${name}.c)
    target_link_libraries(${name} m)
    target_compile_definitions(${name} PUBLIC TESTING)
    target_compile_definitions(${name} PUBLIC SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/inisamples")
    #set(MY_EXECUTABLES ${MY_EXECUTABLES} "${name}${CMAKE_EXECUTABLE_SUFFIX}" PARENT_SCOPE)
endfunction()

add_test_inifile(inifile)


