#!/usr/bin/env python3
"""
Convert existing code documentation to Doxygen format.

This script processes C++ header files and converts various comment styles
to proper Doxygen format:
- Converts /* */ to /** */
- Adds @brief for first paragraph
- Converts common patterns to @param, @return, @tparam, etc.
- Preserves existing Doxygen comments

Usage:
    python scripts/convert_to_doxygen.py [--dry-run] [files...]
    
If no files specified, processes all .hpp files in include/
"""

import re
import sys
import argparse
from pathlib import Path


def ensure_brief_and_convert_tags(comment: str) -> str:
    """Ensure @brief exists in an already-Doxygen formatted comment."""
    lines = comment.split('\n')
    result_lines = []
    has_brief = '@brief' in comment or '\\brief' in comment
    added_brief = has_brief
    
    for i, line in enumerate(lines):
        stripped = line.strip()
        
        # Opening line /** or /** content
        if i == 0:
            if stripped == '/**':
                result_lines.append(line)
                continue
            # Has content on same line as /**
            rest = stripped[3:].strip() if len(stripped) > 3 else ''
            if rest and not rest.startswith('@') and not rest.startswith('\\') and not added_brief:
                new_line = line.replace('/** ', '/** @brief ', 1)
                result_lines.append(new_line)
                added_brief = True
                continue
            result_lines.append(line)
            continue
        
        # Content line starting with *
        if stripped.startswith('*') and stripped != '*/':
            content = stripped[1:].strip() if len(stripped) > 1 else ''
            
            # Add @brief to first content line if missing
            if content and not added_brief:
                if not content.startswith('@') and not content.startswith('\\'):
                    # Preserve original line structure, just insert @brief after "* "
                    new_line = line.replace('* ' + content, '* @brief ' + content, 1)
                    result_lines.append(new_line)
                    added_brief = True
                    continue
        
        # Convert inline tags
        converted = convert_inline_tags(line)
        result_lines.append(converted)
    
    return '\n'.join(result_lines)


def convert_comment_to_doxygen(comment: str) -> str:
    """Convert a multi-line comment to Doxygen format."""
    lines = comment.split('\n')
    if not lines:
        return comment
    
    # Check if already Doxygen format
    first_line = lines[0].strip()
    if first_line.startswith('/**') or first_line.startswith('///'):
        # Already Doxygen, but may need @brief added and tag conversion
        return ensure_brief_and_convert_tags(comment)
    
    # Convert /* to /**
    result_lines = []
    added_brief = False
    
    for i, line in enumerate(lines):
        stripped = line.strip()
        
        # Convert opening line
        if i == 0 and stripped.startswith('/*'):
            # Check if there's content on the opening line
            rest = stripped[2:].strip()
            if rest and not rest.startswith('*'):
                # Content on same line as /*, add @brief
                new_line = line.replace('/*', '/**', 1)
                # Insert @brief after /**
                new_line = new_line.replace('/** ', '/** @brief ', 1)
                result_lines.append(new_line)
                added_brief = True
            else:
                result_lines.append(line.replace('/*', '/**', 1))
            continue
        
        # Handle content lines (starting with *)
        if stripped.startswith('*'):
            content = stripped[1:].strip() if len(stripped) > 1 else ''
            
            # Check for closing */
            if stripped == '*/':
                result_lines.append(line)
                continue
            
            # Add @brief to first content line if not already tagged
            if content and not added_brief:
                if not content.startswith('@') and not content.startswith('\\'):
                    # Preserve original line structure, just insert @brief after "* "
                    new_line = line.replace('* ' + content, '* @brief ' + content, 1)
                    result_lines.append(new_line)
                    added_brief = True
                    continue
            
            # Convert common patterns
            converted = convert_inline_tags(content)
            if converted != content:
                indent = line[:len(line) - len(line.lstrip())]
                result_lines.append(f"{indent} * {converted}")
                continue
        
        result_lines.append(line)
    
    return '\n'.join(result_lines)


def convert_tags(comment: str) -> str:
    """Convert common documentation patterns to Doxygen tags."""
    lines = comment.split('\n')
    result_lines = []
    
    for line in lines:
        result_lines.append(convert_inline_tags(line))
    
    return '\n'.join(result_lines)


