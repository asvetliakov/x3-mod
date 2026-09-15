#!/usr/bin/env python3
"""Strict no-FP/no-hidden-allocation walk of linked loading Span entry points."""
import json
import re
import sys
from pathlib import Path
from check_no_x87 import disassemble, parse, walk, LIGHT_NAMESPACE


def audit(path):
    functions=disassemble(path)
    roots=[]
    for method in ('begin','before_call','finish'):
        prefix=f'{LIGHT_NAMESPACE}4Span{len(method)}{method}E'
        # const before_call has an extra K after _ZN in its mangled namespace.
        matches=[name for name in functions if name.startswith(prefix) or name.startswith(prefix.replace('__ZN','__ZNK',1))]
        if len(matches)!=1:
            raise ValueError(f'{method}: expected one linked entry, found {matches}')
        roots.extend(matches)
    reached=walk(functions,roots)
    faults={}
    for name in reached:
        bad=[]
        if re.search(r'(malloc|calloc|realloc|printf|__emutls|Zn[aw])',name):
            bad.append('forbidden allocation/formatting helper')
        for line in functions[name]:
            mnemonic,operands=parse(line)
            if mnemonic and (mnemonic.startswith('f') or re.search(r'%(?:xmm|mm)\d',operands)):
                bad.append(line.strip())
        if bad:faults[name]=bad
    return dict(result='FAIL' if faults else 'PASS',binary=str(path),roots=roots,
                reachable_functions=len(reached),violations=faults,
                limit='Win32 imports are documented ABI boundaries; no native Windows execution claimed.')


if __name__=='__main__':
    result=audit(Path(sys.argv[1]));print(json.dumps(result,indent=2));raise SystemExit(bool(result['violations']))
