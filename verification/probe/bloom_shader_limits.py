"""Conservative offline SM3 budget gate for our bounded bloom programs.

Microsoft ps_3_0 instruction table (TEXLDL is TWO slots for non-cube maps):
https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-ps-3-0
Minimum slot budget:
https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-differences
This deliberately accepts only the straight-line, sampler2D subset
used by bloom, not arbitrary shaders. Runtime caps and CreatePixelShader remain
required. Slots are static cost, not GPU execution time.
"""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROFILE_SOURCE = ROOT / 'tools/analysis/inspect_motion_output_profiles.py'
INDEX_SOURCE = ROOT / 'tools/analysis/index_shaders.py'
spec = importlib.util.spec_from_file_location('bloom_shader_profile', PROFILE_SOURCE)
profile_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile_tool)

SLOT_COSTS = {name: 1 for name in
    ('abs', 'add', 'cmp', 'dp3', 'dp4', 'exp', 'frc', 'log',
     'mad', 'max', 'min', 'mov', 'mul', 'nop', 'rcp', 'rsq', 'sub')}
SLOT_COSTS.update(dcl=0, **{'def': 0}, defb=0, defi=0,
    crs=2, dp2add=2, dsx=2, dsy=2, lrp=2,
    m3x2=2, m3x3=3, m3x4=4, m4x3=3, m4x4=4, nrm=3, pow=3,
    sincos=8, texldl=2)


def check_profile(p):
    if not p.get('parsed') or p.get('version') != '0xffff0300':
        raise ValueError('Expected parsed ps_3_0 program')
    counts = p['opcode_counts']
    unknown = set(counts) - SLOT_COSTS.keys()
    if unknown:
        raise ValueError('Unqualified bloom opcode(s): ' + ', '.join(sorted(unknown)))
    if p['relative_addressing']['present'] or p['predicated_or_coissued_dwords']:
        raise ValueError('Unqualified relative/predicated/coissued bloom code')
    if not p['control_flow_balanced'] or p['control_flow_max_depth'] != 0:
        raise ValueError('Only straight-line bloom programs are qualified')
    if any(s['texture_type'] != 2 for s in p['declared_samplers']):
        raise ValueError('Only sampler2D texture costs are qualified')
    temporaries = p['temporary_registers']
    samplers = p['sampler_registers']
    constants = sorted(set(p['constant_registers_direct']) | set(p['defined_constant_registers']))
    slots = sum(SLOT_COSTS[name] * count for name, count in counts.items())
    if slots > 512 or max(temporaries, default=-1) >= 32 or max(samplers, default=-1) >= 16 or max(constants, default=-1) >= 224:
        raise ValueError(f'Exceeds minimum SM3 budget: slots={slots}/512, '
                         f'max_temp={max(temporaries, default=-1)}/31, '
                         f'max_sampler={max(samplers, default=-1)}/15, '
                         f'max_constant={max(constants, default=-1)}/223')
    return dict(passed=True, instruction_slots=slots, slot_limit=512,
        slot_headroom=512-slots, executable_instructions=p['executable_instruction_count'],
        opcode_counts=counts, temporary_registers=temporaries, sampler_registers=samplers,
        constant_registers=constants, max_branch_depth=p['control_flow_max_depth'],
        texture_kind='2D', texldl_slots=2, runtime_caps_verified=False)


def check_bytecode(code):
    return check_profile(profile_tool.profile(code, 'authored-bloom', 'ps', '3_0'))
