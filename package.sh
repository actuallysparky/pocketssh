#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target="${1:-}"

case "${target}" in
  tpager) artifact_name="PocketSSH-TPager.bin" ;;
  tdeckplus) artifact_name="PocketSSH-2.0.bin" ;;
  *)
    echo "Usage: $0 <tpager|tdeckplus>" >&2
    exit 2
    ;;
esac

build_dir="${POCKETSSH_BUILD_DIR:-${script_dir}/_local/build/${target}}"
package_dir="${POCKETSSH_PACKAGE_DIR:-${script_dir}/_local/packages/${target}}"
source_bin="${build_dir}/PocketSSH.bin"
destination_bin="${package_dir}/${artifact_name}"

if [[ ! -f "${source_bin}" ]]; then
  echo "Missing source binary: ${source_bin}" >&2
  echo "Run ./build.sh ${target} first, or set POCKETSSH_BUILD_DIR." >&2
  exit 2
fi

mkdir -p "${package_dir}"
cp "${source_bin}" "${destination_bin}"
shasum -a 256 "${destination_bin}"
echo "Packaged binary: ${destination_bin}"
