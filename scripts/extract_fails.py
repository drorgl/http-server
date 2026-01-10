#!/usr/bin/env python3

import argparse
import sys

def main():
    parser = argparse.ArgumentParser(description='Extract failed test logs from a file.',
                                     usage='%(prog)s INPUT_FILE OUTPUT_FILE')
    parser.add_argument('input_file', help='The input file containing test output.')
    parser.add_argument('output_file', help='The output file to write failed test logs to.')

    args = parser.parse_args()

    try:
        # Read as binary to handle mixed/messy encodings cross-platform
        with open(args.input_file, 'rb') as f:
            raw_data = f.read()
        
        # Cross-platform decoding logic
        if raw_data.startswith((b'\xff\xfe', b'\xfe\xff')):
            content = raw_data.decode('utf-16')
        elif b'\x00' in raw_data:
            content = raw_data.decode('utf-16-le', errors='replace')
        else:
            content = raw_data.decode('utf-8', errors='replace')
            
        lines = content.splitlines(keepends=True)
        
    except FileNotFoundError:
        print(f"Error: File '{args.input_file}' not found.")
        return 1

    buffer = []
    fails = []

    for line in lines:
        stripped = line.strip()
        
        # Stop at the Unity Summary block
        if 'Tests' in stripped and 'Failures' in stripped and 'Ignored' in stripped:
            print(f"Summary detected: {stripped}")
            break
        
        if ':PASS' in stripped:
            buffer = []
        elif ':FAIL' in stripped:
            print(f"Matched FAIL: {stripped}")
            fails.extend(buffer)
            fails.append(line)
            buffer = []
        else:
            # Only buffer non-empty log lines
            if stripped:
                buffer.append(line)

    # Output is always written as standard UTF-8
    with open(args.output_file, 'w', encoding='utf-8') as f:
        f.writelines(fails)

    print(f"Failed test logs extracted to '{args.output_file}'")
    return 0

if __name__ == "__main__":
    sys.exit(main())