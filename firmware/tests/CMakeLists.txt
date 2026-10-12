# MOCK(M1/M4): tests use plain asserts until Unity is vendored in M2.
add_executable(test_common test_common.c)
target_link_libraries(test_common fb_common)
add_test(NAME common COMMAND test_common)
