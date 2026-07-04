set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

pio := env_var_or_default("PIO_BIN", "pio")
pio-run := "./scripts/pio-run.sh"

default:
    @just --list

lint:
    cd producer && uv run ruff format --check .
    cd producer && uv run ruff check .

producer-test:
    cd producer && uv run pytest

native-s3:
    {{ pio }} test -d firmware/s3 -e native

native-t-amoled:
    {{ pio }} test -d firmware/t-amoled -e native

native-c3-x4:
    {{ pio }} test -d firmware/c3-x4 -e native

native-rlcd:
    {{ pio }} test -d firmware/rlcd -e native

native-epd397:
    {{ pio }} test -d firmware/epd397 -e native

tlsr-host-test:
    cmake -S firmware/tlsr/firmware/host_test -B firmware/tlsr/firmware/host_test/build
    cmake --build firmware/tlsr/firmware/host_test/build --target host_test
    uv run --directory firmware/tlsr/firmware/host_test pytest

test: producer-test native-s3 native-t-amoled native-c3-x4 native-rlcd native-epd397 tlsr-host-test

build-s3:
    {{ pio-run }} s3 run -d firmware/s3

build-t-amoled:
    {{ pio-run }} t-amoled run -d firmware/t-amoled

build-c3-x4:
    {{ pio-run }} c3-x4 run -d firmware/c3-x4

build-rlcd:
    {{ pio-run }} rlcd run -d firmware/rlcd

build-epd397:
    {{ pio-run }} epd397 run -d firmware/epd397

build: build-s3 build-t-amoled build-c3-x4 build-rlcd build-epd397

goldens:
    cd producer && uv run rcink-goldens regen
