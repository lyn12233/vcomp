cmake_minimum_required(VERSION 3.10)

add_executable(test_ent test_ent.cpp)
target_link_libraries(test_ent PRIVATE vcomp_math)
add_executable(test_ent2 test_ent2.c)
target_link_libraries(test_ent2 PRIVATE vcomp_math_asan)
add_executable(test_pixbuf test_pixbuf.c)
target_link_libraries(test_pixbuf PRIVATE vcomp_util_asan)
add_executable(test_tx test_tx.c)
target_link_libraries(test_tx PRIVATE vcomp_math_asan)

foreach(targ test_ent)
    target_link_libraries(${targ} PRIVATE vcomp_flags)
endforeach()
foreach(targ test_ent2 test_pixbuf test_tx)
    target_link_libraries(${targ} PRIVATE vcomp_flags_asan)
    add_custom_command(TARGET ${targ} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASAN_DLL}" "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E echo copying ${ASAN_DLL} to ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()
    
foreach(targ test_ent)
    target_link_libraries(${targ} PRIVATE GTest::gtest GTest::gtest_main)
    # gtest_discover_tests(${targ})
endforeach()
