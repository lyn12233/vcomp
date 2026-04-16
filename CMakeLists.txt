cmake_minimum_required(VERSION 3.28)

project(vcomp)

enable_testing()
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
include(CheckIncludeFile)
include(GoogleTest)

add_library(vcomp_flags INTERFACE)
add_library(vcomp_flags_asan INTERFACE)

# 1. config
set(VCOMP_ENABLE_TESTS ON CACHE BOOL "build tests")
set(VCOMP_ENABLE_LOGGING ON CACHE BOOL "print error messages using provided callback")
set(VCOMP_ENABLE_ASAN ON CACHE BOOL "enable asan")

# 2. global incdir
foreach(targ vcomp_flags vcomp_flags_asan)
    target_include_directories(${targ} INTERFACE 
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
    )

    # 3. handle option
    target_compile_definitions(${targ} INTERFACE 
        $<$<BOOL:${VCOMP_ENABLE_LOGGING}>:CONFIG_LOG=ON>
    )
    target_compile_definitions(${targ} INTERFACE
        $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0601>
        $<$<PLATFORM_ID:Windows>:UNICODE=1>
        $<$<PLATFORM_ID:Windows>:_CRT_DECLARE_NONSTDC_NAMES=1> # for posix names
        $<$<PLATFORM_ID:Windows>:_CRT_SECURE_NO_WARNINGS=1> # yah
        $<$<PLATFORM_ID:Windows>:fseeko=_fseeki64;ftello=_ftelli64>
    )
endforeach()

if(VCOMP_ENABLE_ASAN)
    if(CMAKE_BUILD_TYPE MATCHES "[Dd]ebug")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_SYSTEM_NAME MATCHES "[Ww]indows")
            message(STATUS "[3] enabling asan")
            # find asan lib
            execute_process(
                COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
                OUTPUT_VARIABLE CLANG_RESOURCE_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if (CMAKE_SIZEOF_VOID_P EQUAL 8) # x86-64
                set(ASAN_LIB "${CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan_dynamic-x86_64.lib")
                set(ASAN_DLL "${CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
            else()
                set(ASAN_LIB "${CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan_dynamic-i386.lib")
                set(ASAN_DLL "${CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan_dynamic-i386.dll")
            endif()
            string(REPLACE "\\" "/" ASAN_LIB "${ASAN_LIB}")
            string(REPLACE "\\" "/" ASAN_DLL "${ASAN_DLL}")
            message(STATUS "[3] asan lib: ${ASAN_LIB}")
            if(EXISTS "${ASAN_LIB}" AND EXISTS "${ASAN_DLL}")
                message(STATUS "[3] asan lib found")
            else()
                message(FATAL_ERROR "[3] asan lib not found")
            endif()
            # compiler flags not set globally because vcpkg+gtest may be weird
        endif()
    else()
        message(STATUS "[3] VCOMP_ENABLE_ASAN is on, but disabled for non debug build")
        set(VCOMP_ENABLE_ASAN OFF)
    endif()
endif()

# 4. libs
# note gtest is not working correct with asan
file(GLOB vcomp_util_src "src/util/*.c")
add_library(vcomp_util ${vcomp_util_src})
target_include_directories(vcomp_util PRIVATE "src/util")
target_link_libraries(vcomp_util PRIVATE vcomp_flags)

add_library(vcomp_util_asan ${vcomp_util_src})
target_include_directories(vcomp_util_asan PRIVATE "src/util")
target_link_libraries(vcomp_util_asan PRIVATE vcomp_flags_asan)

file(GLOB vcomp_math_src "src/math/*.c")
add_library(vcomp_math ${vcomp_math_src})
target_include_directories(vcomp_math PRIVATE "src/math")
target_link_libraries(vcomp_math PRIVATE vcomp_flags)
target_link_libraries(vcomp_math PRIVATE vcomp_util)

add_library(vcomp_math_asan ${vcomp_math_src})
target_include_directories(vcomp_math_asan PRIVATE "src/math")
target_link_libraries(vcomp_math_asan PRIVATE vcomp_flags_asan)
target_link_libraries(vcomp_math_asan PRIVATE vcomp_util_asan)

file(GLOB vcomp_enc_src "src/encode/*.c")
add_library(vcomp_enc ${vcomp_enc_src})
target_include_directories(vcomp_enc PRIVATE "src/encoder")
target_link_libraries(vcomp_enc PRIVATE vcomp_flags)
target_link_libraries(vcomp_enc PRIVATE vcomp_math vcomp_util)

add_library(vcomp_enc_asan ${vcomp_enc_src})
target_include_directories(vcomp_enc_asan PRIVATE "src/encoder")
target_link_libraries(vcomp_enc_asan PRIVATE vcomp_flags_asan)
target_link_libraries(vcomp_enc_asan PRIVATE vcomp_math_asan vcomp_util_asan)

# file(GLOB vcomp_dec_src "src/decode/*.c")


if(VCOMP_ENABLE_ASAN)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_SYSTEM_NAME MATCHES "[Ww]indows")
        target_link_libraries(vcomp_flags_asan INTERFACE "${ASAN_LIB}")
        target_compile_options(vcomp_flags_asan INTERFACE "-fsanitize=address")
        target_compile_options(vcomp_flags_asan INTERFACE "-fno-omit-frame-pointer")
        target_link_options(vcomp_flags_asan INTERFACE "-fsanitize=address")
        target_link_options(vcomp_flags_asan INTERFACE "-fno-omit-frame-pointer")
    endif()
endif()

if(VCOMP_ENABLE_TESTS)
    # this is a test
    # add_executable(hello hello.cpp)
    # target_link_libraries(hello PRIVATE fmt::fmt)
    # target_link_libraries(hello PRIVATE spdlog::spdlog)
    add_subdirectory(test)
endif()
