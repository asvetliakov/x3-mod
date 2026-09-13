"""Small float64 execution oracle for the authored XT pixel-shader opcode set.

This intentionally interprets only the finite, nonpredicated ps_3_0 programs
used by the fourteen XT material pairs and their current transformations.  It
does not model partial precision, texture filtering, rasterization, exceptional
zero/nonfinite behavior, or GPU instruction rounding.  Texture samples are
provided by the caller, so the interpreter makes no texture-format claim.
"""

from dataclasses import dataclass, field
import math
import struct
from typing import Callable, Mapping, MutableMapping, Sequence, Tuple, Union

from tools.analysis import inspect_motion_output_profiles as motion


Vec4 = Tuple[float, float, float, float]
Sampler = Union[Sequence[float], Callable[[Vec4], Sequence[float]]]
LANES = "xyzw"
SUPPORTED = {
    "mov", "add", "mad", "mul", "rcp", "rsq", "dp3", "dp4", "min",
    "max", "lrp", "pow", "abs", "nrm", "cmp", "dp2add", "texld",
    "if", "ifc", "else", "endif", "dcl", "def",
}


@dataclass
class State:
    """Caller-owned shader inputs and the registers produced by execution."""

    registers: MutableMapping[str, Sequence[float]] = field(default_factory=dict)
    booleans: MutableMapping[int, bool] = field(default_factory=dict)
    samplers: Mapping[int, Sampler] = field(default_factory=dict)


def _vec4(value: Sequence[float], name: str) -> Vec4:
    values = tuple(float(component) for component in value)
    if len(values) != 4:
        raise ValueError(f"{name} must have four components")
    if not all(math.isfinite(component) for component in values):
        raise ValueError(f"{name} must be finite")
    return values  # type: ignore[return-value]


def _raw_operands(item):
    """Return destination token and source tokens for this nonrelative subset."""
    opcode = motion.OPCODES.get(item["opcode"])
    words = list(item["words"])
    if opcode in {"dcl", "def"}:
        return None, []
    if any(word & motion.RELATIVE for word in words):
        raise ValueError("relative addressing is outside the XT pixel subset")
    if opcode in motion.FLOW_OPCODES:
        return None, words
    if not words:
        return None, []
    return words[0], words[1:]


def _source(state: State, token: int) -> Vec4:
    kind, number = motion.register_of(token)
    if kind == motion.BOOLEAN_REGISTER_TYPE:
        value = (1.0 if state.booleans.get(number, False) else 0.0,) * 4
    else:
        name = motion.name_of(kind, number)
        if name not in state.registers:
            raise ValueError(f"uninitialized shader register {name}")
        value = _vec4(state.registers[name], name)
    swizzle = motion.swizzle_of(token)
    result = tuple(value[LANES.index(lane)] for lane in swizzle)
    modifier = (token >> 24) & 15
    if modifier == 1:  # D3DSPSM_NEG
        result = tuple(-component for component in result)
    elif modifier == 11:  # D3DSPSM_ABS
        result = tuple(abs(component) for component in result)
    elif modifier == 12:  # D3DSPSM_ABSNEG
        result = tuple(-abs(component) for component in result)
    elif modifier:
        raise ValueError(f"unsupported source modifier {modifier}")
    return result  # type: ignore[return-value]


def _sample(state: State, sampler_token: int, coordinate: Vec4) -> Vec4:
    kind, number = motion.register_of(sampler_token)
    if kind != 10 or number not in state.samplers:
        raise ValueError(f"unbound sampler s{number}")
    source = state.samplers[number]
    value = source(coordinate) if callable(source) else source
    return _vec4(value, f"sample s{number}")


def _comparison(control: int, left: float, right: float) -> bool:
    functions = {
        1: lambda: left > right,
        2: lambda: left == right,
        3: lambda: left >= right,
        4: lambda: left < right,
        5: lambda: left != right,
        6: lambda: left <= right,
    }
    if control not in functions:
        raise ValueError(f"unsupported IFC comparison {control}")
    return functions[control]()


