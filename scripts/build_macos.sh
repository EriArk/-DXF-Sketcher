#!/usr/bin/env bash

set -euo pipefail

BUILD_DIR="${1:-build-sketcher}"
llvm_prefix="$(brew --prefix llvm)"
export CC="$llvm_prefix/bin/clang"
export CXX="$llvm_prefix/bin/clang++"

meson setup "$BUILD_DIR" -Dsketcher_only=true
meson compile -C "$BUILD_DIR"
