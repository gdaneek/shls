#!/usr/bin/env bash
# Compatibility entry point; checks live in scripts/integration/test_startup.py.
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 -B -m unittest discover -s scripts/integration -p 'test_startup.py' -v
