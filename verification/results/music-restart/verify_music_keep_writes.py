#!/usr/bin/env python3
"""Read-only check of what src/proxy/music_keep.cpp compares and writes.

Sibling of verify_music_restart_sites.py for the implementation: every byte
window and address constant of src/proxy/music_keep_core.h is read from the
header and compared with the unmodified X3AP.exe; the stop-all and play caller
tables are checked (E8 target and return address); the five claim sites are
decoded as whole instructions with a minimal length table; the bytes the DLL
writes are modelled (`e9 rel32` over the first five bytes of a claim, the
remaining site bytes untouched, the tail = displaced bytes + `e9` back to
site+length; `e8 rel32` to the thunk at the call site) and the raw .text scan
shows no rel8/rel32 branch into any site's interior; the direct-caller census
of the six functions involved is repeated. No Wine, no game. Prints one JSON
object (also written beside this script) and exits 0 only on PASS.

`parse_line` reads the music_trace_* / music_keep_* lines a flight writes.
"""
import hashlib
import json
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CORE = ROOT / 'src/proxy/music_keep_core.h'
MODULE = ROOT / 'src/proxy/music_keep.cpp'
EXE = Path(os.environ.get('X3AP_EXE', Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe'))
SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
TEXT_VA, TEXT_RAW, TEXT_SIZE = 0x401000, 0x400, 1247232
OUTPUT = Path(__file__).with_name('verify_writes_output.json')

# (constant name of the window, constant name of its VA, kind) — kind: 'window' compared whole; 'claim' also decoded and modelled as a written site
WINDOWS = [
    ('a_window', 'a_window_va'), ('a_skip_window', 'a_skip_va'), ('c_pre_window', 'c_pre_va'), ('c_post_window', 'c_post_va'),
    ('play_set_playing_window', 'play_set_playing_va'), ('seek_head', 'c_target_va'), ('pause_body', 'pause_va'), ('run_head', 'run_va'),
    ('stop_all_head', 't_stop_all_site_va'), ('play_head', 't_play_site_va'), ('stop_movie_head', 't_stop_movie_site_va'),
]
# Claim sites: (name, VA constant, length constant, window the expected bytes come from, offset constant into that window or None)
CLAIMS = [
    ('music_keep_a', 'a_site_va', 'a_site_length', 'a_window', 'a_site_offset'),
    ('music_trace_stop_all', 't_stop_all_site_va', 't_stop_all_length', 'stop_all_head', None),
    ('music_trace_play', 't_play_site_va', 't_play_length', 'play_head', None),
    ('music_trace_stop_movie', 't_stop_movie_site_va', 't_stop_movie_length', 'stop_movie_head', None),
]
CALL_SITE = ('music_keep_c', 'c_site_va', 'c_target_va')
# Expected address constants (the note's sites) and the caller tables.
EXPECTED = {
    'stop_all_va': 0x4982b0, 'stop_all_end_va': 0x498367, 'a_site_va': 0x4982db, 'a_next_va': 0x4982e1, 'a_skip_va': 0x498322, 'a_site_length': 6, 'a_return_slot': 0x10,
    'a_window_va': 0x4982c0, 'a_window_length': 38, 'a_site_offset': 27, 'a_skip_length': 15, 'play_va': 0x498c90, 'play_end_va': 0x498e28,
    'c_site_va': 0x498d54, 'c_target_va': 0x4d0430, 'c_return_va': 0x498d59, 'c_pre_va': 0x498d4d, 'c_post_va': 0x498d59, 'c_run_call_va': 0x498d71,
    'run_va': 0x4d1870, 'pause_va': 0x4d1810, 'call_length': 5, 'c_pre_length': 7, 'c_post_length': 29, 'play_set_playing_va': 0x498d8c, 'play_set_playing_length': 6,
    'seek_head_length': 26, 'pause_body_length': 0x57, 'run_head_length': 14, 't_stop_all_site_va': 0x4982b0, 't_stop_all_length': 5, 'stop_all_head_length': 16,
    't_play_site_va': 0x498c90, 't_play_length': 6, 'play_head_length': 18, 't_stop_movie_site_va': 0x498810, 't_stop_movie_length': 6, 'stop_movie_head_length': 14,
    'play_arg_id': 3, 'play_arg_minutes': 4, 'play_arg_seconds': 5, 'play_arg_ms': 6, 'trace_line_cap': 1000, 'list_head_va': 0x606f44,
    'record_id': 0x10, 'record_context': 0x14, 'record_slot': 0x18, 'record_media': 0x24, 'record_flags': 0x2c, 'flag_loop': 1, 'flag_playing': 2, 'flag_directsound': 0x40, 'flag_music': 0x80,
    'list_walk_limit': 4096, 'hold_capacity': 4,
}
STOP_CALLERS = [(0x4d36bd, 0x4d36c2, 'alt_tab', 'paused'), (0x40455c, 0x404561, 'save', 'keep_running'), (0x407064, 0x407069, 'pause', 'keep_running'),
                (0x404ced, 0x404cf2, 'load', 'vanilla'), (0x497bb6, 0x497bbb, 'p_leave', 'vanilla'), (0x403878, 0x40387d, 'session_start', 'vanilla')]
PLAY_CALLERS = [(0x49981f, 0x499824, 'MOV_PlayMovie'), (0x499982, 0x499987, 'MOV_PlayMovieFrom'), (0x4f6668, 0x4f666d, 'helper_0x004f6640')]
STOP_MOVIE_CALLERS = [(0x499881, 0x499886, 'MOV_StopMovie'), (0x45c27d, 0x45c282, 'selector_0x0045b720'), (0x4f66be, 0x4f66c3, 'caller_0x004f66be')]
CALLEES = {0x4982b0: 6, 0x498c90: 3, 0x498810: 3, 0x4d0430: 3, 0x4d1810: 1, 0x4d1870: 2}
# Raw-scan hits proven to be operand bytes of a containing instruction: 0x498c6f is the ModRM byte 0x7c of
# `lea edi,[esp+0x18]` (8d 7c 24 18 at 0x498c6e), decoded as a false `jl` into 0x498c93.
REJECTED_INTERIOR = {0x498c90: {0x498c6f}}
STOP_MOVIE_PAUSE_CALL = 0x49885f
# Minimal decoder for the displaced instructions (opcode bytes -> length); anything else is a failure.
LENGTHS = {
    b'\x8b\x7e\x24': 3,        # mov edi,[esi+0x24]
    b'\x39\x5f\x04': 3,        # cmp [edi+4],ebx
    b'\xa1': 5,                # mov eax,[imm32]
    b'\x51': 1, b'\x53': 1,    # push ecx / push ebx
    b'\x8b\x5c\x24\x14': 4,    # mov ebx,[esp+0x14] (ESP-relative: replayed by the tail at the entry ESP, which the stub restores)
    b'\x8b\x0d': 6,            # mov ecx,[imm32]
}

_ARRAY = re.compile(r'constexpr unsigned char (\w+)\[(\w+)\] = \{([^}]*)\};')
_CONST = re.compile(r'\b(\w+) = (0x[0-9a-fA-F]+|\d+)\b')
_STOP = re.compile(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), "(\w+)", StopMode::(\w+)\}')
_PLAY = re.compile(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+), "([\w.]+)"\}')


