#!/bin/sh
# Пять перезапусков только Game API; аргумент — число повторений.
set -eu
cd "$(dirname "$0")/../.."
exec python3 -B tests/integration/measure_service.py mttr --repeats "${1:-5}"
