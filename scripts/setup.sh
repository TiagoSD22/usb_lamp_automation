#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="$ROOT/include/config.h"
EXAMPLE="$ROOT/include/config_example.h"

if [[ -f "$CONFIG" ]]; then
  echo "include/config.h already exists — leaving it untouched."
else
  cp "$EXAMPLE" "$CONFIG"
  echo "Created include/config.h from template — fill in your credentials."
fi

echo "Checking tools:"
if command -v pio >/dev/null 2>&1; then
  echo "  ok:      pio ($(pio --version))"
else
  echo "  MISSING: pio — install the PlatformIO IDE extension in VS Code,"
  echo "           or the CLI with: pip install --user platformio"
fi

if id -nG "$USER" | grep -qw dialout; then
  echo "  ok:      user in 'dialout' group"
else
  echo "  MISSING: serial access — run 'sudo usermod -aG dialout $USER' then 'wsl --shutdown'"
fi
