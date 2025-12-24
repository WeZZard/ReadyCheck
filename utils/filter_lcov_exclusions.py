#!/usr/bin/env python3
"""
Filter LCOV_EXCL_LINE markers from merged.lcov file.

This script reads source files and removes coverage data for lines marked with:
- // LCOV_EXCL_LINE
- // LCOV_EXCL_START ... // LCOV_EXCL_STOP
"""

import re
import sys
from pathlib import Path


def find_excluded_lines(source_file):
    """Find all lines that should be excluded from coverage."""
    excluded = set()
    in_excl_block = False

    try:
        with open(source_file, 'r') as f:
            for line_num, line in enumerate(f, 1):
                # Check for LCOV_EXCL_LINE
                if 'LCOV_EXCL_LINE' in line:
                    excluded.add(line_num)

                # Check for LCOV_EXCL_START/STOP
                if 'LCOV_EXCL_START' in line:
                    in_excl_block = True
                    excluded.add(line_num)
                elif 'LCOV_EXCL_STOP' in line:
                    excluded.add(line_num)
                    in_excl_block = False
                elif in_excl_block:
                    excluded.add(line_num)
    except FileNotFoundError:
        pass

    return excluded


def filter_lcov(input_path, output_path):
    """Filter LCOV file to remove excluded lines."""

    with open(input_path, 'r') as f:
        lines = f.readlines()

    current_file = None
    excluded_lines = set()
    filtered_lines = []

    for line in lines:
        # Track current source file
        if line.startswith('SF:'):
            current_file = line[3:].strip()
            excluded_lines = find_excluded_lines(current_file)
            filtered_lines.append(line)

        # Filter DA (line coverage) entries
        elif line.startswith('DA:'):
            match = re.match(r'DA:(\d+),', line)
            if match:
                line_num = int(match.group(1))
                if line_num not in excluded_lines:
                    filtered_lines.append(line)
            else:
                filtered_lines.append(line)

        # Filter BRDA (branch coverage) entries
        elif line.startswith('BRDA:'):
            match = re.match(r'BRDA:(\d+),', line)
            if match:
                line_num = int(match.group(1))
                if line_num not in excluded_lines:
                    filtered_lines.append(line)
            else:
                filtered_lines.append(line)

        # Keep all other lines
        else:
            filtered_lines.append(line)

    with open(output_path, 'w') as f:
        f.writelines(filtered_lines)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: filter_lcov_exclusions.py <input.lcov> <output.lcov>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    filter_lcov(input_file, output_file)
    print(f"Filtered {input_file} -> {output_file}")
