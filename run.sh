#!/usr/bin/env bash

set -euo pipefail

export PATH="/c/Program Files/CMake/bin:/ucrt64/bin:/usr/bin:${PATH}"

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${project_dir}/build"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
    cmake -S "${project_dir}" -B "${build_dir}" -G Ninja
fi

cmake --build "${build_dir}"
"${build_dir}/vulkan.exe"
