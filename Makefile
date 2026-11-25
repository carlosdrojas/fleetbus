# FleetBus Lite: build, test, flash.   `make help`
HOST := build/host
FW := build/fw-mspm0
CAN_KBPS ?= 500

.PHONY: help host-test fw test clean
help:
	@echo "host-test    node logic + Unity tests on the host (no hardware)"
	@echo "fw           cross-build the MSPM0G3507 images into $(FW) (arm-none-eabi-gcc/g++); prints sizes"
	@echo "test         host-test + fw"
	@echo "flash:       tools/flash_mspm0.sh bench bringup | tools/flash_mspm0.sh node battery|body"

host-test:
	cmake -S firmware -B $(HOST) -G Ninja >/dev/null && cmake --build $(HOST) && ctest --test-dir $(HOST) --output-on-failure
fw:
	cmake -S firmware -B $(FW) -G Ninja -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/firmware/cmake/arm-none-eabi.cmake -DFB_CAN_KBPS=$(CAN_KBPS) >/dev/null && cmake --build $(FW)
test: host-test fw
clean:
	rm -rf build
