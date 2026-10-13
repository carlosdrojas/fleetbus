# FleetBus: build, test, demo.   `make help`
PY := .venv/bin/python
HOST := build/host
FW := build/fw
FW_M0 := build/fw-mspm0
export FLEETBUS_REQUIRE_DB ?= 1

.PHONY: help setup host-test fw fw-mspm0 go-test py-test hil dashboard e2e test demo demo-down loadgen db clean
help:
	@echo "setup      create .venv, install python deps"
	@echo "host-test  firmware host build + Unity tests for both board flash maps + simulator + fb_verify"
	@echo "fw         cross-build STM32G431 bootloader + apps (needs arm-none-eabi-gcc); prints size report"
	@echo "fw-mspm0   cross-build MSPM0G3507 (LaunchPad) images into build/fw-mspm0"
	@echo "go-test    backend tests with the race detector (needs: make db)"
	@echo "py-test    gateway pytest (reliability, OTA, signing cross-check)"
	@echo "hil        HIL tests against the simulator (HIL_CAN=slcan:/dev/tty... for a real board)"
	@echo "dashboard  npm ci + typecheck + build"
	@echo "e2e        full stack with 3 simulated cars (needs: make db)"
	@echo "test       everything except e2e"
	@echo "demo       start the stack + simulated cars, dashboard on :8080 (make demo-down to stop)"
	@echo "loadgen    ingest throughput + loss check against the demo database"

setup:
	python3 -m venv .venv && .venv/bin/pip install -q -e "gateway[dev]" "psycopg[binary]"
db:
	docker compose -f deploy/docker-compose.yml up -d --wait db
# Host tests run once per board flash map: build/host is the STM32 map (also used by the simulator, gateway and e2e),
# build/host-mspm0 is the MSPM0 map (1 KB sectors, nondeterministic erased flash).
host-test:
	cmake -S firmware -B $(HOST) -G Ninja -DFB_BOARD=stm32g4 >/dev/null && cmake --build $(HOST) && ctest --test-dir $(HOST) --output-on-failure
	cmake -S firmware -B $(HOST)-mspm0 -G Ninja -DFB_BOARD=mspm0g3507 >/dev/null && cmake --build $(HOST)-mspm0 && ctest --test-dir $(HOST)-mspm0 --output-on-failure
fw:
	cmake -S firmware -B $(FW) -G Ninja -DFB_BOARD=stm32g4 -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/firmware/cmake/arm-none-eabi.cmake >/dev/null && cmake --build $(FW)
fw-mspm0:
	cmake -S firmware -B $(FW_M0) -G Ninja -DFB_BOARD=mspm0g3507 -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/firmware/cmake/arm-none-eabi.cmake >/dev/null && cmake --build $(FW_M0)
go-test:
	cd backend && go vet ./... && go test -race -count=1 ./...
py-test: host-test
	$(PY) -m pytest gateway/tests -q
hil: host-test
	$(PY) -m pytest hil -q
dashboard:
	cd dashboard && npm ci --no-audit --no-fund && npm run build
e2e: host-test
	$(PY) -m pytest tests/e2e -q
test: host-test go-test py-test hil dashboard
demo:
	scripts/demo.sh up 3
demo-down:
	scripts/demo.sh down
loadgen:
	(cd backend && go build -o ../build/bin/ ./cmd/loadgen ./cmd/ingest) && build/bin/loadgen -target 127.0.0.1:9000 -gateways 4 -rate 5000 -duration 8s -dsn "postgres://fleetbus:fleetbus@localhost:5433/fleetbus?sslmode=disable"
clean:
	rm -rf build dashboard/dist dashboard/node_modules
