cmake_minimum_required(VERSION 3.10)

add_executable(test_ent test_ent.cpp)
target_link_libraries(test_ent PRIVATE vcomp_math vcomp_util)
add_executable(test_ent2 test_ent2.c)
target_link_libraries(test_ent2 PRIVATE vcomp_math vcomp_util)
add_executable(test_pixbuf test_pixbuf.c)
target_link_libraries(test_pixbuf PRIVATE vcomp_util)
add_executable(test_tx test_tx.c)
target_link_libraries(test_tx PRIVATE vcomp_util vcomp_math)

foreach(targ test_ent test_ent2 test_pixbuf test_tx)
    target_link_libraries(${targ} PRIVATE vcomp_flags)
endforeach()

foreach(targ test_ent)
    target_link_libraries(${targ} PRIVATE GTest::gtest GTest::gtest_main)
    # gtest_discover_tests(${targ})
endforeach()

if(VCOMP_ENABLE_ASAN)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND CMAKE_SYSTEM_NAME MATCHES "[Ww]indows")
        foreach(targ test_ent2 test_pixbuf test_tx)
            target_link_libraries(${targ} PRIVATE "${ASAN_LIB}")
            target_compile_options(${targ} PRIVATE "-fsanitize=address")
            target_compile_options(${targ} PRIVATE "-fno-omit-frame-pointer")
            target_link_options(${targ} PRIVATE "-fsanitize=address")
            target_link_options(${targ} PRIVATE "-fno-omit-frame-pointer")
            add_custom_command(TARGET ${targ} PRE_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASAN_DLL}" "${CMAKE_CURRENT_BINARY_DIR}"
                COMMAND ${CMAKE_COMMAND} -E echo copying ${ASAN_DLL} to ${CMAKE_CURRENT_BINARY_DIR}
            )
        endforeach()
    endif()
endif()