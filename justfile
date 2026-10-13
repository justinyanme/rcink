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

native-papercolor:
    {{ pio-run }} papercolor test -d firmware/papercolor -e native

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

test: producer-test native-papercolor native-t-amoled native-c3-x4 native-rlcd native-epd397 tlsr-host-test

build-papercolor:
    {{ pio-run }} papercolor run -d firmware/papercolor

flash-papercolor port out: build-papercolor
    uv run scripts/papercolor_flash.py --port '{{ port }}' --out '{{ out }}'

observe-papercolor port out:
    uv run scripts/papercolor_observe.py --port '{{ port }}' --out '{{ out }}' --commands pdrp --seconds 50

trial-papercolor port out modes='N':
    uv run scripts/papercolor_trial.py --port '{{ port }}' --out '{{ out }}' --modes '{{ modes }}'

verify-papercolor port out: native-papercolor build-papercolor
    uv run scripts/papercolor_flash.py --port '{{ port }}' --out '{{ out }}/flash'
    uv run scripts/papercolor_observe.py --port '{{ port }}' --out '{{ out }}/observation' --commands pdrp --seconds 50

build-t-amoled:
    {{ pio-run }} t-amoled run -d firmware/t-amoled

build-c3-x4:
    {{ pio-run }} c3-x4 run -d firmware/c3-x4

build-rlcd:
    {{ pio-run }} rlcd run -d firmware/rlcd

build-epd397:
    {{ pio-run }} epd397 run -d firmware/epd397

build: build-papercolor build-t-amoled build-c3-x4 build-rlcd build-epd397

goldens:
    cd producer && uv run rcink-goldens regen
