#!/usr/bin/env python3
"""Does the compiled script program carry a one-argument method named SelectMode?

Decodes the STRG and CLAS sections of an extracted L/x3story.obj with the loader
contract from docs/reverse-engineering/selection-native-vm.md (STRG: byte i is
(~s[i]-s[i-1])&255, byte 0 is ~s[0]&255; CLAS method rows are 16 bytes, four
big-endian dwords: CODE entry, STRG name offset, locals, argument count; the
loader 0x0049d030 reads them at +0/+4/+8/+0xc) and prints every method row
named SelectMode with its argument count. The by-name wrapper 0x0049f570
matches the name by content (strcmp in 0x004b0740) and fills missing arguments
with 0 up to the row's count, so a row with count 1 is what SelectMode(258)
needs. Usage: check_selectmode.py <x3story.obj> [...]; extract the asset with
extract_x3story.py (addon/04.cat = stock, addon/07.cat = Mayhem).
"""
import struct
import sys


def decode_strg(raw):
    out = bytearray(len(raw))
    if raw:
        out[0] = (~raw[0]) & 255
    for i in range(1, len(raw)):
        out[i] = (~raw[i] - raw[i - 1]) & 255
    return bytes(out)


def section(data, tag):
    at = data.find(tag)
    if at < 0:
        raise SystemExit(f'{tag!r} section missing')
    length = struct.unpack_from('>I', data, at + 4)[0]
    return data[at + 8:at + 8 + length]


def main(paths):
    for path in paths:
        data = open(path, 'rb').read()
        names = decode_strg(section(data, b'STRG'))
        clas = section(data, b'CLAS')
        offsets = []
        p = 0
        while True:
            p = names.find(b'\x00SelectMode\x00', p)
            if p < 0:
                break
            offsets.append(p + 1)
            p += 1
        rows = []
        for off in offsets:
            key = struct.pack('>I', off)
            for i in range(0, len(clas) - 15):
                if clas[i:i + 4] == key:
                    entry, name, locals_, argc = struct.unpack_from('>IIII', clas, i - 4)
                    if name == off:
                        rows.append({'clas_offset': i - 4, 'code_entry': hex(entry), 'locals': locals_, 'argc': argc})
        print(f'{path}: size={len(data)} strg_offsets={offsets} rows={rows}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
