# Host-side Unity tests for firmware/common (built with the host compiler).
add_library(unity STATIC ${TP}/unity/src/unity.c)
target_include_directories(unity PUBLIC ${TP}/unity/src)

add_executable(test_can_msgs test_can_msgs.c)
target_link_libraries(test_can_msgs unity fb_common)
add_test(NAME can_msgs COMMAND test_can_msgs)

foreach(t fsm node gauge)
  add_executable(test_${t} test_${t}.cpp)
  target_link_libraries(test_${t} unity fb_common)
  target_include_directories(test_${t} PRIVATE .)
  target_compile_options(test_${t} PRIVATE -Wall -Wextra)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
