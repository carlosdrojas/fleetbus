# C++ firmware control logic

Implemented and verified in software on 2026-11-06. Hardware validation is still pending.

The battery fault controller (`firmware/common/fault_fsm.cpp`) and body limp-home
controller (`firmware/common/body_fsm.cpp`) are C++17 implementations compiled into
both the actual board images and the host simulator. Their C entry points and POD
state layouts remain stable, so the existing C node core and Python ctypes simulator
execute the same C++ logic. The classes use references to caller-owned state,
`noexcept` methods, and fixed-size storage, with no allocation or virtual dispatch.
Exceptions, RTTI, and thread-safe static initialization are disabled for this library.
The drivers, RTOS, bootloader and other common modules remain C.

The signed overcurrent check now handles INT32_MIN without signed negation overflow.
Regression cases cover both extreme current values and body recovery across timer wrap.

## Reproduce

- `make host-test`: ten Unity suites for each board flash map.
- `make fw-mspm0`: primary MSPM0 firmware, including Lite battery/body images.
- `make fw`: secondary STM32 firmware.
- `make py-test`: Python gateway and simulator tests.

Cross builds require both `arm-none-eabi-gcc` and `arm-none-eabi-g++`, plus newlib
and the matching C++ cross libraries. CMake uses C++17 and retains the Cortex-M0+
soft-float / Cortex-M4F hard-float toolchain flags. CI dependencies were updated;
GitHub CI has not been run for this change.

## Verification this session

- All ten Unity suites passed on each board flash map, including node simulation,
  boot/OTA, display and the C-to-C++ calls.
- All MSPM0 and STM32 default image targets compiled and linked within their budgets.
- 22 Python tests passed across `test_gateway_sim.py`, `test_lite_dash.py`, and
  `test_ota_e2e.py`, using the newly built shared library through ctypes.
- Database-backed system end-to-end scenarios were not rerun for this change.

Accurate description: "Implemented C++17 battery fault and limp-home controllers
in FreeRTOS CAN firmware, retaining C interfaces and verifying the same logic in
host simulation and cross-compiled MSPM0/STM32 images." This is software verification;
it does not establish that the firmware has run on hardware.
