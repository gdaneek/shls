#!/bin/sh
# Три минуты HTTP-наблюдения; аргумент — длительность в секундах.
set -eu
cd "$(dirname "$0")/../.."
exec python3 -B tests/integration/measure_service.py sli --seconds "${1:-180}"
