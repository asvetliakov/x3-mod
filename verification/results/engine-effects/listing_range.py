#!/usr/bin/env python3
"""Print the instructions of an objdump listing between two addresses (inclusive start, exclusive end).

The listing is local and untracked:
  i686-w64-mingw32-objdump -d -M intel --no-show-raw-insn X3AP.exe > /path/x3.lst
  python3 listing_range.py /path/x3.lst 45ad74 45b0b0
"""
import bisect
import sys


def main():
    path, start, end = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
    lines = open(path).read().splitlines()
    addrs, index = [], []
    for i, line in enumerate(lines):
        head = line.split(':', 1)[0].strip()
        if line.startswith('  ') and head and all(c in '0123456789abcdef' for c in head):
            addrs.append(int(head, 16))
            index.append(i)
    a, b = bisect.bisect_left(addrs, start), bisect.bisect_left(addrs, end)
    stop = index[b] if b < len(index) else len(lines)
    print('\n'.join(lines[index[a]:stop]))


if __name__ == '__main__':
    main()
