#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target="${1:-}"

if [[ "${target}" != "tpager" && "${target}" != "tdeckplus" ]]; then
  echo "Usage: $0 <tpager|tdeckplus>" >&2
  exit 2
fi

engineering_root="$(cd "${script_dir}/../.." && pwd)"
export IDF_PATH="${IDF_PATH:-${engineering_root}/toolchains/esp-idf}"
if [[ ! -f "${IDF_PATH}/export.sh" ]]; then
  echo "ESP-IDF export script is missing: ${IDF_PATH}/export.sh" >&2
  exit 2
fi

# shellcheck source=/dev/null
source "${IDF_PATH}/export.sh" >/dev/null

build_dir="${POCKETSSH_BUILD_DIR:-${script_dir}/_local/build/${target}}"
case "${target}" in
  tpager)
    idf.py -B "${build_dir}" -DTPAGER_TARGET=ON -DTPAGER_DIAG=OFF build
    ;;
  tdeckplus)
    idf.py -B "${build_dir}" build
    ;;
esac
