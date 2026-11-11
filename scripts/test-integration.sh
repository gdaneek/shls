#!/usr/bin/env bash
# Compatibility entry point: like the original, this stops Redis and the worker.
set -euo pipefail
cd "$(dirname "$0")/.."
export CHECKERS_DISRUPTIVE=1
exec python3 -B -m unittest discover -s scripts/integration -p 'test_flows.py' -v
