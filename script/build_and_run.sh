#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
arch="$(uname -m)"
mode=run
model=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --universal) arch="arm64;x86_64" ;;
        --build-only) mode=build ;;
        --verify) mode=verify ;;
        --help|-h)
            echo "Usage: $0 [--universal] [--build-only | --verify] [model-file]"
            exit 0 ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 2 ;;
        *)
            if [[ ${#model[@]} -ne 0 ]]; then
                echo "Specify one model file." >&2
                exit 2
            fi
            model=("$1") ;;
    esac
    shift
done

if [[ "$(uname -s)" != Darwin ]] || ! xcode-select -p >/dev/null 2>&1; then
    echo "This script requires macOS and the Xcode Command Line Tools (xcode-select --install)." >&2
    exit 1
fi
cmake_bin="${CMAKE_BIN:-cmake}"
if ! command -v "$cmake_bin" >/dev/null 2>&1; then
    echo "Install CMake 3.24 or later and make it available on PATH, or set CMAKE_BIN." >&2
    exit 1
fi

sdk="$(xcrun --sdk macosx --show-sdk-path)"
sdk_options=("-DCMAKE_OSX_SYSROOT=$sdk")
# Some Command Line Tools installations keep the full libc++ headers in the SDK.
if ! printf '#include <cmath>\n' | xcrun clang++ -isysroot "$sdk" -x c++ -fsyntax-only - 2>/dev/null &&
   [[ -f "$sdk/usr/include/c++/v1/cmath" ]]; then
    sdk_options+=("-DCMAKE_CXX_FLAGS=${CXXFLAGS:-} -nostdinc++ -isystem \"$sdk/usr/include/c++/v1\"")
fi

build="$root/build/macos-${arch/arm64;x86_64/universal}"
app="$root/dist/ModelViewer.app"
"$cmake_bin" -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_OSX_ARCHITECTURES=$arch" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "${sdk_options[@]}"
"$cmake_bin" --build "$build" --parallel "${BUILD_JOBS:-$(sysctl -n hw.logicalcpu)}"
if [[ "$mode" != build ]]; then
    pkill -x -f "$app/Contents/MacOS/ModelViewer" 2>/dev/null || true
fi
"$cmake_bin" --install "$build" --prefix "$root/dist"
codesign --force --sign - "$app"
echo "Built $app"

if [[ "$mode" != build ]]; then
    if [[ ${#model[@]} -eq 1 ]]; then
        open -n -a "$app" "${model[0]}"
    else
        open -n "$app"
    fi
fi
if [[ "$mode" == verify ]]; then
    sleep 1
    pgrep -x -f "$app/Contents/MacOS/ModelViewer" >/dev/null
fi
