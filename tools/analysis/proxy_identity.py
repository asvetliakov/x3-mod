"""Parser for the session log's identity header.

The proxy writes two lines at attach, before any derived *_mode line
(docs/architecture/platform-portability.md, "Session identity"):

    proxy_identity sha256=<64 hex|unavailable> bytes=<n> path=<dll path> \
manifest_sha256=<64 hex|none> source_commit=<commit[-dirty]|unknown> attach_us=<n>
    proxy_options [NAME=VALUE ...]

Fields are space-separated key=value tokens; the proxy replaces any character
outside printable ASCII in a path or option value with '_', so no field can
split. `proxy_options` carries only X3M_* variables and may be empty.
"""
from __future__ import annotations

from collections import namedtuple
import re

HEX64 = re.compile(r'\A[0-9a-f]{64}\Z')
COMMIT = re.compile(r'\A(?:unknown|[0-9a-f]{7,40}(?:-dirty)?)\Z')
IDENTITY_FIELDS = ('sha256', 'bytes', 'path', 'manifest_sha256', 'source_commit')


class ProxyIdentityError(ValueError):
    """The line claims to be an identity/options line but does not parse."""


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
    try:
        fields['bytes'] = int(fields['bytes'])
    except ValueError:
        raise ProxyIdentityError(f'proxy_identity bytes not an integer: {fields["bytes"]!r}') from None
    if fields['bytes'] < 0:
        raise ProxyIdentityError('proxy_identity bytes negative')
    if 'attach_us' in fields:  # optional: the microseconds the header itself cost at attach
        try:
            fields['attach_us'] = int(fields['attach_us'])
        except ValueError:
            raise ProxyIdentityError(f'proxy_identity attach_us not an integer: {fields["attach_us"]!r}') from None
        if fields['attach_us'] < 0:
            raise ProxyIdentityError('proxy_identity attach_us negative')
    fields['dirty'] = fields['source_commit'].endswith('-dirty')
    return fields


def parse_options(line):
    """Parse one `proxy_options` line into {name: value}, or return None for
    any other line. An options line with no variables yields {}."""
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
        options[name] = value
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
