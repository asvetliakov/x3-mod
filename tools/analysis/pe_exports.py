#!/usr/bin/env python3
"""Export directory of a 32-bit PE image, read from the file (no loader).

Used by verification/analysis/test_d3d9_exports.py to prove the built proxy
exports the seventeen names of the system d3d9.dll (W1 of
docs/architecture/native-windows-audit-2026-09-12.md): the fifteen CrossOver's
lib/wine/i386-windows/d3d9.dll exports plus the two Windows adds
(Direct3D9EnableMaximizedWindowedModeShim, Direct3DCreate9On12Ex). Pure
Python, standard library only; ordinals, hints and export RVAs are returned so
a report can show where each name lands. Forwarder strings (an RVA inside the
export directory) are returned as the forwarder text.

usage: pe_exports.py <image> [more images]   prints one JSON object per image
"""
import json
import struct
import sys
from pathlib import Path

SYSTEM_D3D9_EXPORTS = (
    'D3DPERF_BeginEvent', 'D3DPERF_EndEvent', 'D3DPERF_GetStatus', 'D3DPERF_QueryRepeatFrame',
    'D3DPERF_SetMarker', 'D3DPERF_SetOptions', 'D3DPERF_SetRegion',
    'DebugSetLevel', 'DebugSetMute',
    'Direct3D9EnableMaximizedWindowedModeShim',
    'Direct3DCreate9', 'Direct3DCreate9Ex', 'Direct3DCreate9On12', 'Direct3DCreate9On12Ex',
    'Direct3DShaderValidatorCreate9',
    'PSGPError', 'PSGPSampleTexture')


class PeError(ValueError):
    pass


def _sections(data, header_offset, count):
    sections = []
    for i in range(count):
        base = header_offset + 40 * i
        name, virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from('<8sIIII', data, base)
        sections.append(dict(name=name.rstrip(b'\0').decode('ascii', 'replace'), virtual_size=virtual_size,
                             virtual_address=virtual_address, raw_size=raw_size, raw_pointer=raw_pointer))
    return sections


def _rva_to_offset(sections, rva):
    for s in sections:
        size = max(s['virtual_size'], s['raw_size'])
        if s['virtual_address'] <= rva < s['virtual_address'] + size:
            return rva - s['virtual_address'] + s['raw_pointer']
    raise PeError('RVA 0x%x outside every section' % rva)


def _cstring(data, offset):
    end = data.index(b'\0', offset)
    return data[offset:end].decode('ascii', 'replace')


def parse(path):
    """Export directory of the image at `path`: dict(machine, image_base, dll_name,
    ordinal_base, exports=[dict(name, ordinal, hint, rva, forwarder)], names=[...])."""
    data = Path(path).read_bytes()
    if data[:2] != b'MZ':
        raise PeError('not an MZ image')
    pe_offset, = struct.unpack_from('<I', data, 0x3c)
    if data[pe_offset:pe_offset + 4] != b'PE\0\0':
        raise PeError('missing PE signature')
    machine, section_count, _, _, _, optional_size, _ = struct.unpack_from('<HHIIIHH', data, pe_offset + 4)
    optional = pe_offset + 24
    magic, = struct.unpack_from('<H', data, optional)
    if magic == 0x10b:
        image_base, = struct.unpack_from('<I', data, optional + 28)
        directory_offset = optional + 96
    elif magic == 0x20b:
        image_base, = struct.unpack_from('<Q', data, optional + 24)
        directory_offset = optional + 112
    else:
        raise PeError('unknown optional header magic 0x%x' % magic)
    export_rva, export_size = struct.unpack_from('<II', data, directory_offset)
    sections = _sections(data, optional + optional_size, section_count)
    result = dict(path=str(path), machine='0x%04x' % machine, image_base='0x%x' % image_base,
                  dll_name=None, ordinal_base=0, exports=[], names=[])
    if not export_rva:
        return result
    base = _rva_to_offset(sections, export_rva)
    (_, _, _, _, name_rva, ordinal_base, function_count, name_count,
     functions_rva, names_rva, ordinals_rva) = struct.unpack_from('<IIHHIIIIIII', data, base)
    result['dll_name'] = _cstring(data, _rva_to_offset(sections, name_rva))
    result['ordinal_base'] = ordinal_base
    functions_offset = _rva_to_offset(sections, functions_rva)
    names_offset = _rva_to_offset(sections, names_rva)
    ordinals_offset = _rva_to_offset(sections, ordinals_rva)
    exports = []
    for hint in range(name_count):
        string_rva, = struct.unpack_from('<I', data, names_offset + 4 * hint)
        index, = struct.unpack_from('<H', data, ordinals_offset + 2 * hint)
        if index >= function_count:
            raise PeError('ordinal index %d beyond the address table' % index)
        rva, = struct.unpack_from('<I', data, functions_offset + 4 * index)
        forwarder = None
        if export_rva <= rva < export_rva + export_size:
            forwarder = _cstring(data, _rva_to_offset(sections, rva))
        exports.append(dict(name=_cstring(data, _rva_to_offset(sections, string_rva)), ordinal=ordinal_base + index,
                            hint=hint, rva='0x%x' % rva, forwarder=forwarder))
    result['exports'] = exports
    result['names'] = sorted(e['name'] for e in exports)
    return result


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    for path in argv[1:]:
        print(json.dumps(parse(path), indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
