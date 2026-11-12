#!/usr/bin/env bash
# Compatibility entry point; FIREWALL=off checks the policy without nft rules.
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 -B -m unittest discover -s scripts/integration -p 'test_network.py' -v
