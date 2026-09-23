"""Review item C: bodies refused as ambiguous_body_ext (both .pbb and .pbd exist; bob1.resolve_body
raises FormatError) and how many of them would otherwise be eligible.
Usage: python3 ambiguous_ext.py [census.txt]"""
import re
import sys

rows = open(sys.argv[1] if len(sys.argv) > 1 else 'census.txt').read().splitlines()
amb = [r for r in rows if re.search(r' refuse=\S*ambiguous_body_ext', r)]
only = [r for r in amb if re.search(r' refuse=ambiguous_body_ext filter=-( |$)', r)]
print(f'ambiguous_body_ext {len(amb)}; otherwise eligible (no other refusal, no filter) {len(only)}')
for r in only:
    print('  ' + r.split()[0])
