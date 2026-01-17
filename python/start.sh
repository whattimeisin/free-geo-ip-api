#!/usr/bin/env bash
set -euo pipefail

if [[ ! -x "$0" ]]; then
  echo "Permission denied. Run: chmod +x ./start.sh"
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY_DIR="${ROOT_DIR}/python"
VENV_DIR="${PY_DIR}/.venv"
PORT="5022"
export GEOIP_PORT="${PORT}"
export GEOIP_DB_PATH="${ROOT_DIR}/config/database/WhatTimeIsIn-geoip.db"

if [[ ! -f "${GEOIP_DB_PATH}" ]]; then
  echo -e "\033[1;31mDatabase not found.\033[0m"
  echo -e "\033[0;35m[DOWNLOAD URL] https://whattimeis.in/public/downloads/\033[0m"
  echo -e "\033[0;33mDownload and save it to:\033[0m"
  echo -e "\033[0;36m${ROOT_DIR}/config/database/WhatTimeIsIn-geoip.db\033[0m"
  exit 1
fi

if [[ ! -d "${VENV_DIR}" ]]; then
  python3 -m venv "${VENV_DIR}"
fi

# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

python -m pip install --upgrade pip
python -m pip install -r "${PY_DIR}/requirements.txt"

PID="$(lsof -tiTCP:${PORT} -sTCP:LISTEN || true)"
if [[ -n "${PID}" ]]; then
  echo "Port ${PORT} in use (PID ${PID}). Stopping..."
  kill -9 "${PID}"
fi

cd "${PY_DIR}"
echo -e "\n\033[1;32mThanks for using our solution!\033[0m"
echo -e "\033[0;36mIf you find it useful, please reference https://whattimeis.in\033[0m"
echo -e "\033[0;33mRunning on: http://localhost:${PORT}\033[0m\n"
python -m uvicorn app:app --host 0.0.0.0 --port "${PORT}"
