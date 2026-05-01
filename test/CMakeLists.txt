cmake_minimum_required(VERSION 3.10)

add_executable(test_ent test_ent.cpp)
target_link_libraries(test_ent PRIVATE vcomp_math)
add_executable(test_ent2 test_ent2.c)
target_link_libraries(test_ent2 PRIVATE vcomp_math_asan)
add_executable(test_pixbuf test_pixbuf.c)
target_link_libraries(test_pixbuf PRIVATE vcomp_util_asan)
add_executable(test_tx test_tx.c)
target_link_libraries(test_tx PRIVATE vcomp_math_asan)
add_executable(test_blockd test_block_data.c)
target_link_libraries(test_blockd PRIVATE vcomp_enc_asan)
add_executable(test_mem test_mem.c)
target_link_libraries(test_mem PRIVATE vcomp_util_asan)
add_executable(test_search test_search.c)
target_link_libraries(test_search PRIVATE vcomp_util_asan vcomp_enc_asan)
add_executable(test_pred test_pred.cpp)
target_link_libraries(test_pred PRIVATE vcomp_util_asan vcomp_enc_asan)

# for targets without asan
foreach(targ test_ent)
    target_link_libraries(${targ} PRIVATE vcomp_flags)
endforeach()

#for targets with asan
foreach(targ 
    test_ent2 test_pixbuf test_tx test_blockd test_mem test_search test_pred
)
    target_link_libraries(${targ} PRIVATE vcomp_flags_asan)
    add_custom_command(TARGET ${targ} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ASAN_DLL}" "${CMAKE_CURRENT_BINARY_DIR}"
    )
endforeach()
    
# for targets with gtest
foreach(targ test_ent)
    target_link_libraries(${targ} PRIVATE GTest::gtest GTest::gtest_main)
    # gtest_discover_tests(${targ})
endforeach()

# for targets with opencv
foreach(targ test_pred)
    target_link_libraries(${targ} PRIVATE ${OpenCV_LIBS})
endforeach()