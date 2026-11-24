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

# When macOS CommandLineTools or an older Xcode is selected while a newer SDK
# is installed, host links fail on newer TBD stubs (e.g. arm64e.x1-macos).
# Probe once and select the newest working Xcode bundle if DEVELOPER_DIR is unset.
if [ "$(uname -s)" = "Darwin" ] && [ -z "${DEVELOPER_DIR:-}" ] && command -v clang++ >/dev/null 2>&1; then
  probe_bin="$(mktemp "${TMPDIR:-/tmp}/rcink-ld-probe.XXXXXX")"
  if ! printf 'int main(){return 0;}\n' | clang++ -x c++ - -o "${probe_bin}" >/dev/null 2>&1; then
    while IFS= read -r dev_dir; do
      [ -d "${dev_dir}/Platforms/MacOSX.platform/Developer/SDKs" ] || continue
      if printf 'int main(){return 0;}\n' | DEVELOPER_DIR="${dev_dir}" clang++ -x c++ - -o "${probe_bin}" >/dev/null 2>&1; then
        export DEVELOPER_DIR="${dev_dir}"
        break
      fi
    done < <(
      printf '%s\n' /Applications/Xcode-*.app/Contents/Developer | sort -rV
      printf '%s\n' /Applications/Xcode.app/Contents/Developer
    )
  fi
  rm -f "${probe_bin}"
fi

exec "${pio_bin}" "$@"
