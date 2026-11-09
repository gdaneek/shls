#!/usr/bin/env bash
set -euo pipefail
printf "\ninclude_if_exists = '/etc/postgresql/replication.conf'\n" >> "$PGDATA/postgresql.conf"
