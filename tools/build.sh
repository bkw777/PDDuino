#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Outputs stay outside the checkout. Override for separate builds/DSR wiring.
"${ARDUINO_CLI:-arduino-cli}" compile --fqbn arduino:avr:uno --jobs 2 \
  --build-path "${PDDUINO_BUILD_DIR:-${TMPDIR:-/tmp}/pdduino-openlog-build}" \
  "$@" PDDuino
