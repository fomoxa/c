cmake_minimum_required(VERSION 3.16)

project(cyclone VERSION 0.1.0 LANGUAGES C)

# The Makefile beside this file is the canonical build: it compiles the
# library, runs every test and the sanitizer pass. This file exists for one
# reason - so a project that consumes Cyclone can `find_package(cyclone)` or
# FetchContent it and link `cyclone::cyclone` without hand-copying headers and
# without having to know that Windows needs ws2_32. It deliberately builds no
# tests; tests/package is a separate consumer project that CI configures
# against the installed result.

# Read by both this file and the Makefile so the two never keep two lists that
# drift apart. One path per line, nothing else - see the note in the Makefile.
file(STRINGS sources.txt CYCLONE_SOURCES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sources.txt)

add_library(cyclone STATIC ${CYCLONE_SOURCES})
add_library(cyclone::cyclone ALIAS cyclone)

set_target_properties(cyclone PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

# `src` is private: core.h, portable.h and transport/socket.h are internal and
# are never installed. Only `include` is part of the interface.
target_include_directories(cyclone
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The one piece of build knowledge a consumer cannot guess. PUBLIC so it
# travels with the imported target.
if(WIN32)
    target_link_libraries(cyclone PUBLIC ws2_32)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS cyclone
    EXPORT cyclone-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# include/cyclone.h is deliberately absent: it holds only the annotation
# markers cyclonec reads as source text, every one of which expands to
# nothing, and a developer copies it into their own project. Installing the
# `cyclone` subdirectory rather than all of `include` is what keeps it out.
install(DIRECTORY include/cyclone/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cyclone
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

install(EXPORT cyclone-targets
    FILE cyclone-targets.cmake
    NAMESPACE cyclone::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyclone)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cyclone-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cyclone-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyclone)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/cyclone-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/cyclone-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cyclone-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cyclone)
