#!/usr/bin/env python3
"""Stage one explicit fixture-only official/strict LAV graph assembly."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess

from prepare_lav_fixture import (BINARIES, SOURCE_CLSID, VIDEO_CLSID,
    SYSTEM, verify_record as verify_official_record)
from owned_lav_provider import (MODULES as STRICT_MODULES, inspect_pe,
    verify_strict_record)


OFFICIAL_MODULES = tuple(name for name in BINARIES if name not in STRICT_MODULES)
AUXILIARY = ('provider.manifest', 'LAVFilters.Dependencies.manifest', 'COPYING',
             'README.md', 'include/LAVVideoSettings.h',
             'include/LAVSplitterSettings.h')
FILES = (*BINARIES, *AUXILIARY)
ORIGINS = {name: ('strict' if name in STRICT_MODULES else 'official') for name in FILES}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def _outside(paths, forbidden_roots):
    roots = tuple(Path(root).resolve() for root in forbidden_roots)
    require(all(not any(path.resolve().is_relative_to(root) for root in roots)
                for path in paths), 'graph provider path inside forbidden root')


def _source_path(name, official_path, strict_path):
    root = strict_path.parent if ORIGINS[name] == 'strict' else official_path.parent
    target = (root/name).resolve()
    require(target.is_relative_to(root.resolve()) and
            '..' not in PurePosixPath(name).parts, 'graph provider source traversal')
    return target


def inspect_import_symbols(path, objdump='i686-w64-mingw32-objdump'):
    try:
        output=subprocess.check_output([objdump,'-p',str(path)],text=True,
            stderr=subprocess.STDOUT,timeout=30)
    except (OSError,subprocess.CalledProcessError,subprocess.TimeoutExpired) as error:
        raise ValueError('graph import-symbol inspection failed: '+path.name) from error
    section=output.partition('The Import Tables')[2].partition('The Export Tables')[0]
    result={};current=None
    for line in section.splitlines():
        match=re.match(r'^\s*DLL Name:\s*(\S+)\s*$',line)
        if match:
            current=match.group(1).lower();result[current]=[];continue
        if current is None:continue
        named=re.match(r'^\s*[0-9a-fA-F]+\s+<none>\s+[0-9a-fA-F]+\s+(\S+)\s*$',line)
        ordinal=re.match(r'^\s*[0-9a-fA-F]+\s+(\d+)\s+<none>\s+<none>\s*$',line)
        if named:result[current].append(named.group(1))
        elif ordinal:result[current].append('#'+ordinal.group(1))
    require(result and all(values for values in result.values()),
            'missing graph import-symbol evidence: '+path.name)
    return {name:sorted(values) for name,values in sorted(result.items())}


def _binary_records(root, objdump):
    observed={name:inspect_pe(root/name,objdump) for name in BINARIES}
    symbol_imports={name:inspect_import_symbols(root/name,objdump) for name in BINARIES}
    allowed=SYSTEM | {item.lower() for item in BINARIES}
    exports={name.lower():set(observed[name]['exports']) for name in BINARIES}
    records={}
    for name in BINARIES:
        require(all(item in allowed for item in observed[name]['imports']) and
                set(symbol_imports[name])==set(observed[name]['imports']),
                'graph provider import closure differs: '+name)
        selected={dependency:symbols for dependency,symbols in symbol_imports[name].items()
                  if dependency in exports}
        for dependency,symbols in selected.items():
            require(all(not symbol.startswith('#') and symbol in exports[dependency]
                        for symbol in symbols),
                    'unresolved selected graph import: '+name+' -> '+dependency)
        encoded=json.dumps(selected,sort_keys=True,separators=(',',':')).encode()
        path=root/name
        records[name]=dict(sha256=digest(path),bytes=path.stat().st_size,machine='I386',
            imports=observed[name]['imports'],assembly_import_count=sum(map(len,selected.values())),
            assembly_imports_sha256=hashlib.sha256(encoded).hexdigest(),origin=ORIGINS[name])
    return records


def prepare(output, official_record, strict_record, patch_review,
            forbidden_roots=(), objdump='i686-w64-mingw32-objdump'):
    output=output.resolve();official_record=official_record.resolve()
    strict_record=strict_record.resolve();patch_review=patch_review.resolve()
    require(not output.exists(), 'output must be a fresh private directory')
    _outside((output, official_record, strict_record, patch_review), forbidden_roots)
    require(not output.is_relative_to(official_record.parent) and
            not output.is_relative_to(strict_record.parent),
            'output must not be nested under an input provider')
    official=verify_official_record(official_record)
    strict=verify_strict_record(strict_record,patch_review,forbidden_roots,objdump)

    sources={name:_source_path(name,official_record,strict_record) for name in FILES}
    watched={'official_record':official_record,'strict_record':strict_record,
             'patch_review':patch_review,**{'source_'+name:path for name,path in sources.items()}}
    before={name:digest(path) for name,path in watched.items()}
    output.mkdir(parents=True)
    for name,source in sources.items():
        target=output/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(source,target)

    files={name:dict(sha256=digest(output/name),bytes=(output/name).stat().st_size,
                     origin=ORIGINS[name]) for name in FILES}
    binaries=_binary_records(output,objdump)
    expected_paths={name:str((output/name).resolve()) for name in BINARIES}
    record=dict(schema=1,provider='LAVFilters-mixed-graph-fixture',
        role='reviewed-strict-graph-assembly',architecture='x86',private_root=str(output),
        inputs=dict(
            official=dict(record_path=str(official_record),record_sha256=digest(official_record)),
            strict=dict(record_path=str(strict_record),record_sha256=digest(strict_record)),
            patch_review=dict(path=str(patch_review),sha256=digest(patch_review))),
        files=files,binaries=binaries,expected_module_paths=expected_paths,
        classes=dict(source=SOURCE_CLSID,video=VIDEO_CLSID),threading_model='Both',
        input_files_unchanged=all(digest(path)==before[name] for name,path in watched.items()),
        fixture_only=True,distribution_qualified=False,game_playback_proven=False,
        native_windows_qualified=False,
        limitation='Mixed diagnostic assembly; runtime must prove all nine exact loaded paths.')
    require(record['input_files_unchanged'], 'graph provider input changed while staging')
    (output/'graph-provider.json').write_text(json.dumps(record,indent=2)+'\n')
    verify_record(output/'graph-provider.json',forbidden_roots,objdump)
    return record


def verify_record(path, forbidden_roots=(), objdump='i686-w64-mingw32-objdump'):
    path=path.resolve();data=json.loads(path.read_text())
    require(data.get('schema') == 1 and
            data.get('provider') == 'LAVFilters-mixed-graph-fixture' and
            data.get('role') == 'reviewed-strict-graph-assembly' and
            data.get('architecture') == 'x86' and data.get('fixture_only') is True and
            data.get('distribution_qualified') is False and
            data.get('game_playback_proven') is False and
            data.get('native_windows_qualified') is False,
            'unrecognized graph provider record')
    root=Path(data.get('private_root','')).resolve()
    require(root != Path('/') and path.parent == root and path.is_relative_to(root),
            'graph provider record/root mismatch')
    expected_files={root/name for name in FILES}|{root/'graph-provider.json'}
    expected_directories={root/'include'}
    actual_files=set();actual_directories=set()
    for member in root.rglob('*'):
        require(not member.is_symlink() and member.resolve().is_relative_to(root),
                'graph provider contains escaping/symlink member')
        (actual_directories if member.is_dir() else actual_files).add(member.resolve())
    require(actual_files=={item.resolve() for item in expected_files} and
            actual_directories=={item.resolve() for item in expected_directories},
            'graph provider directory members differ')
    inputs=data.get('inputs',{})
    require(set(inputs) == {'official','strict','patch_review'},
            'graph provider input set differs')
    official_path=Path(inputs['official'].get('record_path','')).resolve()
    strict_path=Path(inputs['strict'].get('record_path','')).resolve()
    review_path=Path(inputs['patch_review'].get('path','')).resolve()
    _outside((root,official_path,strict_path,review_path),forbidden_roots)
    require(digest(official_path)==inputs['official'].get('record_sha256') and
            digest(strict_path)==inputs['strict'].get('record_sha256') and
            digest(review_path)==inputs['patch_review'].get('sha256'),
            'graph provider input record changed')
    official=verify_official_record(official_path)
    strict=verify_strict_record(strict_path,review_path,forbidden_roots,objdump)
    require(data.get('classes') == dict(source=SOURCE_CLSID,video=VIDEO_CLSID) and
            data.get('threading_model') == 'Both' and
            data.get('input_files_unchanged') is True,
            'graph provider activation/input contract differs')

    files=data.get('files');binaries=data.get('binaries')
    expected=data.get('expected_module_paths')
    require(isinstance(files,dict) and set(files)==set(FILES) and
            isinstance(binaries,dict) and set(binaries)==set(BINARIES) and
            isinstance(expected,dict) and set(expected)==set(BINARIES),
            'graph provider member set differs')
    for name in FILES:
        item=files[name];origin=ORIGINS[name];target=(root/name).resolve()
        require(set(item)=={'sha256','bytes','origin'} and item['origin']==origin and
                target.is_relative_to(root) and digest(target)==item['sha256'] and
                target.stat().st_size==item['bytes'], 'graph provider file changed: '+name)
        source=_source_path(name,official_path,strict_path)
        require(item['sha256']==digest(source), 'graph provider origin mix differs: '+name)
    require((root/'provider.manifest').read_text()==(official_path.parent/'provider.manifest').read_text() and
            digest(root/'LAVFilters.Dependencies.manifest')==
            digest(official_path.parent/'LAVFilters.Dependencies.manifest'),
            'official graph manifests differ')

    actual_binaries=_binary_records(root,objdump)
    for name in BINARIES:
        require(expected[name]==str((root/name).resolve()),
                'graph provider expected module path differs: '+name)
        actual=actual_binaries[name]
        require(binaries[name]==actual and actual['sha256']==files[name]['sha256'],
                'graph provider binary record differs: '+name)
        source_imports=(strict['artifacts'][name]['imports'] if name in STRICT_MODULES else
                        sorted(item.lower() for item in official['binaries'][name]['imports']))
        require(actual['imports']==source_imports,
                'graph provider binary origin closure differs: '+name)
    return data


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--official-provider',required=True,type=Path)
    parser.add_argument('--strict-provider',required=True,type=Path)
    parser.add_argument('--patch-review',required=True,type=Path)
    parser.add_argument('--forbidden-root',required=True,action='append',type=Path)
    parser.add_argument('--objdump',default='i686-w64-mingw32-objdump')
    args=parser.parse_args()
    record=prepare(args.output,args.official_provider,args.strict_provider,args.patch_review,
                   args.forbidden_root,args.objdump)
    print(json.dumps(dict(record=str(args.output.resolve()/'graph-provider.json'),
                          modules=len(record['binaries']),files=len(record['files']))))


if __name__=='__main__':main()
