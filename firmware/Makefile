SHELL := /bin/bash
SHELLFLAGS := -euo pipefail -c

.DEFAULT_GOAL := all

PYTHON ?= python3
BUILD_DIR ?= build
GENERATOR ?= Ninja
TOOLCHAIN_PREFIX ?= $(HOME)/w/tc32
SDK_PREFIX ?= $(HOME)/w/telink_b85m_ble_single_connection_sdk/b85_ble_sdk
MANUFACTURER_CODE ?= 0x1201
QUOTES_FILE ?= rocry.txt
EPD_PAD_TOP ?= 8
CMAKE ?= cmake
BUILD_TARGETS ?= TLSR-PRICE-TAG TLSR-PRICE-TAG.bin TLSR-PRICE-TAG.ota
EXTRA_CMAKE_ARGS ?=

PREVIEW_BUILD_DIR ?= sim/quote_preview/build
PREVIEW_QUOTES_FILE ?= $(QUOTES_FILE)
PREVIEW_SET_NAME := $(basename $(notdir $(PREVIEW_QUOTES_FILE)))
PREVIEW_OUTPUT ?= out/quote_preview.png
PREVIEW_OUTPUT_DIR ?= out/$(PREVIEW_SET_NAME)
PREVIEW_QUOTE ?=
PREVIEW_LIMIT ?= 100
PREVIEW_BATTERY ?= 3300
PREVIEW_TEMPERATURE ?= 25
PREVIEW_TIME ?=
PREVIEW_FULL_REFRESH ?= 0
PREVIEW_FORCE_STACK ?=
PREVIEW_FONT_FORCE_STACK ?=
ifeq ($(strip $(PREVIEW_FORCE_STACK)),)
  ifeq ($(strip $(PREVIEW_FONT_FORCE_STACK)),)
    PREVIEW_FORCE_STACK_EFFECTIVE := AUTO
  else
    PREVIEW_FORCE_STACK_EFFECTIVE := $(strip $(PREVIEW_FONT_FORCE_STACK))
  endif
else
  PREVIEW_FORCE_STACK_EFFECTIVE := $(strip $(PREVIEW_FORCE_STACK))
endif
PREVIEW_ARGS ?=

.PHONY: all configure build clean quote-preview

all: build

configure: $(BUILD_DIR)/CMakeCache.txt

$(BUILD_DIR)/CMakeCache.txt:
	$(CMAKE) -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
	  -DTOOLCHAIN_PREFIX="$(TOOLCHAIN_PREFIX)" \
	  -DSDK_PREFIX="$(SDK_PREFIX)" \
	  -DMANUFACTURER_CODE=$(MANUFACTURER_CODE) \
	  -DQUOTES_FILE="$(QUOTES_FILE)" \
	  -DEPD_PAD_TOP=$(EPD_PAD_TOP) \
	  $(EXTRA_CMAKE_ARGS)

build: $(BUILD_DIR)/CMakeCache.txt
	$(CMAKE) --build $(BUILD_DIR) --target $(BUILD_TARGETS)

clean:
	rm -rf "$(BUILD_DIR)"

quote-preview:
ifeq ($(strip $(PREVIEW_QUOTE)),)
	mkdir -p "$(PREVIEW_OUTPUT_DIR)"
	$(PYTHON) tools/quote_preview.py render_all \
	  --output-dir "$(PREVIEW_OUTPUT_DIR)" \
	  --build-dir "$(PREVIEW_BUILD_DIR)" \
	  --battery-mv $(PREVIEW_BATTERY) \
	  --temperature-c $(PREVIEW_TEMPERATURE) \
	  --limit $(PREVIEW_LIMIT) \
	  $(if $(filter 1 true TRUE,$(PREVIEW_FULL_REFRESH)),--full-refresh,) \
	  $(if $(strip $(PREVIEW_TIME)),--time-iso "$(PREVIEW_TIME)",) \
	  $(if $(strip $(PREVIEW_QUOTES_FILE)),--quotes-file "$(PREVIEW_QUOTES_FILE)",) \
	  --font-force-stack "$(PREVIEW_FORCE_STACK_EFFECTIVE)" \
	  $(PREVIEW_ARGS)
else
	mkdir -p "$(dir $(PREVIEW_OUTPUT))"
	$(PYTHON) tools/quote_preview.py render $(PREVIEW_QUOTE) \
	  --output "$(PREVIEW_OUTPUT)" \
	  --build-dir "$(PREVIEW_BUILD_DIR)" \
	  --battery-mv $(PREVIEW_BATTERY) \
	  --temperature-c $(PREVIEW_TEMPERATURE) \
	  $(if $(filter 1 true TRUE,$(PREVIEW_FULL_REFRESH)),--full-refresh,) \
	  $(if $(strip $(PREVIEW_TIME)),--time-iso "$(PREVIEW_TIME)",) \
	  $(if $(strip $(PREVIEW_QUOTES_FILE)),--quotes-file "$(PREVIEW_QUOTES_FILE)",) \
	  --font-force-stack "$(PREVIEW_FORCE_STACK_EFFECTIVE)" \
	  $(PREVIEW_ARGS)
endif