def source_constants(text):
    consts = {}
    for name, value in _CONST.findall(text):
        if name in consts:
            continue
        consts[name] = int(value, 0)
    # derived constants the header spells as expressions
    consts.setdefault('a_site_offset', consts['a_site_va'] - consts['a_window_va'])
    for alias, base in (('t_stop_all_site_va', 'stop_all_va'), ('t_play_site_va', 'play_va')):
        consts.setdefault(alias, consts[base])
    return consts


def source_windows(text):
    return {name: bytes(int(b, 0) for b in re.findall(r'0x[0-9a-fA-F]{2}', body)) for name, _, body in _ARRAY.findall(text)}


def source_callers(text):
    stops = [(int(a, 16), int(b, 16), n, m) for a, b, n, m in _STOP.findall(text)]
    plays = [(int(a, 16), int(b, 16), n) for a, b, n in _PLAY.findall(text)]
    return stops, plays


def decode_span(raw):
    """Instruction lengths of a displaced span; None when an opcode is unknown or the span does not end on a boundary."""
    lengths, i = [], 0
    while i < len(raw):
        for prefix, length in LENGTHS.items():
            if raw[i:i + len(prefix)] == prefix:
                lengths.append(length)
                i += length
                break
        else:
            return None
    return lengths if i == len(raw) else None


