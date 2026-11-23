# FleetBus Lite: build, test, flash.   `make help`
PY := .venv/bin/python
HOST := build/host
FW := build/fw-mspm0

.PHONY: help setup host-test fw py-test test dash gauge-shots clean
help:
	@echo "setup        create .venv, install the dashboard's python deps"
	@echo "host-test    host build of the node logic + Unity tests (no hardware)"
	@echo "fw           cross-build the MSPM0G3507 images into $(FW) (needs arm-none-eabi-gcc/g++); prints sizes"
	@echo "py-test      dashboard tests (decoder, simulator, HTTP)"
	@echo "test         host-test + py-test + fw"
	@echo "dash         live dashboard on :8000 against the simulator (real bus: tools/lite_dash.py --can slcan:/dev/...)"
	@echo "gauge-shots  render the display's four screens to docs/img/ with the firmware's own renderer"
	@echo "flash:       tools/flash_mspm0.sh bench bringup | tools/flash_mspm0.sh node battery|body"

setup:
	python3 -m venv .venv && .venv/bin/pip install -q -e "dash[dev]"
host-test:
	cmake -S firmware -B $(HOST) -G Ninja >/dev/null && cmake --build $(HOST) && ctest --test-dir $(HOST) --output-on-failure
fw:
	cmake -S firmware -B $(FW) -G Ninja -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/firmware/cmake/arm-none-eabi.cmake >/dev/null && cmake --build $(FW)
py-test: host-test
	$(PY) -m pytest dash/tests -q
test: py-test fw
dash: host-test
	$(PY) tools/lite_dash.py --sim
gauge-shots: host-test
	$(PY) tools/gauge_shots.py
clean:
	rm -rf build
