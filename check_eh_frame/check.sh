#!/bin/bash

dir="$1" # Directory to scan

# Initialize counters
total=0
with_eh_frame=0
with_debuginfo=0

# Recursively find all files in the directory
while read -r file; do
    # Check if the file is an ELF binary
    if file "$file" | grep -q ELF; then
        ((total++))

        # Capture the output of objdump
        objdump_output=$(objdump -h "$file" 2>/dev/null)

        # Check if the binary has a .eh_frame section
        if echo "$objdump_output" | grep -q '\.eh_frame'; then
            ((with_eh_frame++))
        fi

        # Check if the binary has a .debug_frame or .debug_info section
        if echo "$objdump_output" | grep -Eq '\.debug_frame|\.debug_info'; then
            ((with_debuginfo++))
        fi

    fi
done < <(find "$dir" -type f)

echo "Total ELF binaries: $total"
echo "With .eh_frame: $with_eh_frame"
echo "With .debuginfo: $with_debuginfo"
