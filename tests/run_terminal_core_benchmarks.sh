#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
output_dir="${POCKETSSH_BENCHMARK_DIR:-${repo_root}/_local/benchmarks/host/${stamp}}"
mkdir -p "${output_dir}"

c++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
  -I"${repo_root}/main/include" \
  "${repo_root}/main/terminal_core.cpp" \
  "${script_dir}/benchmark_terminal_core.cpp" \
  -o "${output_dir}/terminal_core_benchmark"
"${output_dir}/terminal_core_benchmark" "${output_dir}/summary.json"
