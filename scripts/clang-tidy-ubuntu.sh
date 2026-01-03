#!/bin/bash

# Wrapper for Ubuntu clang-tidy that filters out stdexec flags
# Usage: clang-tidy-ubuntu.sh [clang-tidy args...]

# Get the compile command from compile_commands.json and filter out problematic flags
filter_compile_command() {
    local file="$1"
    local compile_cmd=$(jq -r ".[] | select(.file == \"$file\") | .command" build/compile_commands.json)
    
    # Filter out the problematic flag
    echo "$compile_cmd" | sed 's/-fconcepts-diagnostics-depth=10//g'
}

# Filter arguments passed to clang-tidy
FILTERED_ARGS=()
for arg in "$@"; do
    case "$arg" in
        -p)
            # Keep -p but handle the next argument specially
            FILTERED_ARGS+=("$arg")
            shift
            if [[ $# -gt 0 ]]; then
                # If it's a compile database, we'll handle it specially
                if [[ "$1" == "build" ]]; then
                    FILTERED_ARGS+=("$1")
                else
                    FILTERED_ARGS+=("$1")
                fi
            fi
            ;;
        *)
            FILTERED_ARGS+=("$arg")
            ;;
    esac
    shift
done

# Run clang-tidy with filtered arguments
exec clang-tidy-18 "${FILTERED_ARGS[@]}"