def _result(opcode: str, sources: Sequence[Vec4]) -> Vec4:
    a = sources[0] if sources else (0.0,) * 4
    if opcode == "mov":
        return a
    if opcode == "add":
        return tuple(x + y for x, y in zip(a, sources[1]))  # type: ignore[return-value]
    if opcode == "mul":
        return tuple(x * y for x, y in zip(a, sources[1]))  # type: ignore[return-value]
    if opcode == "mad":
        return tuple(x * y + z for x, y, z in zip(a, sources[1], sources[2]))  # type: ignore[return-value]
    if opcode == "min":
        return tuple(min(x, y) for x, y in zip(a, sources[1]))  # type: ignore[return-value]
    if opcode == "max":
        return tuple(max(x, y) for x, y in zip(a, sources[1]))  # type: ignore[return-value]
    if opcode == "abs":
        return tuple(abs(x) for x in a)  # type: ignore[return-value]
    if opcode == "lrp":
        return tuple(x * y + (1.0 - x) * z
                     for x, y, z in zip(a, sources[1], sources[2]))  # type: ignore[return-value]
    if opcode == "cmp":
        return tuple(y if x >= 0.0 else z
                     for x, y, z in zip(a, sources[1], sources[2]))  # type: ignore[return-value]
    if opcode in {"dp3", "dp4"}:
        width = 3 if opcode == "dp3" else 4
        value = sum(a[index] * sources[1][index] for index in range(width))
        return (value,) * 4
    if opcode == "dp2add":
        value = a[0] * sources[1][0] + a[1] * sources[1][1] + sources[2][0]
        return (value,) * 4
    if opcode == "rcp":
        return (1.0 / a[0],) * 4
    if opcode == "rsq":
        return (1.0 / math.sqrt(a[0]),) * 4
    if opcode == "pow":
        return (math.pow(a[0], sources[1][0]),) * 4
    if opcode == "nrm":
        length = math.sqrt(sum(component * component for component in a[:3]))
        result = tuple(component / length for component in a[:3]) + (a[3],)
        return result  # type: ignore[return-value]
    raise ValueError(f"unsupported arithmetic opcode {opcode}")


def execute(code: bytes, state: State, *, stop_after_oc0: bool = True) -> State:
    """Execute one current XT PS and return ``state`` with output registers.

    ``stop_after_oc0`` excludes the independently-qualified temporal/depth
    epilogue once all four primary-color lanes have been produced.  All material
    math precedes that boundary in the current authored variants.
    """
    words, items, _ = motion.instructions(code)
    if words[0] != 0xffff0300:
        raise ValueError("only ps_3_0 is supported")
    state.registers = {name: list(_vec4(value, name))
                       for name, value in state.registers.items()}
    active = True
    primary_lanes = set()
    branches = []
    for item in items:
        opcode = motion.OPCODES.get(item["opcode"])
        if opcode not in SUPPORTED:
            raise ValueError(f"opcode {opcode!r} is outside the XT pixel subset")
        destination_token, source_tokens = _raw_operands(item)

        if opcode == "if" or opcode == "ifc":
            condition = False
            if active:
                sources = [_source(state, token) for token in source_tokens]
                condition = (sources[0][0] != 0.0 if opcode == "if" else
                             _comparison((item["token"] >> 16) & 7,
                                         sources[0][0], sources[1][0]))
            branches.append((active, condition))
            active = active and condition
            continue
        if opcode == "else":
            if not branches:
                raise ValueError("ELSE without IF")
            parent, condition = branches[-1]
            active = parent and not condition
            continue
        if opcode == "endif":
            if not branches:
                raise ValueError("ENDIF without IF")
            parent, _ = branches.pop()
            active = parent
            continue
        if not active or opcode == "dcl":
            continue
        if opcode == "def":
            if len(item["words"]) != 5:
                raise ValueError("malformed DEF")
            kind, number = motion.register_of(item["words"][0])
            if kind != 2:
                raise ValueError("non-float DEF is outside the XT subset")
            state.registers[motion.name_of(kind, number)] = list(struct.unpack(
                "<4f", struct.pack("<4I", *item["words"][1:])))
            continue
        if destination_token is None:
            raise ValueError(f"missing destination for {opcode}")
        destination_kind, destination_number = motion.register_of(destination_token)
        destination_name = motion.name_of(destination_kind, destination_number)
        if opcode == "texld":
            if len(source_tokens) != 2:
                raise ValueError("malformed TEXLD")
            value = _sample(state, source_tokens[1], _source(state, source_tokens[0]))
        else:
            sources = [_source(state, token) for token in source_tokens]
            value = _result(opcode, sources)
        modifier = (destination_token >> 20) & 15
        unknown_modifiers = modifier & ~3  # saturation + ignored partial precision
        if unknown_modifiers:
            raise ValueError(f"unsupported destination modifier {modifier}")
        if modifier & 1:
            value = tuple(min(max(component, 0.0), 1.0) for component in value)
        target = list(state.registers.get(destination_name, (0.0,) * 4))
        for lane in motion.mask_of(destination_token):
            index = LANES.index(lane)
            target[index] = value[index]
        state.registers[destination_name] = target
        if destination_name == "oC0":
            primary_lanes.update(motion.mask_of(destination_token))
            if stop_after_oc0 and primary_lanes == set(LANES):
                break
    if branches:
        raise ValueError("unterminated IF")
    return state
