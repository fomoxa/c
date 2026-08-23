cmake_minimum_required(VERSION 3.16)

project(fomoxa-package-test LANGUAGES C)

# Configured against an already-installed Fomoxa, never against the source
# tree, so it fails whenever the install layout or the exported target is
# wrong: a missing header, a header that never got installed, or the ws2_32
# link requirement that Windows needs and only the exported target carries.
find_package(fomoxa 0.1.0 REQUIRED)

add_executable(consume_c main.c)
target_link_libraries(consume_c PRIVATE fomoxa::fomoxa)

# The C++ wrapper is optional in the library, so it is optional here too:
# probe for the installed header rather than assuming it shipped.
get_target_property(FOMOXA_INCLUDES fomoxa::fomoxa INTERFACE_INCLUDE_DIRECTORIES)
find_file(FOMOXA_NET_HPP fomoxa/net.hpp PATHS ${FOMOXA_INCLUDES} NO_DEFAULT_PATH)

if(FOMOXA_NET_HPP)
    enable_language(CXX)
    add_executable(consume_cpp main.cpp)
    set_target_properties(consume_cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(consume_cpp PRIVATE fomoxa::fomoxa)
    message(STATUS "fomoxa/net.hpp found, building the C++ consumer too")
else()
    message(STATUS "fomoxa/net.hpp not installed, skipping the C++ consumer")
endif()