def convert_inline_tags(line: str) -> str:
    """Convert inline documentation patterns to Doxygen tags."""
    # Already has Doxygen tags
    if '@' in line or '\\param' in line or '\\return' in line:
        return line
    
    # Pattern: "Returns: description" or "Return: description"
    return_pattern = r'^\s*\*?\s*[Rr]eturns?\s*[:.]?\s+(.+)$'
    match = re.match(return_pattern, line)
    if match:
        return line.replace(match.group(0).strip(), f'@return {match.group(1)}')
    
    # Pattern: "Note: description"
    note_pattern = r'^\s*\*?\s*[Nn]ote\s*[:.]?\s+(.+)$'
    match = re.match(note_pattern, line)
    if match:
        return line.replace(match.group(0).strip(), f'@note {match.group(1)}')
    
    # Pattern: "Warning: description"
    warning_pattern = r'^\s*\*?\s*[Ww]arning\s*[:.]?\s+(.+)$'
    match = re.match(warning_pattern, line)
    if match:
        return line.replace(match.group(0).strip(), f'@warning {match.group(1)}')
    
    # Pattern: "Example: ..." or "Example:"
    example_pattern = r'^\s*\*?\s*[Ee]xample\s*[:.]?\s*(.*)$'
    match = re.match(example_pattern, line)
    if match:
        content = match.group(1) if match.group(1) else ''
        return line.replace(match.group(0).strip(), f'@code{{{content}}}' if content else '@code')
    
    # Pattern: "See: description" or "See also: description"
    see_pattern = r'^\s*\*?\s*[Ss]ee\s*(?:also)?\s*[:.]?\s+(.+)$'
    match = re.match(see_pattern, line)
    if match:
        return line.replace(match.group(0).strip(), f'@see {match.group(1)}')
    
    return line


def process_file(filepath: Path, dry_run: bool = False) -> tuple[bool, int]:
    """
    Process a single file, converting comments to Doxygen format.
    
    Returns:
        Tuple of (was_modified, num_changes)
    """
    content = filepath.read_text()
    original = content
    
    # Find all multi-line comments that span multiple lines (not inline)
    # Pattern matches /* ... */ or /** ... */ but only if it contains newlines
    # This avoids converting inline comments like /*param*/ or /*unused*/
    comment_pattern = r'/\*\*?(?:[^*]|\*(?!/))*\n(?:[^*]|\*(?!/))*\*/'
    
    def replace_comment(match):
        comment = match.group(0)
        return convert_comment_to_doxygen(comment)
    
    new_content = re.sub(comment_pattern, replace_comment, content)
    
    # Count changes
    num_changes = sum(1 for a, b in zip(content.split('/**'), new_content.split('/**')) if a != b)
    
    was_modified = new_content != original
    
    if was_modified and not dry_run:
        filepath.write_text(new_content)
    
    return was_modified, num_changes


def main():
    parser = argparse.ArgumentParser(
        description='Convert code documentation to Doxygen format'
    )
    parser.add_argument(
        '--dry-run', '-n',
        action='store_true',
        help='Show what would be changed without modifying files'
    )
    parser.add_argument(
        'files',
        nargs='*',
        help='Files to process (default: all .hpp in include/)'
    )
    
    args = parser.parse_args()
    
    # Find project root (where this script is in scripts/)
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    
    if args.files:
        files = [Path(f) for f in args.files]
    else:
        # Default: all .hpp and .cpp files in include/, example/, test/
        files = []
        for directory in ['include', 'example', 'test']:
            dir_path = project_root / directory
            if dir_path.exists():
                files.extend(dir_path.rglob('*.hpp'))
                files.extend(dir_path.rglob('*.cpp'))
    
    if not files:
        print("No files to process")
        return 0
    
    total_modified = 0
    total_changes = 0
    
    for filepath in files:
        if not filepath.exists():
            print(f"Warning: {filepath} does not exist, skipping")
            continue
        
        was_modified, num_changes = process_file(filepath, args.dry_run)
        
        if was_modified:
            total_modified += 1
            total_changes += num_changes
            status = "[DRY RUN] Would modify" if args.dry_run else "Modified"
            print(f"{status}: {filepath.relative_to(project_root)}")
    
    print(f"\nSummary: {total_modified} files {'would be ' if args.dry_run else ''}modified")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