def written_bytes(site, length, original, dispatcher):
    """What claim() writes at the site (five bytes) and what the tail holds, for a dispatcher at `dispatcher`."""
    patched = b'\xe9' + struct.pack('<i', dispatcher - (site + 5)) + original[5:length]
    tail = original[:length] + b'\xe9'   # + rel32 to site+length, which depends on the tail's own address
    return patched, tail


def scan(text, sites, callees):
    interior = {s: [] for s in sites}
    callers = {t: [] for t in callees}
    for i in range(len(text) - 6):
        va = TEXT_VA + i
        op = text[i]
        targets = []
        if op in (0xe8, 0xe9):
            targets.append((va + 5 + struct.unpack_from('<i', text, i + 1)[0]) & 0xffffffff)
        elif op == 0xeb or 0x70 <= op <= 0x7f:
            targets.append((va + 2 + struct.unpack_from('<b', text, i + 1)[0]) & 0xffffffff)
        elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f:
            targets.append((va + 6 + struct.unpack_from('<i', text, i + 2)[0]) & 0xffffffff)
        for t in targets:
            if op == 0xe8 and t in callers:
                callers[t].append(va)
            for s, n in sites.items():
                if s < t < s + n:
                    interior[s].append(va)
    return interior, callers


def inspect(data, core_text):
    sha = hashlib.sha256(data).hexdigest()
    text = data[TEXT_RAW:TEXT_RAW + TEXT_SIZE]

    def rd(va, n):
        return text[va - TEXT_VA:va - TEXT_VA + n]

    def call_target(va):
        raw = rd(va, 5)
        return (va + 5 + struct.unpack('<i', raw[1:])[0]) & 0xffffffff if raw[:1] == b'\xe8' else None

    consts, windows = source_constants(core_text), source_windows(core_text)
    stops, plays = source_callers(core_text)
    checks = {'sha256': sha == SHA256}
    checks['constants'] = all(consts.get(k) == v for k, v in EXPECTED.items())
    for name, va_name in WINDOWS:
        raw = windows.get(name, b'')
        checks['window_' + name] = bool(raw) and len(raw) == consts.get(name + '_length', consts.get(name.replace('_window', '') + '_length', len(raw))) and rd(consts[va_name], len(raw)) == raw
    checks['a_skip_is_bookkeeping_label'] = rd(consts['a_skip_va'], 4) == b'\x8b\x0d\xe4\x85' and consts['a_skip_va'] == 0x498322
    checks['a_je_consumes_tail_cmp'] = rd(consts['a_next_va'], 2) == b'\x74\x13'
    # Claim sites: whole instructions, expected bytes from the window, the written form.
    sites, written = {}, {}
    for name, va_name, len_name, window_name, offset_name in CLAIMS:
        va, n = consts[va_name], consts[len_name]
        offset = consts[offset_name] if offset_name else 0
        expected = windows[window_name][offset:offset + n]
        original = rd(va, n)
        lengths = decode_span(original)
        checks['claim_' + name] = original == expected and lengths is not None and n >= 5 and sum(lengths) == n
        sites[va] = n
        patched, tail = written_bytes(va, n, original, 0x0a100000)
        written[name] = {'site': hex(va), 'length': n, 'original': original.hex(), 'instructions': lengths, 'writes_example_dispatcher_0x0a100000': patched.hex(), 'tail_prefix': tail.hex(),
                         'tail_jump_target': hex(va + n)}
    c_name, c_site_name, c_target_name = CALL_SITE
    c_site, c_target = consts[c_site_name], consts[c_target_name]
    checks['call_' + c_name] = call_target(c_site) == c_target and rd(consts['c_run_call_va'], 5)[:1] == b'\xe8' and call_target(consts['c_run_call_va']) == consts['run_va']
    sites[c_site] = 5
    written[c_name] = {'site': hex(c_site), 'length': 5, 'original': rd(c_site, 5).hex(), 'writes_example_thunk_0x0a200000': (b'\xe8' + struct.pack('<i', 0x0a200000 - (c_site + 5))).hex(),
                       'callee': hex(c_target), 'caller_pops': 4}
    checks['stop_movie_calls_pause'] = call_target(STOP_MOVIE_PAUSE_CALL) == consts['pause_va']
    checks['stop_callers'] = [(a, b, n, m) for a, b, n, m in stops] == STOP_CALLERS and all(call_target(a) == consts['stop_all_va'] and b == a + 5 for a, b, _, _ in stops)
    checks['play_callers'] = plays[:len(PLAY_CALLERS)] == PLAY_CALLERS and all(call_target(a) == consts['play_va'] and b == a + 5 for a, b, _ in plays[:len(PLAY_CALLERS)])
    stop_movies = plays[len(PLAY_CALLERS):]
    checks['stop_movie_callers'] = stop_movies == STOP_MOVIE_CALLERS and all(call_target(a) == consts['t_stop_movie_site_va'] and b == a + 5 for a, b, _ in stop_movies)
    interior, callers = scan(text, sites, CALLEES)
    for s, n in sites.items():
        checks['interior_%x' % s] = not (set(interior[s]) - REJECTED_INTERIOR.get(s, set()))
    for t, n in CALLEES.items():
        checks['callers_%x' % t] = len(callers[t]) == n
    checks['stop_all_callers_are_the_table'] = sorted(callers[0x4982b0]) == sorted(a for a, _, _, _ in STOP_CALLERS)
    checks['play_callers_are_the_table'] = sorted(callers[0x498c90]) == sorted(a for a, _, _ in PLAY_CALLERS)
    checks['stop_movie_callers_are_the_table'] = sorted(callers[0x498810]) == sorted(a for a, _, _ in STOP_MOVIE_CALLERS)
    literal = {hex(t): data.count(struct.pack('<I', t)) for t in CALLEES}
    checks['no_literal_refs'] = not any(literal.values())
    failures = sorted(k for k, v in checks.items() if not v)
    return {'exe_sha256': sha, 'checks': checks, 'check_count': len(checks), 'windows': {k: len(v) for k, v in windows.items()}, 'written': written,
            'direct_callers': {hex(t): [hex(v) for v in c] for t, c in callers.items()}, 'literal_dword_refs': literal,
            'raw_branch_into_site_interior': {hex(s): [hex(v) for v in c] for s, c in interior.items()},
            'rejected_operand_byte_hits': {hex(k): sorted(hex(v) for v in vs) for k, vs in REJECTED_INTERIOR.items()},
            'failures': failures, 'result': 'PASS' if not failures else 'FAIL'}


