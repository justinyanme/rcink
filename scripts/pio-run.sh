#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <target> <pio-args...>" >&2
  exit 2
fi

target="$1"
shift

pio_bin="${PIO_BIN:-pio}"

default_official_core="${HOME}/.platformio-rcink-official"
default_c3_core="${HOME}/.platformio-rcink-c3"

case "${target}" in
  c3-x4 | firmware/c3-x4)
    core_dir="${RCINK_PIO_C3_CORE_DIR:-${PIO_C3_CORE_DIR:-${default_c3_core}}}"
    ;;
  *)
    core_dir="${RCINK_PIO_OFFICIAL_CORE_DIR:-${PIO_OFFICIAL_CORE_DIR:-${default_official_core}}}"
    ;;
esac

export PLATFORMIO_CORE_DIR="${core_dir}"
exec "${pio_bin}" "$@"
