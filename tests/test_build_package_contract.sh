#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
test_root="$(mktemp -d)"
trap 'rm -rf "${test_root}"' EXIT

project_root="${test_root}/PocketSSH"
fake_idf="${test_root}/esp-idf"
fake_bin="${test_root}/bin"
mkdir -p "${project_root}" "${fake_idf}" "${fake_bin}"
cp "${repo_root}/build.sh" "${repo_root}/package.sh" "${project_root}/"

cat >"${fake_idf}/export.sh" <<'EOF'
#!/usr/bin/env bash
export PATH="${TEST_FAKE_BIN}:${PATH}"
EOF

cat >"${fake_bin}/idf.py" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >>"${TEST_IDF_LOG}"
while [[ "$#" -gt 0 ]]; do
  if [[ "$1" == "-B" ]]; then
    mkdir -p "$2"
    : >"$2/PocketSSH.bin"
    exit 0
  fi
  shift
done
exit 1
EOF
chmod +x "${fake_idf}/export.sh" "${fake_bin}/idf.py"

assert_fails_with_usage() {
  local script="$1"
  local output="${test_root}/$(basename "${script}").out"
  if "${script}" invalid >"${output}" 2>&1; then
    echo "expected invalid target to fail: ${script}" >&2
    exit 1
  fi
  grep -Fq 'Usage:' "${output}"
}

assert_fails_with_usage "${project_root}/build.sh"
assert_fails_with_usage "${project_root}/package.sh"

export IDF_PATH="${fake_idf}"
export TEST_FAKE_BIN="${fake_bin}"
export TEST_IDF_LOG="${test_root}/idf.log"
export POCKETSSH_BUILD_DIR="${test_root}/build/tpager"
export POCKETSSH_PACKAGE_DIR="${test_root}/packages/tpager"
"${project_root}/build.sh" tpager
grep -Fxq -- "-B ${POCKETSSH_BUILD_DIR} -DTPAGER_TARGET=ON -DTPAGER_DIAG=OFF build" "${TEST_IDF_LOG}"
"${project_root}/package.sh" tpager >/dev/null
test -f "${POCKETSSH_PACKAGE_DIR}/PocketSSH-TPager.bin"

export POCKETSSH_BUILD_DIR="${test_root}/build/tdeckplus"
export POCKETSSH_PACKAGE_DIR="${test_root}/packages/tdeckplus"
"${project_root}/build.sh" tdeckplus
grep -Fxq -- "-B ${POCKETSSH_BUILD_DIR} build" "${TEST_IDF_LOG}"
"${project_root}/package.sh" tdeckplus >/dev/null
test -f "${POCKETSSH_PACKAGE_DIR}/PocketSSH-2.0.bin"

echo "build/package wrapper contract passed"
