cmake_minimum_required(VERSION 3.16)

project(cyclone-package-test LANGUAGES C)

# Configured against an already-installed Cyclone, never against the source
# tree, so it fails whenever the install layout or the exported target is
# wrong: a missing header, a header that never got installed, or the ws2_32
# link requirement that Windows needs and only the exported target carries.
find_package(cyclone 0.1.0 REQUIRED)

add_executable(consume_c main.c)
target_link_libraries(consume_c PRIVATE cyclone::cyclone)

# The C++ wrapper is optional in the library, so it is optional here too:
# probe for the installed header rather than assuming it shipped.
get_target_property(CYCLONE_INCLUDES cyclone::cyclone INTERFACE_INCLUDE_DIRECTORIES)
find_file(CYCLONE_NET_HPP cyclone/net.hpp PATHS ${CYCLONE_INCLUDES} NO_DEFAULT_PATH)

if(CYCLONE_NET_HPP)
    enable_language(CXX)
    add_executable(consume_cpp main.cpp)
    set_target_properties(consume_cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(consume_cpp PRIVATE cyclone::cyclone)
    message(STATUS "cyclone/net.hpp found, building the C++ consumer too")
else()
    message(STATUS "cyclone/net.hpp not installed, skipping the C++ consumer")
endif()
