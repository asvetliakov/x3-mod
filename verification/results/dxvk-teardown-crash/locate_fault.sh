#!/bin/sh
# Locates the DXVK teardown fault of a crashed state_hook_benchmark proxy case.
# Usage: sh locate_fault.sh <case-dir>   (case-dir: verification/probe/build/state-hook-benchmark-proxy-timing-off-*)
D="/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/dxvk/i386-windows/d3d9.dll"
c="$1"
grep -E "^(Backtrace|=>|  [0-9] 0x|PE .*d3d9|EIP|EAX|ESI)" "$c/stdout.txt"           # winedbg block
grep -E "^(device_hooked|motion_output_release|device_destroy|exception )" "$c/x3m.log" | cut -c1-160
# proxy return address 0x76a9461a, loaded at 0x76a50000, ImageBase 0x6fb40000 -> capture.cpp release_device
i686-w64-mingw32-addr2line -f -C -e "$c/d3d9.dll" 0x6fb8461a
# DXVK return address 0x75ebd6d1, loaded at 0x75d40000, ImageBase 0x62440000 -> 0x625bd6d1 (Release, final branch)
i686-w64-mingw32-objdump -d --no-show-raw-insn --start-address=0x625bd697 --stop-address=0x625bd6d3 "$D"
# D3D9DeviceEx vtable address point 0x627409a8: slot 134 (0x218) = D1, slot 135 (0x21c) = D0
i686-w64-mingw32-objdump -s --start-address=0x62740bc0 --stop-address=0x62740bc8 "$D" | tail -1
