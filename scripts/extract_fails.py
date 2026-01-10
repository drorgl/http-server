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
        with open(args.input_file, 'r', encoding='utf-16', errors='replace') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"Error: File '{args.input_file}' not found.")
        return 1

    buffer = []
    fails = []

    for line in lines:
        stripped = line.strip()
        if 'Tests' in stripped and 'Failures' in stripped and 'Ignored' in stripped:
            print(f"Summary detected: {stripped}")
            break
        
        if ':PASS' in stripped:
            buffer = []
            print(stripped)
        elif ':FAIL' in stripped:
            print(stripped)
            fails.extend(buffer)
            fails.append(line)
            buffer = []
        else:
            buffer.append(line)

    with open(args.output_file, 'w') as f:
        f.writelines(fails)

    print(f"Failed test logs extracted to '{args.output_file}'")
    return 0

if __name__ == "__main__":
    exit(main())
