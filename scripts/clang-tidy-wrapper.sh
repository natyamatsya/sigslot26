#!/bin/bash

# Wrapper script for clang-tidy that handles platform differences
# and filters out incompatible compiler flags
# Usage: clang-tidy-wrapper.sh [clang-tidy args...]

# Check if we're on macOS and xcrun is available
if [[ "$OSTYPE" == "darwin"* ]] && command -v xcrun >/dev/null 2>&1; then
    # On macOS: Use system clang-tidy with SDK paths
    SDK_PATH=$(xcrun --show-sdk-path)
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    EXTRA_ARGS=(
        "-I$SCRIPT_DIR/../include"
        "-std=c++20"
        "-isystem$SDK_PATH/usr/include/c++/v1"
        "-isystem$SDK_PATH/usr/include"
    )
else
    # On Linux: Add include directory
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    EXTRA_ARGS=(
        "-I$SCRIPT_DIR/../include"
    )
fi

# Filter out arguments that clang-tidy doesn't understand
FILTERED_ARGS=()
for arg in "$@"; do
    case "$arg" in
        -fconcepts-diagnostics-depth=*)
            # Skip this stdexec flag
            ;;
        *)
            FILTERED_ARGS+=("$arg")
            ;;
    esac
done

# Pass filtered arguments to clang-tidy with extra args after --
exec clang-tidy "${FILTERED_ARGS[@]}" -- "${EXTRA_ARGS[@]}"
