# Host simulator shared library (loaded by Python via ctypes). Not part of the firmware image.
add_library(fbsim SHARED fbsim.c)
target_link_libraries(fbsim PRIVATE fake_flash fb_common monocypher)
target_include_directories(fbsim PRIVATE ../common ../tests)
set_target_properties(fbsim PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
