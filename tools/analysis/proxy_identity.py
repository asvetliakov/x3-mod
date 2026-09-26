"""Parser for the session log's identity header.

The proxy writes two lines at attach, before any derived *_mode line
(docs/architecture/platform-portability.md, "Session identity"):

    proxy_identity sha256=<64 hex|unavailable> bytes=<n> path=<dll path> \
manifest_sha256=<64 hex|none> source_commit=<commit[-dirty]|unknown> attach_us=<n> \
exe_sha256=<64 hex|unavailable> exe_bytes=<n> exe_hash_us=<n> exe_laa=<0|1> exe_max_app=<8 hex>
    proxy_options [NAME=VALUE ...]

Fields are space-separated key=value tokens; the proxy replaces any character
outside printable ASCII in a path or option value with '_', so no field can
split. `proxy_options` carries only X3M_* variables and may be empty; since the settings file (2026-09-26,
docs/architecture/config-file.md) each value ends in its source, `@env`, `@file` or `@default` (the settings the
DLL resolved from x3m.ini or its built-in defaults are listed too): parse_options strips it, parse_option_sources
returns {name: (value, source)} (source None on an older line). The
exe_* fields (game executable provenance, never a gate; docs/reverse-engineering/
executable-identity.md) and attach_us are optional: older logs lack them.
attach_us excludes the executable hash, whose cost is exe_hash_us.
"""
from __future__ import annotations

from collections import namedtuple
import re

HEX64 = re.compile(r'\A[0-9a-f]{64}\Z')
COMMIT = re.compile(r'\A(?:unknown|[0-9a-f]{7,40}(?:-dirty)?)\Z')
IDENTITY_FIELDS = ('sha256', 'bytes', 'path', 'manifest_sha256', 'source_commit')


class ProxyIdentityError(ValueError):
    """The line claims to be an identity/options line but does not parse."""


def _count(fields, name):
    """A non-negative decimal field; int() alone would accept '+1', ' 1' or '1_0'."""
    if not re.fullmatch(r'\d+', fields[name]):
        raise ProxyIdentityError(f'proxy_identity {name} not a non-negative integer: {fields[name]!r}')
    return int(fields[name])


def parse_identity(line):
    """Parse one `proxy_identity` line into a dict, or return None for any
    other line. Raises ProxyIdentityError on a malformed identity line."""
    tokens = line.strip().split(' ')
    if not tokens or tokens[0] != 'proxy_identity':
        return None
    fields = {}
    for token in tokens[1:]:
        if not token:
            continue
        name, separator, value = token.partition('=')
        if not separator or name in fields:
            raise ProxyIdentityError(f'malformed proxy_identity token: {token!r}')
        fields[name] = value
    missing = [name for name in IDENTITY_FIELDS if name not in fields]
    if missing:
        raise ProxyIdentityError(f'proxy_identity missing fields: {", ".join(missing)}')
    if fields['sha256'] != 'unavailable' and not HEX64.match(fields['sha256']):
        raise ProxyIdentityError(f'proxy_identity sha256 not a digest: {fields["sha256"]!r}')
    if fields['manifest_sha256'] != 'none' and not HEX64.match(fields['manifest_sha256']):
        raise ProxyIdentityError(f'proxy_identity manifest_sha256 not a digest: {fields["manifest_sha256"]!r}')
    if not COMMIT.match(fields['source_commit']):
        raise ProxyIdentityError(f'proxy_identity source_commit malformed: {fields["source_commit"]!r}')
    fields['bytes'] = _count(fields, 'bytes')
    for name in ('attach_us', 'exe_bytes', 'exe_hash_us'):  # optional: older logs lack them
        if name in fields:
            fields[name] = _count(fields, name)
    if 'exe_sha256' in fields and fields['exe_sha256'] != 'unavailable' and not HEX64.match(fields['exe_sha256']):
        raise ProxyIdentityError(f'proxy_identity exe_sha256 not a digest: {fields["exe_sha256"]!r}')
    if 'exe_laa' in fields:
        if fields['exe_laa'] not in ('0', '1'):
            raise ProxyIdentityError(f'proxy_identity exe_laa not 0/1: {fields["exe_laa"]!r}')
        fields['exe_laa'] = fields['exe_laa'] == '1'
    if 'exe_max_app' in fields:
        if not re.fullmatch(r'[0-9a-f]{8}', fields['exe_max_app']):
            raise ProxyIdentityError(f'proxy_identity exe_max_app not 8 hex digits: {fields["exe_max_app"]!r}')
        fields['exe_max_app'] = int(fields['exe_max_app'], 16)
    fields['dirty'] = fields['source_commit'].endswith('-dirty')
    return fields


SOURCES = ('env', 'file', 'default', 'bare')


def split_source(value):
    """'1@env' -> ('1', 'env'); a value without a known source suffix -> (value, None)."""
    head, separator, tail = value.rpartition('@')
    return (head, tail) if separator and tail in SOURCES else (value, None)


def parse_option_sources(line):
    """Parse one `proxy_options` line into {name: (value, source)}, or return None for any other line."""
    options = parse_options(line, keep_source=True)
    return None if options is None else {name: split_source(value) for name, value in options.items()}


def parse_options(line, keep_source=False):
    """Parse one `proxy_options` line into {name: value} (the source suffix removed unless keep_source), or return
    None for any other line. An options line with no variables yields {}."""
    stripped = line.strip()
    if stripped != 'proxy_options' and not stripped.startswith('proxy_options '):
        return None
    options = {}
    for token in stripped.split(' ')[1:]:
        if not token:
            continue
        name, separator, value = token.partition('=')
        if not separator or not name.upper().startswith('X3M_'):  # Win32 names are case-insensitive
            raise ProxyIdentityError(f'malformed proxy_options token: {token!r}')
        if name in options:
            raise ProxyIdentityError(f'duplicate proxy_options name: {name}')
        options[name] = value if keep_source else split_source(value)[0]
    return options


ScanResult = namedtuple('ScanResult', 'identity options malformed')


def scan(lines):
    """Return ScanResult(identity, options, malformed) for an iterable of log
    lines. A field is None when the log carries no such line (a build older than
    the header) or when the line it found does not parse; `malformed` then holds
    the offending lines. Never raises on log content."""
    identity = options = None
    malformed = []
    for line in lines:
        if identity is None:
            try:
                identity = parse_identity(line)
            except ProxyIdentityError:
                malformed.append(line.rstrip('\n'))
                identity = None
                continue
            if identity is not None:
                continue
        if options is None:
            try:
                options = parse_options(line)
            except ProxyIdentityError:
                malformed.append(line.rstrip('\n'))
                options = None
                continue
        if identity is not None and options is not None:
            break
    return ScanResult(identity, options, malformed)


def scan_log(path, encoding='utf-8', errors='replace'):
    """scan() over a session log file."""
    with open(path, encoding=encoding, errors=errors) as handle:
        return scan(handle)
