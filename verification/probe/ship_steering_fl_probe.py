"""Shared helpers of the two FL ship-steering verifiers (verify_ship_steering_sites_fl.py, verify_ship_avoidance_sites_fl.py):
the claimed-span check (bytes, Capstone instruction lengths, no relative flow, no relocation, no direct edge or relocated pointer
into the interior), the identity-window rows of the src/engine/fl/seta_windows_fl.h form both sites headers use, the anchor rows,
the recursive getter-graph walk (switch tables followed) and the AP refusal. Read-only; nothing is written."""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import seta_fl_probe as P  # noqa: E402,F401

BASE = P.BASE


def span_row(img, name, rva, hexbytes, lengths, allowed_hits=()):
    """One claimed span: exact bytes, whole-instruction lengths, no relative flow, no HIGHLOW inside, no raw branch or relocated
    pointer into bytes 1..n-1 (entering at the first byte is the claim's own contract)."""
    want = bytes.fromhex(hexbytes)
    got = img.rva(rva, len(want))
    insns = img.insns(rva, rva + len(want)) or []
    rel = [i for i in insns if i.mnemonic.startswith(('j', 'call', 'loop', 'ret'))]
    fix = [f for f in img.fixups if rva - 3 <= f < rva + len(want)]
    hits = [h for h in P.raw_branch_hits(img, rva, rva + len(want)) if h not in allowed_hits]
    ptr_in = sorted(p for p in P.pointers(img) if rva < p < rva + len(want))
    ok = got == want and tuple(i.size for i in insns) == tuple(lengths) and not rel and not fix and not hits and not ptr_in
    return {'name': name, 'rva': f'{rva:#08x}', 'continuation': f'{rva + len(want):#08x}', 'ok': ok,
            'lengths': [i.size for i in insns], 'text': [f'{i.mnemonic} {i.op_str}' for i in insns],
            'interior_edges': [[f'{a:#08x}', f'{t:#08x}'] for a, t in hits], 'interior_pointers': [f'{p:#08x}' for p in ptr_in]}


def windows_check(img, spans, header_text):
    """Every (name, rva, length, part, code) window: the record (fixups zeroed), FNV-1a, whole-instruction decode for code
    windows, and equality with the header's row."""
    header = P.header_window_rows(header_text) if header_text else {}
    rows = {}
    for name, rva, length, part, code in spans:
        rec = P.window_record(img, rva, length)
        whole = not code or img.insns(rva, rva + length) is not None
        h = P.fnv1a32(rec[0]) if rec else None
        in_header = header.get(name) == (rva, length, h, rec[1] if rec else None)
        rows[name] = {'ok': rec is not None and whole and in_header, 'rva': f'{rva:#08x}', 'length': length, 'part': part,
                      'fnv1a32': f'{h:08x}' if h is not None else None, 'header': in_header,
                      'fixups': [[o, f'{t:#08x}'] for o, t in rec[1]] if rec else None}
    return {'ok': all(r['ok'] for r in rows.values()) and len(header) == len(spans), 'count': len(rows), **rows}


def emit_windows(img, spans):
    out = []
    for name, rva, length, part, _ in spans:
        raw, fix = P.window_record(img, rva, length)
        out.append(P.window_row(name, rva, length, raw, fix, f' {part},'))
    return '\n'.join(out)


def anchors(img, rows):
    return P.anchor_rows(img, rows)


def graph(img, roots):
    """Recursive descent from each root (RVAs), direct calls and switch tables (`jmp [reg*4 + table]`, relocated entries near the
    jump) followed. Returns the functions, instruction count, switch tables and every x87, SIMD, indirect call/jump or
    undecodable instruction."""
    seen, funcs, todo, bad, tables, stores = set(), set(roots), list(roots), [], [], []
    while todo:
        pc = todo.pop()
        while pc not in seen:
            seen.add(pc)
            code = img.rva(pc, 16)
            i = next(img.md.disasm(code, BASE + pc), None) if code else None
            if i is None:
                bad.append((pc, 'undecodable'))
                break
            text = f'{i.mnemonic} {i.op_str}'
            if i.mnemonic.startswith('f') or 'st(' in i.op_str:
                bad.append((pc, 'x87 ' + text))
            if 'xmm' in i.op_str or re.search(r'\bmm\d', i.op_str):
                bad.append((pc, 'simd ' + text))
            if i.mnemonic.startswith('mov') and re.match(r'(byte|word|dword) ptr \[', i.op_str):
                stores.append((pc, text))
            if i.mnemonic == 'call':
                if i.op_str.startswith('0x'):
                    t = int(i.op_str, 16) - BASE
                    if t not in funcs:
                        funcs.add(t)
                        todo.append(t)
                else:
                    bad.append((pc, 'indirect ' + text))
            elif i.mnemonic == 'jmp':
                if i.op_str.startswith('0x'):
                    todo.append(int(i.op_str, 16) - BASE)
                else:
                    m = re.match(r'dword ptr \[e[a-z]x\*4 \+ (0x[0-9a-f]+)\]$', i.op_str)
                    if not m:
                        bad.append((pc, 'indirect ' + text))
                    else:
                        tab, k = int(m.group(1), 16) - BASE, 0
                        while tab + 4 * k in img.fixups:
                            v = struct.unpack('<I', img.rva(tab + 4 * k, 4))[0] - BASE
                            if abs(v - pc) > 0x1000:
                                break
                            todo.append(v)
                            k += 1
                        tables.append((pc, tab, k))
                break
            elif i.mnemonic.startswith('j') or i.mnemonic.startswith('loop'):
                todo.append(int(i.op_str, 16) - BASE)
            elif i.mnemonic == 'ret':
                break
            pc += i.size
    return {'functions': sorted(funcs), 'instructions': len(seen), 'tables': sorted(tables), 'bad': bad, 'stores': stores}


def ap_refusal(img, ap_spans, header_texts):
    """AP preferred VAs and their AP bytes: none may match in X3FL.exe, and no AP VA may appear in an FL header."""
    rows = []
    for va, hexbytes in ap_spans:
        got = img.read(va, len(hexbytes) // 2)
        rows.append({'va': f'{va:#010x}', 'matches': got == bytes.fromhex(hexbytes)})
    text = '\n'.join(header_texts)
    in_header = [f'{va:#010x}' for va, _ in ap_spans if f'{va:#010x}' in text or f'{va:#x}' in text]
    return {'ok': not any(r['matches'] for r in rows) and not in_header, 'ap_spans': rows, 'ap_in_header': in_header}
