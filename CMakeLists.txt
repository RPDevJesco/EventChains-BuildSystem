cmake_minimum_required(VERSION 3.10)
project(EventChainsBuildSystem C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler-specific flags
if(MSVC)
    # MSVC (Windows)
    add_compile_options(/W4)  # Warning level 4
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)  # Disable secure CRT warnings

    # Fix incompatibility: /O2 and /RTC1 cannot be used together
    # Remove /RTC1 from Debug flags since we want full control
    string(REGEX REPLACE "/RTC[^ ]*" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REGEX REPLACE "/RTC[^ ]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Debug build: No optimization, with debug info
        add_compile_options(/Od /Zi)
    else()
        # Release build: Full optimization
        add_compile_options(/O2)
    endif()
else()
    # GCC/Clang (Linux/macOS/MinGW)
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O2)
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# EventChains core library (the pattern implementation)
add_library(eventchains STATIC
        include/eventchains.c
)

# Platform-specific threading library
if(NOT WIN32)
    # On Unix/Linux/macOS, link with pthread
    target_link_libraries(eventchains pthread)
endif()

# Add POSIX feature macro for strdup and other functions (non-MSVC)
if(NOT MSVC)
    target_compile_definitions(eventchains PRIVATE _POSIX_C_SOURCE=200809L)
endif()

# Build system library - the dependency resolver and compilation events
add_library(eventchains_build STATIC
        dependency_resolver.c
        compile_events.c
        eventchains_build.c
        eventchains_middleware.c
        cache_metadata.c
)

# Link build system library with EventChains core
target_link_libraries(eventchains_build PUBLIC eventchains)

# Add POSIX feature macro for build system library too (non-MSVC)
if(NOT MSVC)
    target_compile_definitions(eventchains_build PRIVATE _POSIX_C_SOURCE=200809L)
endif()

# Main executable - ecbuild
add_executable(ecbuild
        ecbuild.c
)
target_link_libraries(ecbuild eventchains_build)

# Test executable
add_executable(test_dependency_resolver
        test_dependency_resolver.c
)
target_link_libraries(test_dependency_resolver eventchains_build)

# Demo executable
add_executable(dependency_demo
        dependency_demo.c
)
target_link_libraries(dependency_demo eventchains_build)

# EventChains integration test
add_executable(test_eventchains_integration
        test_eventchains_integration.c
)
target_link_libraries(test_eventchains_integration eventchains_build)

# Persistent cache test
add_executable(test_persistent_cache
        test_persistent_cache.c
)
target_link_libraries(test_persistent_cache eventchains_build)

# Enable testing
enable_testing()
add_test(NAME DependencyResolverTests COMMAND test_dependency_resolver)

# Install targets
install(TARGETS eventchains eventchains_build
        ARCHIVE DESTINATION lib
)
install(FILES
        include/eventchains.h
        include/eventchains_platform.h
        dependency_resolver.h
        compile_events.h
        eventchains_build.h
        DESTINATION include/eventchains
)
install(TARGETS ecbuild test_dependency_resolver dependency_demo test_eventchains_integration
        RUNTIME DESTINATION bin
)

# Print configuration summary
message(STATUS "")
message(STATUS "EventChains Build System - Configuration Summary")
message(STATUS "================================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C Flags: ${CMAKE_C_FLAGS}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")