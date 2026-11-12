#!/usr/bin/env bash
# Compatibility entry point; the test uses a new, temporary database.
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 -B -m unittest discover -s scripts/integration -p 'test_schema.py' -v
