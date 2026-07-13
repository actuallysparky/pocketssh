#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
esp_root="$(cd "${repo_root}/../../.." && pwd)"
build_dir="${POCKETSSH_BUILD_DIR:-${esp_root}/_local/build/pocketssh/tdeckplus}"
package_dir="${POCKETSSH_PACKAGE_DIR:-${esp_root}/_local/packages/pocketssh}"
src_bin="${build_dir}/PocketSSH.bin"
dst_bin="${package_dir}/PocketSSH-TDeckPlus.bin"

if [[ ! -f "${src_bin}" ]]; then
  echo "Missing source binary: ${src_bin}" >&2
  echo "Run idf.py -B ${build_dir} build first, or set POCKETSSH_BUILD_DIR." >&2
  exit 2
fi

mkdir -p "${package_dir}"
cp "${src_bin}" "${dst_bin}"
echo "Packaged binary: ${dst_bin}"
