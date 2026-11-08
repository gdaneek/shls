#!/usr/bin/env bash
# Usage: infrastructure/firewall/firewall.sh apply|rollback|status
#   apply    load mini-checkers.nft (replaces the table atomically)
#   rollback delete the table; traffic inside the segments is no longer filtered
#   status   print the rules with packet counters
# Needs root for nft: runs `sudo nft` when started by a regular user.
set -euo pipefail

cd "$(dirname "$0")"

table="bridge mini_checkers"
nft=(nft)
((EUID == 0)) || nft=(sudo nft)

has_table() { "${nft[@]}" list table $table >/dev/null 2>&1; }

case "${1:-}" in
  apply)
    for bridge in mc-data mc-msg; do
      ip link show "$bridge" >/dev/null 2>&1 ||
        echo "note: bridge $bridge does not exist yet; rules apply once the stack is up"
    done
    "${nft[@]}" -f mini-checkers.nft
    echo "firewall applied: table $table"
    ;;
  rollback)
    if has_table; then
      "${nft[@]}" delete table $table
      echo "firewall removed: table $table"
    else
      echo "firewall is not applied"
    fi
    ;;
  status)
    if has_table; then
      "${nft[@]}" list table $table
    else
      echo "firewall is not applied"
      exit 1
    fi
    ;;
  *)
    echo "usage: $0 apply|rollback|status" >&2
    exit 2
    ;;
esac
