#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="${ROOT_DIR}/docs/naturaldocs"
OUTPUT_DIR="${ROOT_DIR}/docs/site"

ND_CMD=()
if command -v NaturalDocs >/dev/null 2>&1; then
    ND_CMD=("NaturalDocs")
elif [[ -f "${ROOT_DIR}/NaturalDocs/NaturalDocs.exe" ]]; then
    if command -v mono >/dev/null 2>&1; then
        ND_CMD=("mono" "${ROOT_DIR}/NaturalDocs/NaturalDocs.exe")
    else
        echo "[ERROR] Found ${ROOT_DIR}/NaturalDocs/NaturalDocs.exe but mono is not installed."
        echo "Install mono or add NaturalDocs to PATH."
        exit 1
    fi
else
    echo "[ERROR] NaturalDocs not found in PATH and no local ./NaturalDocs/NaturalDocs.exe."
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

"${ND_CMD[@]}" \
    -i "${ROOT_DIR}/src" \
    -o HTML "${OUTPUT_DIR}" \
    -p "${PROJECT_DIR}"

echo "[OK] Documentation generated at ${OUTPUT_DIR}"
