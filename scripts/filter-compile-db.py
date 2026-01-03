#!/usr/bin/env python3
"""
Filter compile_commands.json to remove problematic flags for clang-tidy
"""

import json
import re
import sys


def should_remove_arg(arg):
    """Check if an argument should be removed"""
    return arg.startswith('-fconcepts-diagnostics-depth')


def filter_compile_commands(input_file, output_file):
    """Remove problematic flags from compile commands"""

    with open(input_file, 'r') as f:
        data = json.load(f)

    for entry in data:
        # Handle 'command' field (single string)
        if 'command' in entry:
            cmd = entry['command']
            cmd = re.sub(r'-fconcepts-diagnostics-depth=\d+', '', cmd)
            cmd = re.sub(r'\s+', ' ', cmd).strip()
            entry['command'] = cmd

        # Handle 'arguments' field (list of strings)
        if 'arguments' in entry:
            entry['arguments'] = [
                arg for arg in entry['arguments']
                if not should_remove_arg(arg)
            ]

    with open(output_file, 'w') as f:
        json.dump(data, f, indent=2)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: filter-compile-db.py <input.json> <output.json>")
        sys.exit(1)

    filter_compile_commands(sys.argv[1], sys.argv[2])
