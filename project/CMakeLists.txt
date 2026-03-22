# Saints Row (Xbox 360, 2006) - ReXGlue Static Recompilation Project
cmake_minimum_required(VERSION 3.25)
project(saintsrow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find ReXGlue SDK
if(DEFINED ENV{REXSDK})
    list(PREPEND CMAKE_PREFIX_PATH $ENV{REXSDK})
endif()
find_package(rexglue REQUIRED)
if(NOT rexglue_FOUND)
    message(FATAL_ERROR "ReXGlue SDK was not found. Set REXSDK env var to the SDK install path.")
endif()

# User source files
set(SAINTSROW_SOURCES
    src/main.cpp
    src/stubs.cpp
)

# Rex app implementation (graphics, audio, overlay setup)
list(APPEND SAINTSROW_SOURCES ${REXGLUE_SHARE_DIR}/rex_app.cpp)

# Platform-specific entry point
if(WIN32)
    list(APPEND SAINTSROW_SOURCES ${REXGLUE_SHARE_DIR}/windowed_app_main_win.cpp)
else()
    list(APPEND SAINTSROW_SOURCES ${REXGLUE_SHARE_DIR}/windowed_app_main_posix.cpp)
endif()

# Include generated code
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../generated/sources.cmake")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../generated/sources.cmake)
    list(APPEND SAINTSROW_SOURCES ${GENERATED_SOURCES})
endif()

if(WIN32)
    add_executable(saintsrow WIN32 ${SAINTSROW_SOURCES})
else()
    add_executable(saintsrow ${SAINTSROW_SOURCES})
endif()

target_include_directories(saintsrow PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
)

target_link_libraries(saintsrow PRIVATE
    rex::core
    rex::system
    rex::kernel
    rex::filesystem
    rex::graphics
    rex::audio
    rex::input
    rex::ui
)

# Compiler options
target_compile_options(saintsrow PRIVATE
    -fno-strict-aliasing
    -ffp-model=strict
    -fno-char8_t
    $<$<CONFIG:DEBUG>:-g>
    $<$<CONFIG:DEBUG>:-O0>
    $<$<CONFIG:RELEASE>:-O3>
    $<$<CONFIG:RELEASE>:-DNDEBUG>
)

if(NOT MSVC)
    target_compile_options(saintsrow PRIVATE -msse4.1)
endif()

# Allow our stubs to override kernel functions
if(WIN32)
    target_link_options(saintsrow PRIVATE "LINKER:/force:multiple")
endif()

# Console test executable (no GUI, no GPU)
set(TEST_SOURCES src/test_boot.cpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../generated/sources.cmake")
    include(${CMAKE_CURRENT_SOURCE_DIR}/../generated/sources.cmake)
    list(APPEND TEST_SOURCES ${GENERATED_SOURCES})
endif()

add_executable(saintsrow_test ${TEST_SOURCES})
target_include_directories(saintsrow_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../generated
)
target_link_libraries(saintsrow_test PRIVATE
    rex::core
    rex::system
    rex::kernel
    rex::filesystem
    rex::audio
    rex::input
    rex::ui
    rex::graphics
)
target_compile_options(saintsrow_test PRIVATE
    -fno-strict-aliasing
    -ffp-model=strict
    -fno-char8_t
    $<$<CONFIG:RELEASE>:-O3>
    $<$<CONFIG:RELEASE>:-DNDEBUG>
)
if(NOT MSVC)
    target_compile_options(saintsrow_test PRIVATE -msse4.1)
endif()
if(WIN32)
    target_link_options(saintsrow_test PRIVATE "LINKER:/force:multiple")
endif()

# Codegen target
add_custom_target(saintsrow_codegen
    COMMAND $<TARGET_FILE:rex::rexglue> codegen ${CMAKE_CURRENT_SOURCE_DIR}/../config/saintsrow_rexglue.toml
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    COMMENT "Generating recompiled code for Saints Row"
    VERBATIM
)
