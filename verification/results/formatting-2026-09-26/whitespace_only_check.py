"""Whitespace-only proof for the clang-format pass of 2026-09-26.

For every C/C++ file changed between BASE and the working tree (default BASE: HEAD~1, i.e. run on the formatting
commit): (a) the code with comments removed and whitespace outside string/char literals removed (macro line-continuation
backslashes ignored) is identical; (b) the sequence of comment words is identical once the added
`// clang-format off` / `// clang-format on` guards are dropped. Exit 1 when any file differs.

    /usr/bin/python3 verification/results/formatting-2026-09-26/whitespace_only_check.py [BASE]

Result at the formatting commit: 521 changed C/C++ files, 0 code differences, 0 comment-word differences.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
BASE = sys.argv[1] if len(sys.argv) > 1 else 'HEAD~1'
LITERAL = r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|R"([^(]*)\(.*?\)\1"'
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|' + LITERAL, re.S)


def split(text):
    code, comments, pos = [], [], 0
    for m in TOKEN.finditer(text):
        code.append(text[pos:m.start()])
        s = m.group(0)
        if s.startswith(('//', '/*')):
            comments.append(s)
            code.append(' ')
        else:
            code.append(s)
        pos = m.end()
    code.append(text[pos:])
    return ''.join(code), comments


def squash(code):
    out, pos = [], 0
    for m in re.finditer(LITERAL, code, re.S):
        out.append(re.sub(r'\s+', '', code[pos:m.start()]))
        out.append(m.group(0))
        pos = m.end()
    out.append(re.sub(r'\s+', '', code[pos:]))
    return ''.join(out).replace('\\', '')


def words(comments):
    result = []
    for c in comments:
        body = c[2:] if c.startswith('//') else c[2:-2]
        if not re.fullmatch(r'\s*clang-format o(n|ff)\s*', body):
            result.extend(body.split())
    return result


def main():
    names = subprocess.run(['git', 'diff', '--name-only', BASE, '--', '*.c', '*.cc', '*.cpp', '*.h', '*.hpp', '*.inl'],
                           cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    code_diff, comment_diff = [], []
    for name in names:
        old = subprocess.run(['git', 'show', f'{BASE}:{name}'], cwd=ROOT, capture_output=True, text=True,
                             errors='surrogateescape', check=True).stdout
        new = (ROOT / name).read_text(errors='surrogateescape')
        old_code, old_comments = split(old)
        new_code, new_comments = split(new)
        if squash(old_code) != squash(new_code):
            code_diff.append(name)
        if words(old_comments) != words(new_comments):
            comment_diff.append(name)
    print(f'changed={len(names)} code_differs={len(code_diff)} comment_words_differ={len(comment_diff)}')
    for name in code_diff + comment_diff:
        print('DIFF', name)
    return 1 if code_diff or comment_diff else 0


if __name__ == '__main__':
    sys.exit(main())
