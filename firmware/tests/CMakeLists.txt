# Host-side Unity tests for firmware/common (built with the host compiler).
set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
add_library(unity STATIC ${TP}/unity/src/unity.c)
target_include_directories(unity PUBLIC ${TP}/unity/src)

add_library(monocypher STATIC ${TP}/monocypher/src/monocypher.c ${TP}/monocypher/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC ${TP}/monocypher/src ${TP}/monocypher/src/optional)

add_library(fake_flash STATIC fake_flash.c image_builder.c)
target_link_libraries(fake_flash PUBLIC fb_common monocypher)
target_include_directories(fake_flash PUBLIC .)

foreach(t crc can_msgs fsm obd boot_meta image ota)
  add_executable(test_${t} test_${t}.c)
  target_link_libraries(test_${t} unity fake_flash fb_common monocypher)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
