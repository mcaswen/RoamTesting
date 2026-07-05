#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." >/dev/null 2>&1 && pwd)"

run_parallel_roam_preset() {
    local preset="$1"
    shift

    local executable="${PROJECT_ROOT}/build/${preset}/bin/ParallelROAM"

    cd "${PROJECT_ROOT}"

    echo "[ParallelROAM] configure: ${preset}"
    cmake --preset "${preset}"

    echo "[ParallelROAM] build: ${preset}"
    cmake --build --preset "${preset}" --parallel

    if [[ ! -x "${executable}" ]]; then
        echo "[ParallelROAM] executable not found: ${executable}" >&2
        exit 1
    fi

    echo "[ParallelROAM] run: ${executable} $*"
    "${executable}" "$@"
}
