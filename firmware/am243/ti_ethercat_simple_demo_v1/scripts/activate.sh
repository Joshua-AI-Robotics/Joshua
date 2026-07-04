#!/usr/bin/env bash
# shellcheck disable=SC1090

BASE="${HOME}/myWorkspace"
VENV_DIR="${BASE}/venvs/ti"

if [[ ! -d "${VENV_DIR}" ]]; then
    echo "Creating Python virtual environment at ${VENV_DIR}"
    python3 -m venv "${VENV_DIR}"
fi

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "Please source this file instead of executing it:"
    echo "  source ${BASH_SOURCE[0]}"
    exit 1
fi

# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
