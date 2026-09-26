"""Whitespace-insensitive view of C/C++ source text for the host tests that pin production spelling.

The tree is clang-formatted (.clang-format; README "Formatting"), so a fragment a test expects may be wrapped or
spaced differently from the way it was written. `source_text(path)` returns the file text as a `SourceText`: a `str`
holding the raw text whose `in`, `count`, `find`/`index`, `rfind`/`rindex`, `split` and `replace` compare in a canonical form
where whitespace between two word characters is one space and every other whitespace run is dropped. The same
canonicalisation applies to the fragment, so `a = b(c, d)` matches `a=b(c,d)` and a statement wrapped over two lines,
but never a different token sequence. A fragment that starts or ends with whitespace next to a word character still
requires a word boundary there, and a fragment holding a newline matches the raw layout first. Positions returned by `find`/`index` are raw-text positions, and slices stay
`SourceText`, so slicing between two found fragments works as before; `end(fragment, start)` is the raw position just
past a match, for `index(fragment) + len(fragment)`. Any other file type is returned as a plain str.
"""
import pathlib
import re

C_SUFFIXES = {'.c', '.cc', '.cpp', '.h', '.hpp', '.inl'}
_WS = re.compile(r'\s+')


def _is_word(ch):
    return ch.isalnum() or ch == '_'


def canonical(text):
    """Canonical form of a fragment: whitespace runs dropped, or one space between two word characters."""
    out, pos = [], 0
    for m in _WS.finditer(text):
        out.append(text[pos:m.start()])
        if m.start() > 0 and m.end() < len(text) and _is_word(text[m.start() - 1]) and _is_word(text[m.end()]):
            out.append(' ')
        pos = m.end()
    out.append(text[pos:])
    return ''.join(out)


class SourceText(str):
    def _index(self):
        cached = self.__dict__.get('_canon')
        if cached is None:
            raw = str(self)
            chars, where, pos = [], [], 0
            for m in _WS.finditer(raw):
                chars.append(raw[pos:m.start()])
                where.extend(range(pos, m.start()))
                if m.start() > 0 and m.end() < len(raw) and _is_word(raw[m.start() - 1]) and _is_word(raw[m.end()]):
                    chars.append(' ')
                    where.append(m.start())
                pos = m.end()
            chars.append(raw[pos:])
            where.extend(range(pos, len(raw)))
            cached = (''.join(chars), where)
            self.__dict__['_canon'] = cached
        return cached

    def _matches(self, fragment):
        """Raw (start, end) spans of every non-overlapping canonical match of the fragment. A fragment holding a
        newline is layout-sensitive ('\\n}' ends a column-0 function): its raw matches win when there are any."""
        if '\n' in fragment:
            raw, spans, at = str(self), [], 0
            while (i := raw.find(fragment, at)) >= 0:
                spans.append((i, i + len(fragment)))
                at = i + max(1, len(fragment))
            if spans:
                return spans
        canon, where = self._index()
        needle = canonical(fragment)
        if not needle:
            return None
        left = fragment[:1].isspace() and _is_word(needle[0])
        right = fragment[-1:].isspace() and _is_word(needle[-1])
        spans, at = [], 0
        while True:
            i = canon.find(needle, at)
            if i < 0:
                return spans
            j = i + len(needle)
            if (left and i > 0 and _is_word(canon[i - 1])) or (right and j < len(canon) and _is_word(canon[j])):
                at = i + 1
                continue
            spans.append((where[i], where[j - 1] + 1))
            at = j

    def __contains__(self, fragment):
        if not isinstance(fragment, str):
            return str.__contains__(self, fragment)
        spans = self._matches(fragment)
        return str.__contains__(self, fragment) if spans is None else bool(spans)

    def count(self, fragment, *args):
        spans = self._matches(fragment) if isinstance(fragment, str) and not args else None
        return str.count(self, fragment, *args) if spans is None else len(spans)

    def _bounded(self, fragment, start, end):
        spans = self._matches(fragment) if isinstance(fragment, str) else None
        if spans is None:
            return None
        lo = 0 if start is None else (start if start >= 0 else max(0, len(self) + start))
        hi = len(self) if end is None else (end if end >= 0 else max(0, len(self) + end))
        return [s for s in spans if s[0] >= lo and s[1] <= hi]

    def find(self, fragment, start=None, end=None):
        spans = self._bounded(fragment, start, end)
        if spans is None:
            return str.find(self, fragment, *[a for a in (start, end) if a is not None])
        return spans[0][0] if spans else -1

    def rfind(self, fragment, start=None, end=None):
        spans = self._bounded(fragment, start, end)
        if spans is None:
            return str.rfind(self, fragment, *[a for a in (start, end) if a is not None])
        return spans[-1][0] if spans else -1

    def index(self, fragment, start=None, end=None):
        i = self.find(fragment, start, end)
        if i < 0:
            raise ValueError('substring not found')
        return i

    def rindex(self, fragment, start=None, end=None):
        i = self.rfind(fragment, start, end)
        if i < 0:
            raise ValueError('substring not found')
        return i

    def end(self, fragment, start=None, end=None):
        """Raw position just past the first match of the fragment (index(fragment) + its length in the raw text)."""
        spans = self._bounded(fragment, start, end)
        if not spans:
            raise ValueError('substring not found')
        return spans[0][1]

    def split(self, sep=None, maxsplit=-1):
        spans = self._matches(sep) if isinstance(sep, str) else None
        if spans is None:
            return str.split(self, sep, maxsplit)
        if maxsplit >= 0:
            spans = spans[:maxsplit]
        parts, pos = [], 0
        for a, b in spans:
            parts.append(SourceText(str(self)[pos:a]))
            pos = b
        parts.append(SourceText(str(self)[pos:]))
        return parts

    def partition(self, sep):
        parts = self.split(sep, 1)
        if len(parts) == 1:
            return parts[0], SourceText(''), SourceText('')
        a, b = self._matches(sep)[0]
        return parts[0], SourceText(str(self)[a:b]), parts[1]

    def replace(self, old, new, count=-1):
        spans = self._matches(old) if isinstance(old, str) and isinstance(new, str) else None
        if spans is None:
            return SourceText(str.replace(self, old, new, count))
        if count >= 0:
            spans = spans[:count]
        out, pos = [], 0
        for a, b in spans:
            out.append(str(self)[pos:a])
            out.append(new)
            pos = b
        out.append(str(self)[pos:])
        return SourceText(''.join(out))

    def __getitem__(self, key):
        item = str.__getitem__(self, key)
        return SourceText(item) if isinstance(key, slice) else item


def source_text(path, *args, **kwargs):
    """The file's text; C/C++ sources as a whitespace-insensitive SourceText (module docstring)."""
    path = pathlib.Path(path)
    text = path.read_text(*args, **kwargs)
    return SourceText(text) if path.suffix in C_SUFFIXES else text
