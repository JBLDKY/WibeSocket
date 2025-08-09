.PHONY: build test clean example

build:
	cmake -S . -B build
	cmake --build build -j

test: build
	ctest --test-dir build --output-on-failure

example: build
	./build/example_simple_echo ws://echo.websocket.events/

clean:
	rm -rf build

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD := $(ROOT)/build

.PHONY: build test docs clean

build:
	cmake -S $(ROOT) -B $(BUILD) -DCMAKE_BUILD_TYPE=Release $(EXTRA_CMAKE)
	cmake --build $(BUILD) -j

test: build
	bash $(ROOT)/scripts/dev_check.sh

docs:
	cd $(ROOT)/py && mkdocs build

clean:
	rm -rf $(BUILD)
	rm -rf $(ROOT)/.venv

# Sanitized build: make test SANITIZE=ON
ifdef SANITIZE
EXTRA_CMAKE := -DWS_SANITIZE=ON
endif


