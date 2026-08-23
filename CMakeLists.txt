cmake_minimum_required(VERSION 3.16)

project(fomoxa VERSION 0.1.0 LANGUAGES C)

# The Makefile beside this file is the canonical build: it compiles the
# library, runs every test and the sanitizer pass. This file exists for one
# reason - so a project that consumes Fomoxa can `find_package(fomoxa)` or
# FetchContent it and link `fomoxa::fomoxa` without hand-copying headers and
# without having to know that Windows needs ws2_32. It deliberately builds no
# tests; tests/package is a separate consumer project that CI configures
# against the installed result.

# Read by both this file and the Makefile so the two never keep two lists that
# drift apart. One path per line, nothing else - see the note in the Makefile.
file(STRINGS sources.txt FOMOXA_SOURCES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sources.txt)

add_library(fomoxa STATIC ${FOMOXA_SOURCES})
add_library(fomoxa::fomoxa ALIAS fomoxa)

set_target_properties(fomoxa PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

# `src` is private: core.h, portable.h and transport/socket.h are internal and
# are never installed. Only `include` is part of the interface.
target_include_directories(fomoxa
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The one piece of build knowledge a consumer cannot guess. PUBLIC so it
# travels with the imported target.
if(WIN32)
    target_link_libraries(fomoxa PUBLIC ws2_32)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS fomoxa
    EXPORT fomoxa-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# include/fomoxa.h is deliberately absent: it holds only the annotation
# markers fomoxac reads as source text, every one of which expands to
# nothing, and a developer copies it into their own project. Installing the
# `fomoxa` subdirectory rather than all of `include` is what keeps it out.
install(DIRECTORY include/fomoxa/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fomoxa
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

install(EXPORT fomoxa-targets
    FILE fomoxa-targets.cmake
    NAMESPACE fomoxa::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fomoxa)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fomoxa-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/fomoxa-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fomoxa)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/fomoxa-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/fomoxa-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/fomoxa-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fomoxa)
