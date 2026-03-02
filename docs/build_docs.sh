#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="${ROOT_DIR}/docs/naturaldocs"
OUTPUT_DIR="${ROOT_DIR}/docs/site"

if ! command -v NaturalDocs >/dev/null 2>&1; then
    echo "[ERROR] NaturalDocs not found in PATH."
    echo "Install Natural Docs and re-run this script."
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

NaturalDocs \
    -i "${ROOT_DIR}/src" \
    -o HTML "${OUTPUT_DIR}" \
    -p "${PROJECT_DIR}"

echo "[OK] Documentation generated at ${OUTPUT_DIR}"
