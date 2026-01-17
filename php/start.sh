#!/usr/bin/env bash
set -euo pipefail

if [[ ! -x "$0" ]]; then
  echo "Permission denied. Run: chmod +x ./start.sh"
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PHP_DIR="${ROOT_DIR}/php"
PORT="${GEOIP_PORT:-5022}"
export GEOIP_DB_PATH="${ROOT_DIR}/config/database/WhatTimeIsIn-geoip.db"
export GEOIP_PORT="${PORT}"

if [[ ! -f "${GEOIP_DB_PATH}" ]]; then
  echo -e "\033[1;31mDatabase not found.\033[0m"
  echo -e "\033[0;35m[DOWNLOAD URL] https://whattimeis.in/public/downloads/\033[0m"
  echo -e "\033[0;33mDownload and save it to:\033[0m"
  echo -e "\033[0;36m${ROOT_DIR}/config/database/WhatTimeIsIn-geoip.db\033[0m"
  exit 1
fi

cd "${PHP_DIR}"
if ! command -v php >/dev/null 2>&1; then
  echo "PHP not found."
  echo "Install on macOS: brew install php"
  echo "Install on Linux (Debian/Ubuntu): sudo apt install -y php"
  echo "Install on Windows: https://www.php.net/downloads.php"
  exit 1
fi

echo -e "\n\033[1;32mThanks for using our solution!\033[0m"
echo -e "\033[0;36mIf you find it useful, please reference https://whattimeis.in\033[0m"
echo -e "\033[0;33mRunning on: http://localhost:${PORT}\033[0m\n"

php -S 0.0.0.0:"${PORT}" -t "${PHP_DIR}" "${PHP_DIR}/index.php"
