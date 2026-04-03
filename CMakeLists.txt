cmake_minimum_required(VERSION 3.28)

project(vcomp)

enable_testing()
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
include(CheckIncludeFile)
include(GoogleTest)

add_library(vcomp_flags INTERFACE)

# 1. config
set(VCOMP_ENABLE_TESTS ON CACHE BOOL "build tests")
set(VCOMP_ENABLE_LOGGING ON CACHE BOOL "print error messages using provided callback")

# 2. global incdir
target_include_directories(vcomp_flags INTERFACE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

# 3. handle option
target_compile_definitions(vcomp_flags INTERFACE 
    $<$<BOOL:${VCOMP_ENABLE_LOGGING}>:CONFIG_LOG=ON>
)
target_compile_definitions(vcomp_flags INTERFACE
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0601>
    $<$<PLATFORM_ID:Windows>:UNICODE=1>
    $<$<PLATFORM_ID:Windows>:_CRT_DECLARE_NONSTDC_NAMES=1> # for posix names
    $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS=1> # yah
    $<$<PLATFORM_ID:Windows>:fseeko=_fseeki64;ftello=_ftelli64>
)

# 4. libs
file(GLOB vcomp_math_src "src/math/*.c")
add_library(vcomp_math ${vcomp_math_src})
target_include_directories(vcomp_math PRIVATE "src/math")
target_link_libraries(vcomp_math PRIVATE vcomp_flags)

file(GLOB vcomp_util_src "src/util/*.c")
add_library(vcomp_util ${vcomp_util_src})
target_include_directories(vcomp_util PRIVATE "src/util")
target_link_libraries(vcomp_util PRIVATE vcomp_flags)

# file(GLOB vcomp_enc_src "src/encode/*.c")
# file(GLOB vcomp_dec_src "src/decode/*.c")

if(VCOMP_ENABLE_TESTS)
    # this is a test
    # add_executable(hello hello.cpp)
    # target_link_libraries(hello PRIVATE fmt::fmt)
    # target_link_libraries(hello PRIVATE spdlog::spdlog)
    add_subdirectory(test)
endif()