_LINE = re.compile(r'^(?:\S+ )?(music_trace_stop|music_trace_play|music_trace_stop_movie|music_keep_stop|music_keep_seek|music_keep_orphan|music_keep_stop_movie|music_walk_cut|music_keep|music_trace|music_trace_cap)\b(.*)$')


def parse_line(line):
    """One music_* line -> {'kind': ..., fields} with integers decoded; None for any other line."""
    m = _LINE.match(line.strip())
    if not m:
        return None
    row = {'kind': m.group(1)}
    for key, value in re.findall(r'(\w+)=(\S+)', m.group(2)):
        if re.fullmatch(r'-?\d+', value):
            row[key] = int(value)
        elif re.fullmatch(r'0x[0-9a-fA-F]+', value):
            row[key] = int(value, 16)
        else:
            row[key] = value
    return row


def restart_pairs(rows):
    """(stop, play) pairs of a trace: each music_trace_stop with the next music_trace_play; `same_id` needs a
    preceding play of that id, `seek_skipped` a music_keep_seek action=skip between them."""
    pairs, last_id, pending, skipped = [], None, None, False
    for r in rows:
        if r['kind'] == 'music_trace_play':
            if pending is not None:
                pairs.append({'stop_caller': pending.get('name'), 'stop_seq': pending.get('seq'), 'play_seq': r.get('seq'), 'id': r.get('id'), 'start_ms': r.get('start_ms'),
                              'same_id': last_id == r.get('id'), 'seek_skipped': skipped})
                pending, skipped = None, False
            last_id = r.get('id')
        elif r['kind'] == 'music_trace_stop':
            pending, skipped = r, False
        elif r['kind'] == 'music_keep_seek' and r.get('action') == 'skip':
            skipped = True
    return pairs


def verify(exe=EXE):
    return inspect(exe.read_bytes(), CORE.read_text())


def main():
    report = verify()
    OUTPUT.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in ('exe_sha256', 'check_count', 'failures', 'result')}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
