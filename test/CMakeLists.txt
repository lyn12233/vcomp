cmake_minimum_required(VERSION 3.10)

add_executable(test_ent test_ent.cpp)
target_link_libraries(test_ent PRIVATE vcomp_math)

foreach(targ test_ent)
    target_link_libraries(${targ} PRIVATE vcomp_flags)
    target_link_libraries(${targ} PRIVATE GTest::gtest GTest::gtest_main)
    gtest_discover_tests(${targ})
endforeach()
