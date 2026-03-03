#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ANIMVM_DIR="${ROOT_DIR}/src/procRhai/animvm"
BUILD_DIR="${1:-${ROOT_DIR}/build}"

if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "[ERROR] Build directory not found: ${BUILD_DIR}"
    echo "Usage: $0 [build_dir]"
    exit 1
fi

if ! command -v cargo >/dev/null 2>&1; then
    echo "[ERROR] cargo not found in PATH."
    exit 1
fi

echo "[INFO] Building animvm (Rust cdylib)..."
cargo build --release --manifest-path "${ANIMVM_DIR}/Cargo.toml"

LIB_NAME="libanimvm.so"
if [[ "$(uname -s)" == "Darwin" ]]; then
    LIB_NAME="libanimvm.dylib"
fi

SRC_LIB="${ANIMVM_DIR}/target/release/${LIB_NAME}"
if [[ ! -f "${SRC_LIB}" ]]; then
    echo "[ERROR] Built library not found: ${SRC_LIB}"
    exit 1
fi

cp -f "${SRC_LIB}" "${BUILD_DIR}/${LIB_NAME}"
echo "[OK] Copied ${LIB_NAME} to ${BUILD_DIR}"
