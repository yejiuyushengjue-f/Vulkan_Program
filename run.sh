#!/usr/bin/env bash

set -euo pipefail

export PATH="/c/Program Files/CMake/bin:/ucrt64/bin:/usr/bin:${PATH}"

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${project_dir}/build"
cmake_exe="/c/Program Files/CMake/bin/cmake.exe"

"${cmake_exe}" \
    -S "${project_dir}" \
    -B "${build_dir}" \
    -G Ninja \
    -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe \
    -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
    -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe

"${cmake_exe}" --build "${build_dir}"
"${build_dir}/vulkan.exe"
