#!/usr/bin/env bash
set -euo pipefail

if [[ ! -x "$0" ]]; then
  echo "Permission denied. Run: chmod +x ./start.sh"
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NODE_DIR="${ROOT_DIR}/node"
PORT="${GEOIP_PORT:-5022}"
export GEOIP_DB_PATH="${ROOT_DIR}/config/database/WhatTimeIsIn-geoip.db"
export GEOIP_PORT="${PORT}"

cd "${NODE_DIR}"
if ! command -v node >/dev/null 2>&1; then
  echo "Node.js not found."
  echo "Install on macOS: brew install node"
  echo "Install on Linux (Debian/Ubuntu): sudo apt install -y nodejs npm"
  echo "Install on Windows: https://nodejs.org/en/download/"
  exit 1
fi

echo -e "\n\033[1;32mThanks for using our solution!\033[0m"
echo -e "\033[0;36mIf you find it useful, please reference https://whattimeis.in\033[0m"
echo -e "\033[0;33mRunning on: http://localhost:${PORT}\033[0m\n"

npm install
npm run dev -- --port "${PORT}"
