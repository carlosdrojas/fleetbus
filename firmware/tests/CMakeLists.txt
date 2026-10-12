# Host-side Unity tests for firmware/common (built with the host compiler).
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
add_library(unity STATIC ${TP}/unity/src/unity.c)
target_include_directories(unity PUBLIC ${TP}/unity/src)

add_library(fake_flash STATIC fake_flash.c image_builder.c)
target_link_libraries(fake_flash PUBLIC fb_common monocypher)
target_include_directories(fake_flash PUBLIC .)

add_library(fbsim_static STATIC ../host/fbsim.c)
target_link_libraries(fbsim_static PUBLIC fake_flash fb_common monocypher)
target_include_directories(fbsim_static PUBLIC ../host ../common .)

foreach(t crc can_msgs fsm obd boot_meta image ota node_sim telematics)
  add_executable(test_${t} test_${t}.c)
  target_link_libraries(test_${t} unity fbsim_static fake_flash fb_common monocypher)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
