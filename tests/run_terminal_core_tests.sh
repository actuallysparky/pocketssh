#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
work_dir="${POCKETSSH_TEST_BUILD_DIR:-${repo_root}/_local/tests}"

mkdir -p "${work_dir}"
c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"${repo_root}/main/include" \
  "${repo_root}/main/terminal_core.cpp" \
  "${script_dir}/test_terminal_core.cpp" \
  -o "${work_dir}/terminal_core_tests"
"${work_dir}/terminal_core_tests"
"${script_dir}/test_build_package_contract.sh"
