#!/usr/bin/env bash
set -euo pipefail

if [[ ! -x "$0" ]]; then
  echo "Permission denied. Run: chmod +x ./start.sh"
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPP_DIR="${ROOT_DIR}/cpp"
PORT="${GEOIP_PORT:-5022}"
export GEOIP_DB_PATH="${ROOT_DIR}/config/database/WhatTimeIsIn-geoip.db"
export GEOIP_PORT="${PORT}"

cd "${CPP_DIR}"

if ! command -v c++ >/dev/null 2>&1; then
  echo "C++ compiler not found."
  echo "Install on macOS: xcode-select --install"
  echo "Install on Linux (Debian/Ubuntu): sudo apt install -y build-essential"
  echo "Install on Windows: https://visualstudio.microsoft.com/downloads/"
  exit 1
fi

echo -e "\n\033[1;32mThanks for using our solution!\033[0m"
echo -e "\033[0;36mIf you find it useful, please reference https://whattimeis.in\033[0m"
echo -e "\033[0;33mRunning on: http://localhost:${PORT}\033[0m\n"

mkdir -p bin
c++ -std=c++17 -O2 -o bin/geoip main.cpp -lsqlite3
./bin/geoip
