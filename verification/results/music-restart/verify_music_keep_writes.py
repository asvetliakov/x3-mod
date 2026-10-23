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
of the six functions involved is repeated. Music through the save
(music-restart.md section 7): Patch E at 0x00498d71 (whole instruction, its
continuation, the written `e8 rel32`, the interior scan), the pump's head and
drain exit, the status query's DirectSound tail as the pump's only direct
entry, no literal pump reference, the gzwrite thunk and its IAT slot from the
import directory (zlib1.dll!gzwrite); the resume's position reader 0x004d0600
(interfaces, vtable offsets, the 10000 divisor and the 1000.0 factor). No Wine,
no game. Prints one JSON object (also written beside this script) and exits 0
only on PASS. The executable: X3AP_EXE, else the native install
/mnt/c/X3/X3AP.exe, else the CrossOver bottle path.

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
_CANDIDATES = [Path('/mnt/c/X3/X3AP.exe'), Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe']
EXE = Path(os.environ['X3AP_EXE']) if os.environ.get('X3AP_EXE') else next((c for c in _CANDIDATES if c.is_file()), _CANDIDATES[-1])
SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
TEXT_VA, TEXT_RAW, TEXT_SIZE = 0x401000, 0x400, 1247232
OUTPUT = Path(__file__).with_name('verify_writes_output.json')

# (constant name of the window, constant name of its VA, kind) — kind: 'window' compared whole; 'claim' also decoded and modelled as a written site
WINDOWS = [
    ('a_window', 'a_window_va'), ('a_skip_window', 'a_skip_va'), ('a_next_record_window', 'a_next_record_va'), ('c_pre_window', 'c_pre_va'), ('c_post_window', 'c_post_va'),
    ('play_set_playing_window', 'play_set_playing_va'), ('seek_head', 'c_target_va'), ('pause_body', 'pause_va'), ('run_head', 'run_va'),
    ('stop_all_head', 't_stop_all_site_va'), ('play_head', 't_play_site_va'), ('stop_movie_head', 't_stop_movie_site_va'),
    ('d_caller_window', 'd_caller_va'), ('status_head', 'd_target_va'), ('status_ended_window', 'status_ended_va'),
    ('pump_head', 'pump_va'), ('pump_end', 'pump_end_va'), ('status_ds_tail', 'status_ds_tail_va'), ('e_post_window', 'e_return_va'),
    ('gzwrite_thunk', 'gzwrite_thunk_va'), ('position_reader', 'position_reader_va'),
    ('gzclose_wrapper', 'gzclose_wrapper_va'), ('gzclose_thunk', 'gzclose_thunk_va'),
    # the track queue (music-script-driver.md section 8.3): the delete-by-id body, the completion delivery of the
    # MOV_StopMovie body, the KC layout witnesses (jump handler, loader stores)
    ('free_by_id_body', 'free_by_id_va'), ('completion_window', 'completion_window_va'), ('kc_jump_handler', 'kc_jump_handler_va'),
    ('kc_loader_code_store', 'kc_loader_code_store_va'), ('kc_loader_build_store', 'kc_loader_build_store_va'),
]
# Claim sites: (name, VA constant, length constant, window the expected bytes come from, offset constant into that window or None)
CLAIMS = [
    ('music_keep_a', 'a_site_va', 'a_site_length', 'a_window', 'a_site_offset'),
    ('music_trace_stop_all', 't_stop_all_site_va', 't_stop_all_length', 'stop_all_head', None),
    ('music_trace_play', 't_play_site_va', 't_play_length', 'play_head', None),
    ('music_trace_stop_movie', 't_stop_movie_site_va', 't_stop_movie_length', 'stop_movie_head', None),
]
CALL_SITE = ('music_keep_c', 'c_site_va', 'c_target_va')
STATUS_CALL_SITE = ('music_keep_d', 'd_site_va', 'd_target_va')
RUN_CALL_SITE = ('music_keep_e', 'e_site_va', 'e_target_va')
# Expected address constants (the note's sites) and the caller tables.
EXPECTED = {
    'stop_all_va': 0x4982b0, 'stop_all_end_va': 0x498367, 'a_site_va': 0x4982db, 'a_next_va': 0x4982e1, 'a_skip_va': 0x498322, 'a_site_length': 6, 'a_return_slot': 0x10,
    'a_window_va': 0x4982c0, 'a_window_length': 38, 'a_site_offset': 27, 'a_skip_length': 15,
    'a_next_record_va': 0x498359, 'a_next_record_length': 14, 'a_loop_head_va': 0x4982d0, 'a_not_playing_je_va': 0x4982d9, 'play_va': 0x498c90, 'play_end_va': 0x498e28,
    'c_site_va': 0x498d54, 'c_target_va': 0x4d0430, 'c_return_va': 0x498d59, 'c_pre_va': 0x498d4d, 'c_post_va': 0x498d59, 'c_run_call_va': 0x498d71,
    'run_va': 0x4d1870, 'pause_va': 0x4d1810, 'call_length': 5, 'c_pre_length': 7, 'c_post_length': 29, 'play_set_playing_va': 0x498d8c, 'play_set_playing_length': 6,
    'seek_head_length': 26, 'pause_body_length': 0x57, 'run_head_length': 14, 't_stop_all_site_va': 0x4982b0, 't_stop_all_length': 5, 'stop_all_head_length': 16,
    't_play_site_va': 0x498c90, 't_play_length': 6, 'play_head_length': 18, 't_stop_movie_site_va': 0x498810, 't_stop_movie_length': 6, 'stop_movie_head_length': 14,
    'play_arg_id': 3, 'play_arg_minutes': 4, 'play_arg_seconds': 5, 'play_arg_ms': 6, 'trace_line_cap': 1000, 'list_head_va': 0x606f44,
    'record_id': 0x10, 'record_context': 0x14, 'record_slot': 0x18, 'record_media': 0x24, 'record_flags': 0x2c, 'flag_loop': 1, 'flag_playing': 2, 'flag_directsound': 0x40, 'flag_music': 0x80,
    'list_walk_limit': 4096, 'hold_capacity': 4,
    'd_site_va': 0x4983d9, 'd_target_va': 0x4d14e0, 'd_return_va': 0x4983de, 'd_caller_va': 0x4983d4, 'd_caller_length': 41, 'status_head_length': 38,
    'status_ended_va': 0x4d15a0, 'status_ended_length': 12, 'status_normal_va': 0x4d1506, 'active_flag_va': 0x608adc, 'input_flags_ptr_va': 0x606f3c,
    'run_in_background_bit': 0x4000,
    # music through the save (section 7) and the resume after combat (music-script-driver.md section 4)
    'save_return_va': 0x404561, 'pump_va': 0x4d0700, 'pump_end_va': 0x4d0c0c, 'status_ds_tail_va': 0x4d17b5, 'pump_head_length': 55, 'pump_end_length': 37,
    'status_ds_tail_length': 28, 'media_stream': 0x04, 'media_buffer_state': 0x48, 'media_position': 0x70, 'media_flags': 0x8c, 'media_ds_mask': 0x48,
    'media_ds_value': 0x40, 'media_stream_clock': 0x20, 'buffer_playing': 2, 'buffer_draining': 4, 'gzwrite_iat_va': 0x5323e0, 'gzwrite_thunk_va': 0x4fae46,
    'gzwrite_thunk_length': 6, 'pump_interval_ms': 250, 'pump_poll_stride': 32, 'pump_bound_ms': 120000, 'e_site_va': 0x498d71, 'e_target_va': 0x4d1870,
    'e_return_va': 0x498d76, 'e_post_length': 35, 'stop_movie_native_return_va': 0x499886, 'combat_id_first': 1001, 'combat_id_last': 1042,
    'position_reader_va': 0x4d0600, 'position_reader_length': 120, 'position_vtbl_get_current': 0x24, 'stream_vtbl_get_time': 0x20, 'seek_resume_bit': 4,
    # the save's own gzclose (review fix 4), the Run-skip bound (fix 2), the trace's task fields (script-task-scheduler.md section 2-3)
    'save_gzclose_call_va': 0x404a6f, 'save_gzclose_return_va': 0x404a74, 'gzclose_wrapper_va': 0x4e9360, 'gzclose_wrapper_return_va': 0x4e9384,
    'gzclose_thunk_va': 0x4fae52, 'gzclose_iat_va': 0x5323f0, 'gzclose_wrapper_length': 39, 'gzclose_thunk_length': 6, 'run_skip_bound_ms': 2500,
    'task_id': 0x08, 'task_capacity': 0x10, 'task_cells': 0x14, 'task_index': 0x18, 'task_ip': 0x1c, 'cell_size': 5, 'alert_record_ptr_va': 0x608504,
    # the track queue (music-script-driver.md section 8.3 as amended by 8.4; docs/architecture/music-queue.md)
    'vm_ptr_va': 0x6085e4, 'vm_slot_guard': 0x04, 'vm_code': 0x08, 'vm_code_size': 0x10, 'vm_build': 0x143c, 'slot_stride': 0x18,
    'slot_completion': 0x34, 'slot_count': 0x20, 'completion_window_va': 0x498864, 'completion_window_length': 42,
    'free_by_id_va': 0x4986b0, 'free_by_id_caller_va': 0x4986f0, 'free_by_id_length': 51, 'kc_jump_handler_va': 0x4a317a,
    'kc_loader_code_store_va': 0x49d332, 'kc_loader_build_store_va': 0x49d2a7, 'kc_jump_handler_length': 9,
    'kc_loader_code_store_length': 29, 'kc_loader_build_store_length': 6, 'story_build': 1462141839, 'warp_fork_offset': 0x161c3,
    'warp_continue_offset': 0x161d7, 'warp_method_entry': 0x16185, 'warp_window_length': 6, 'kc_op_fork': 0x6f, 'kc_op_jt': 0x33,
    'kc_op_jmp': 0x32, 'kc_op_filler': 0x01, 'kc_check_stride_frames': 8, 'kc_play_stop_ip': 0x0db54, 'kc_fade_stop_ip': 0x0da17,
    'kc_play_ip': 0x0dbf7, 'kc_loop_music_return': 0x0dc84, 'kc_change_track_return': 0x0dcdb, 'queue_stack_depth': 19,
    'queue_cell_old': 18, 'queue_cell_loop_return': 13, 'queue_cell_new': 11, 'queue_cell_fade': 10, 'queue_cell_loop': 9,
    'queue_cell_change_return': 7, 'tag_int': 1, 'tag_return': 3, 'queue_cell_count': 6, 'id_death': 13, 'id_landing': 14,
    'boarding_id_first': 8233, 'boarding_id_last': 8237, 'queue_key_base': 0x7f000000, 'queue_key_mask': 0xffff,
}
STOP_CALLERS = [(0x4d36bd, 0x4d36c2, 'alt_tab', 'skip_all'), (0x40455c, 0x404561, 'save', 'keep_running'), (0x407064, 0x407069, 'pause', 'keep_running'),
                (0x404ced, 0x404cf2, 'load', 'vanilla'), (0x497bb6, 0x497bbb, 'p_leave', 'vanilla'), (0x403878, 0x40387d, 'session_start', 'vanilla')]
PLAY_CALLERS = [(0x49981f, 0x499824, 'MOV_PlayMovie'), (0x499982, 0x499987, 'MOV_PlayMovieFrom'), (0x4f6668, 0x4f666d, 'helper_0x004f6640')]
STOP_MOVIE_CALLERS = [(0x499881, 0x499886, 'MOV_StopMovie'), (0x45c27d, 0x45c282, 'selector_0x0045b720'), (0x4f66be, 0x4f66c3, 'caller_0x004f66be')]
CALLEES = {0x4982b0: 6, 0x498c90: 3, 0x498810: 3, 0x4d0430: 3, 0x4d1810: 1, 0x4d1870: 2, 0x4d14e0: 1, 0x4986b0: 1}
# Raw-scan hits proven to be operand bytes of a containing instruction: 0x498c6f is the ModRM byte 0x7c of
# `lea edi,[esp+0x18]` (8d 7c 24 18 at 0x498c6e), decoded as a false `jl` into 0x498c93.
# 0x498d4e is the ModRM byte 0x7c of the 4-byte `add edi,[esp+0x2c]` at 0x498d4d, decoded as a false `jl` into 0x498d74
# (inside Patch E's call), the rejection of music-restart.md section 7.5.
REJECTED_INTERIOR = {0x498c90: {0x498c6f}, 0x498d71: {0x498d4e}}
PUMP_LITERAL = 0x4d0700
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
    for alias, base in (('t_stop_all_site_va', 'stop_all_va'), ('t_play_site_va', 'play_va'), ('e_site_va', 'c_run_call_va'), ('e_target_va', 'run_va')):
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


def sections(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    out = []
    for i in range(count):
        o = pe + 24 + optional + 40 * i
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, o + 8)
        out.append((rva, max(virtual_size, raw_size), raw))
    return pe, out


def read_va(data, va, n):
    """Bytes at a VA of any section (image base 0x400000), b'' when unmapped."""
    _, secs = sections(data)
    rva = va - 0x400000
    for start, size, raw in secs:
        if start <= rva < start + size:
            return data[raw + rva - start:raw + rva - start + n]
    return b''


def import_slot(data, dll, name):
    """VA of the IAT slot the import directory binds to dll!name (by its OriginalFirstThunk name), or None."""
    pe, _ = sections(data)
    directory = struct.unpack_from('<II', data, pe + 24 + 104)[0]   # DataDirectory[1] (imports) of a PE32 optional header
    for i in range(256):
        desc = read_va(data, 0x400000 + directory + 20 * i, 20)
        if len(desc) < 20:
            return None
        names, _, _, dll_rva, first = struct.unpack('<IIIII', desc)
        if not dll_rva:
            return None
        if read_va(data, 0x400000 + dll_rva, len(dll) + 1).lower() != dll.lower().encode() + b'\0':
            continue
        for j in range(4096):
            entry = struct.unpack('<I', read_va(data, 0x400000 + names + 4 * j, 4))[0]
            if not entry:
                return None
            if entry & 0x80000000:
                continue
            if read_va(data, 0x400000 + entry + 2, len(name) + 1) == name.encode() + b'\0':
                return 0x400000 + first + 4 * j
    return None


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
    # The skip_all exit's target is the per-record loop's continue: the loop's own `je` for a record without flag 2
    # (at a_not_playing_je_va, inside a_window) lands on it, and its `jne` goes back to the loop head inside a_window.
    je_at, cont, head = consts['a_not_playing_je_va'], consts['a_next_record_va'], consts['a_loop_head_va']
    je = rd(je_at, 2)
    jne = rd(cont + 3, 6)
    checks['a_next_record_is_loop_continue'] = (
        je[:1] == b'\x74' and je_at + 2 + struct.unpack('<b', je[1:])[0] == cont and consts['a_window_va'] <= je_at < consts['a_window_va'] + consts['a_window_length'] - 1
        and jne[:2] == b'\x0f\x85' and cont + 9 + struct.unpack('<i', jne[2:])[0] == head and consts['a_window_va'] <= head < consts['a_site_va']
        and consts['a_site_va'] < cont < consts['stop_all_end_va'])
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
    # Patch D: the update's only call of the status query, redirected; the query's inactive arm (active flag 0 and the
    # RunInBackground bit clear -> the "ended" block) and the caller's contract (AX 1 -> position, AX 2 -> ended).
    d_name, d_site_name, d_target_name = STATUS_CALL_SITE
    d_site, d_target = consts[d_site_name], consts[d_target_name]
    head = rd(d_target, consts['status_head_length'])

    def rel8(at):
        raw = rd(at, 2)
        return at + 2 + struct.unpack('<b', raw[1:])[0]

    def rel32(at):
        return at + 6 + struct.unpack('<i', rd(at + 2, 4))[0]

    checks['call_' + d_name] = (call_target(d_site) == d_target and d_site == consts['d_caller_va'] + 5 and d_site + 5 == consts['d_return_va']
                                and rd(consts['d_return_va'], 7) == bytes.fromhex('0fb7c0663d0100') and rd(0x4983fb - 4, 4) == bytes.fromhex('663d0200'))
    checks['status_inactive_arm'] = (
        head[11:17] == b'\x39\x2d' + struct.pack('<I', consts['active_flag_va']) and rd(0x4d14f2, 1) == b'\x75' and rel8(0x4d14f2) == consts['status_normal_va']
        and head[20:26] == b'\x8b\x15' + struct.pack('<I', consts['input_flags_ptr_va']) and head[26:32] == b'\xf7\x02' + struct.pack('<I', consts['run_in_background_bit'])
        and rd(0x4d1500, 2) == b'\x0f\x84' and rel32(0x4d1500) == consts['status_ended_va'] and rd(consts['status_ended_va'], 4) == bytes.fromhex('66b80200'))
    sites[d_site] = 5
    # Patch E: the Run call is one whole instruction between `mov eax,esi` and the continuation window; five bytes in
    # one qword (one lock cmpxchg8b); the pump's head/exit and the status query's DS tail (its only direct entry).
    e_name, e_site_name, e_target_name = RUN_CALL_SITE
    e_site, e_target = consts[e_site_name], consts[e_target_name]
    checks['call_' + e_name] = (call_target(e_site) == e_target and rd(e_site - 2, 2) == b'\x8b\xc6' and e_site + 5 == consts['e_return_va']
                                and (e_site & 7) + 5 <= 8 and rd(consts['e_return_va'], 4) == bytes.fromhex('85c07464'))
    sites[e_site] = 5
    written[e_name] = {'site': hex(e_site), 'length': 5, 'original': rd(e_site, 5).hex(), 'writes_example_thunk_0x0a400000': (b'\xe8' + struct.pack('<i', 0x0a400000 - (e_site + 5))).hex(),
                       'callee': hex(e_target), 'caller_pops': 0, 'qword_offset': e_site & 7}
    pump, tail = consts['pump_va'], consts['status_ds_tail_va']
    pump_entries = []
    for i in range(len(text) - 5):
        if text[i] in (0xe8, 0xe9) and (TEXT_VA + i + 5 + struct.unpack_from('<i', text, i + 1)[0]) & 0xffffffff == pump:
            pump_entries.append(TEXT_VA + i)
    checks['pump_only_entry_is_status_ds_tail'] = pump_entries == [tail + 23] and rd(tail + 23, 1) == b'\xe9'
    checks['pump_contract'] = (rd(pump + 3, 4) == bytes.fromhex('53555657') and rd(pump + 7, 2) == bytes.fromhex('8bf0')
                               and rd(pump + 14, 3) == b'\x39\x7e' + bytes([consts['media_buffer_state']])
                               and rd(consts['pump_end_va'] + 11, 1) == b'\x5f' and rd(consts['pump_end_va'] + 28, 9) == bytes.fromhex('66b802005b83c444c3'))
    checks['pump_ds_gate'] = (rd(tail, 6) == b'\x8b\x86' + struct.pack('<I', consts['media_flags']) and rd(tail + 6, 2) == b'\xa8\x08' and rd(tail + 10, 2) == b'\xa8\x40'
                              and consts['media_ds_mask'] == 0x48 and consts['media_ds_value'] == 0x40 and rd(tail + 14, 2) == bytes.fromhex('8bc6'))
    pump_literals = data.count(struct.pack('<I', PUMP_LITERAL))
    checks['pump_no_literal_refs'] = pump_literals == 0 and consts['pump_va'] == PUMP_LITERAL
    # The save pump's import: the thunk jumps through the slot the import directory binds to zlib1.dll!gzwrite.
    iat, thunk = consts['gzwrite_iat_va'], consts['gzwrite_thunk_va']
    bound = import_slot(data, 'zlib1.dll', 'gzwrite')
    checks['gzwrite_iat_slot'] = bound == iat and rd(thunk, 6) == b'\xff\x25' + struct.pack('<I', iat)
    thunk_refs = text.count(b'\xff\x25' + struct.pack('<I', iat))
    checks['gzwrite_single_thunk'] = thunk_refs == 1
    checks['save_caller'] = consts['save_return_va'] in [b for _, b, n, _ in stops if n == 'save'] and consts['stop_movie_native_return_va'] == STOP_MOVIE_CALLERS[0][1]
    # The resume's position reader: [rec+0x24]; [m+0x8c] & 0x20 -> [m+4] vtbl +0x20 (GetTime, / 10000); else [m+0x70] vtbl +0x24 (seconds * 1000.0).
    r = consts['position_reader_va']
    factor = read_va(data, 0x565550, 8)
    checks['position_reader_contract'] = (
        rd(r, 3) == b'\x8b\x40' + bytes([consts['record_media']]) and rd(r + 6, 7) == b'\xf6\x80' + struct.pack('<I', consts['media_flags']) + bytes([consts['media_stream_clock']])
        and rd(r + 15, 3) == b'\x8b\x40' + bytes([consts['media_position']]) and rd(r + 25, 3) == b'\x8b\x41' + bytes([consts['position_vtbl_get_current']])
        and rd(r + 0x3b, 3) == b'\x8b\x40' + bytes([consts['media_stream']]) and rd(r + 0x45, 3) == b'\x8b\x41' + bytes([consts['stream_vtbl_get_time']])
        and rd(r + 0x5d, 5) == b'\x68' + struct.pack('<I', 10000) and rd(r + 0x25, 6) == b'\xdc\x0d' + struct.pack('<I', 0x565550)
        and len(factor) == 8 and struct.unpack('<d', factor)[0] == 1000.0)
    checks['combat_ids'] = (consts['combat_id_first'], consts['combat_id_last']) == (1001, 1042)
    # The save's own gzclose: 0x00404a6f calls the close wrapper (return 0x00404a74); the wrapper's gzclose call returns to
    # 0x004e9384 and goes through the thunk on the slot the import directory binds to zlib1.dll!gzclose.
    gzclose_bound = import_slot(data, 'zlib1.dll', 'gzclose')
    wrapper_call = consts['gzclose_wrapper_return_va'] - 5
    checks['save_gzclose_path'] = (
        call_target(consts['save_gzclose_call_va']) == consts['gzclose_wrapper_va'] and consts['save_gzclose_call_va'] + 5 == consts['save_gzclose_return_va']
        and call_target(wrapper_call) == consts['gzclose_thunk_va'] and rd(wrapper_call - 1, 1) == b'\x50'
        and consts['gzclose_wrapper_va'] < wrapper_call < consts['gzclose_wrapper_va'] + consts['gzclose_wrapper_length']
        and gzclose_bound == consts['gzclose_iat_va'] and rd(consts['gzclose_thunk_va'], 6) == b'\xff\x25' + struct.pack('<I', consts['gzclose_iat_va']))
    checks['run_skip_bound'] = consts['run_skip_bound_ms'] < 3000 and consts['pump_interval_ms'] < consts['run_skip_bound_ms']   # under the 5 s ring minus one 2 s sample
    # The track queue. The delete-by-id 0x004986b0: ECX = id, `cmp [esi+0x24],0` (media != 0), `cmp [esi+0x10],ecx`, the call of
    # the teardown 0x004984d0, `mov eax,1`; its only direct caller is MOV_FreeMovie's body 0x004986f0.
    fb = consts['free_by_id_va']
    checks['free_by_id_contract'] = (rd(fb, 5) == b'\xa1' + struct.pack('<I', consts['list_head_va']) and rd(fb + 16, 4) == b'\x83\x7e' + bytes([consts['record_media']]) + b'\x00'
                                     and rd(fb + 22, 3) == b'\x39\x4e' + bytes([consts['record_id']]) and call_target(fb + 39) == 0x4984d0 and rd(fb + 44, 5) == b'\xb8\x01\x00\x00\x00'
                                     and call_target(consts['free_by_id_caller_va']) == fb and fb + consts['free_by_id_length'] == 0x4986e3)
    # The completion delivery of the MOV_StopMovie body (the slot-table layout the queue repeats): owner *0x006085e4, [owner+4] != 0,
    # slot < 0x20, function at owner + 0x34 + slot * 0x18 (lea ecx,[ecx*3]; [eax+ecx*8+0x34]), cdecl (ctx, 1).
    cw = consts['completion_window_va']
    checks['completion_slot_layout'] = (rd(cw, 5) == b'\xa1' + struct.pack('<I', consts['vm_ptr_va']) and rd(cw + 7, 3) == b'\x8b\x4e' + bytes([consts['record_slot']])
                                        and rd(cw + 12, 4) == b'\x83\x78' + bytes([consts['vm_slot_guard']]) + b'\x00' and rd(cw + 18, 3) == b'\x83\xf9' + bytes([consts['slot_count']])
                                        and rd(cw + 23, 3) == b'\x8d\x14\x49' and rd(cw + 26, 4) == b'\x8b\x4c\xd0' + bytes([consts['slot_completion']])
                                        and rd(cw + 34, 3) == b'\x8b\x46' + bytes([consts['record_context']]) and rd(cw + 37, 5) == b'\x6a\x01\x50\xff\xd1'
                                        and consts['slot_stride'] == 0x18 and consts['t_stop_movie_site_va'] < cw < consts['t_stop_movie_site_va'] + 0xb9)
    # KC layout witnesses: the jump handler adds the operand to [VM+8]; the loader stores the CODE size at [VM+0x10] and the
    # buffer at [VM+8]; the build id at [VM+0x143c].
    checks['kc_layout_witnesses'] = (rd(consts['kc_jump_handler_va'] + 6, 3) == b'\x03\x7a' + bytes([consts['vm_code']])
                                     and rd(consts['kc_loader_code_store_va'], 3) == b'\x89\x43' + bytes([consts['vm_code_size']])
                                     and rd(consts['kc_loader_code_store_va'] + 26, 3) == b'\x89\x43' + bytes([consts['vm_code']])
                                     and rd(consts['kc_loader_build_store_va'], 6) == b'\x89\x83' + struct.pack('<I', consts['vm_build']))
    # The KC patch bytes: the vanilla window is `op6f; jt <continue>` with the operand little-endian in memory and the patch is
    # `jmp <continue>` plus the filler; the private key is outside 101..299 and the deferral never takes a combat, death, landing or
    # boarding id.
    cont = struct.pack('<I', consts['warp_continue_offset'])
    checks['kc_patch_bytes'] = (windows['warp_window_memory'] == bytes([consts['kc_op_fork'], consts['kc_op_jt']]) + cont
                                and windows['warp_patch_memory'] == bytes([consts['kc_op_jmp']]) + cont + bytes([consts['kc_op_filler']])
                                and consts['warp_fork_offset'] + consts['warp_window_length'] < consts['warp_continue_offset']
                                and consts['queue_key_base'] > 299 and consts['queue_stack_depth'] == consts['queue_cell_old'] + 1
                                and consts['id_death'] == 13 and consts['id_landing'] == 14 and consts['boarding_id_first'] == 8233)
    written[d_name] = {'site': hex(d_site), 'length': 5, 'original': rd(d_site, 5).hex(), 'writes_example_thunk_0x0a300000': (b'\xe8' + struct.pack('<i', 0x0a300000 - (d_site + 5))).hex(),
                       'callee': hex(d_target), 'caller_pops': 0}
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
            'pump_direct_entries': [hex(v) for v in pump_entries], 'pump_literal_refs': pump_literals, 'gzwrite_import_slot': hex(bound) if bound else None,
            'gzwrite_thunks': thunk_refs, 'gzclose_import_slot': hex(gzclose_bound) if gzclose_bound else None,
            'direct_callers': {hex(t): [hex(v) for v in c] for t, c in callers.items()}, 'literal_dword_refs': literal,
            'raw_branch_into_site_interior': {hex(s): [hex(v) for v in c] for s, c in interior.items()},
            'rejected_operand_byte_hits': {hex(k): sorted(hex(v) for v in vs) for k, vs in REJECTED_INTERIOR.items()},
            'failures': failures, 'result': 'PASS' if not failures else 'FAIL'}


_LINE = re.compile(r'^(?:\S+ )?(music_trace_stop|music_trace_play|music_trace_stop_movie|music_keep_stop|music_keep_seek|music_keep_orphan|music_keep_stop_movie|music_keep_save_pump_hook|music_keep_save_pump|music_keep_resume|music_walk_cut|music_queue_kc|music_queue|music_keep|music_trace|music_trace_cap)\b(.*)$')


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
