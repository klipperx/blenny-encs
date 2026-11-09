#!/bin/bash
# Mac launcher — plug badges; the pit viper does the rest.
set -euo pipefail
cd "$(dirname "$0")/.."

export PATH="${HOME}/.platformio/penv/bin:${PATH}"
export PYTHONUNBUFFERED=1

if [[ ! -x .venv/bin/python3 ]]; then
  echo "Missing .venv — run: python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt"
  read -r -p "Press Enter to close..."
  exit 1
fi

clear
case "${1:-}" in
  --update) echo "Starting Viper Update — Let'ssssss Update your Firmware…" ;;
  --sync) echo "Starting Viper Sync — firmware + roster (friends kept)…" ;;
  --roster) echo "Starting Viper Roster — feeding the nesssst a new name…" ;;
  --identify) echo "Starting Viper Identify — sniff sniff… who are you?" ;;
  *) echo "Starting Viper Check-In…" ;;
esac
echo
exec .venv/bin/python3 tools/flash_attendees.py "$@"
