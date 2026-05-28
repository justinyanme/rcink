SHELL := /bin/bash
SHELLFLAGS := -euo pipefail -c

.DEFAULT_GOAL := all

PYTHON ?= python3
BUILD_DIR ?= build
GENERATOR ?= Ninja
TOOLCHAIN_PREFIX ?= $(HOME)/w/tc32
SDK_PREFIX ?= $(HOME)/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk
MANUFACTURER_CODE ?= 0x1201
EPD_PAD_TOP ?= 8
# Peek mode: after the 5-min boot sync window, the tag wakes briefly every
# BLE_PEEK_INTERVAL_SECONDS to advertise (default 5 s / 60 s in src/CMakeLists.txt).
# Required for any scheduled / auto-update workflow; tradeoff is higher idle
# current than deep-sleep-forever. Override with ENABLE_BLE_PEEK=OFF make ...
ENABLE_BLE_PEEK ?= ON
CMAKE ?= cmake
BUILD_TARGETS ?= TLSR-LAYOUT TLSR-LAYOUT.bin TLSR-LAYOUT.ota
EXTRA_CMAKE_ARGS ?=

# Flashing over the Telink BDT debugger (reverse-engineered tool).
BURN_TOOL ?= $(HOME)/w/telink_burn_tool
FIRMWARE_BIN ?= $(abspath $(BUILD_DIR)/src/TLSR-LAYOUT.bin)

.PHONY: all configure build clean flash

all: build

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
	  -DTOOLCHAIN_PREFIX="$(TOOLCHAIN_PREFIX)" \
	  -DSDK_PREFIX="$(SDK_PREFIX)" \
	  -DMANUFACTURER_CODE=$(MANUFACTURER_CODE) \
	  -DEPD_PAD_TOP=$(EPD_PAD_TOP) \
	  -DENABLE_BLE_PEEK=$(ENABLE_BLE_PEEK) \
	  $(EXTRA_CMAKE_ARGS)

build: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) --target $(BUILD_TARGETS)

clean:
	rm -rf "$(BUILD_DIR)"

# Flash the prebuilt image to a chip connected via the BDT debugger.
# Run `make build` first. The tool self-activates, programs, then resets to boot.
flash:
	@test -f "$(FIRMWARE_BIN)" || { echo "Missing $(FIRMWARE_BIN) — run 'make build' first"; exit 1; }
	uv run --directory "$(BURN_TOOL)" python telink_tool.py -f "$(FIRMWARE_BIN)"
