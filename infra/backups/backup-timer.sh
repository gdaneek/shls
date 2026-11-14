#!/usr/bin/env bash
# User systemd timer: no sudo or Docker socket mounted in a container.
set -euo pipefail
cd "$(dirname "$0")/../.."
directory="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
unit=mini-checkers-backup
case "${1:-}" in
  install)
    mkdir -p "$directory"
    for suffix in service timer; do
      file="$directory/$unit.$suffix"
      if [[ -e "$file" ]] && ! grep -q 'Description=Mini Checkers daily PostgreSQL' "$file"; then
        echo "Refusing to replace unrelated unit $file" >&2
        exit 1
      fi
    done
    # Escape systemd quoted values and specifiers (paths may contain spaces).
    root="${PWD//\\/\\\\}"
    root="${root//\"/\\\"}"
    root="${root//%/%%}"
    root="${root//\$/\$\$}"
    template="$(cat infra/backups/$unit.service.in)"
    printf '%s\n' "${template//@ROOT@/$root}" >"$directory/$unit.service"
    cp "infra/backups/$unit.timer" "$directory/$unit.timer"
    systemctl --user daemon-reload
    systemctl --user enable --now "$unit.timer"
    ;;
  remove)
    systemctl --user disable --now "$unit.timer"
    systemctl --user stop "$unit.service"
    rm -f "$directory/$unit.service" "$directory/$unit.timer"
    systemctl --user daemon-reload
    ;;
  status)
    systemctl --user list-timers --all "$unit.timer"
    systemctl --user show "$unit.service" -p Result -p ExecMainStatus
    ;;
  *) echo "usage: $0 install|remove|status" >&2; exit 2 ;;
esac
