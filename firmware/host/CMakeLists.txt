# Host simulator shared library (loaded by Python via ctypes). Not part of the firmware image.
add_library(fbsim SHARED fbsim.cpp)
target_link_libraries(fbsim PRIVATE fb_common)
target_include_directories(fbsim PUBLIC .)
set_target_properties(fbsim PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_executable(gauge_shot gauge_shot.cpp)
target_link_libraries(gauge_shot PRIVATE fb_common)
