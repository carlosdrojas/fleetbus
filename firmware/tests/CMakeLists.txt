# Host-side Unity tests for firmware/common (built with the host compiler).
add_library(unity STATIC ${TP}/unity/src/unity.c)
target_include_directories(unity PUBLIC ${TP}/unity/src)

foreach(t can_msgs fsm node gauge)
  add_executable(test_${t} test_${t}.cpp)
  target_link_libraries(test_${t} unity fb_common)
  target_include_directories(test_${t} PRIVATE .)
  target_compile_options(test_${t} PRIVATE -Wall -Wextra)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
