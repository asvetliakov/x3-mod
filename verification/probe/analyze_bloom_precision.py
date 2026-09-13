#!/usr/bin/env python3
"""Offline qualification of retained bloom outputs with independent precision controls.

The original nearest-store/ideal-sampling rejection remains unchanged. Exact
1D sampler modeling applies only to cases which failed that original verdict;
all 2D cases retain their independent nine-tap ideal verdict. Local stages use
actual prior GPU inputs; whole-chain stages start from authored/uploaded inputs.
Neither analysis changes the original .002+.003*abs(expected) tolerance.
"""
import argparse
import hashlib
import json
import math
import re
from pathlib import Path
import struct
import tempfile

import bloom_characterization as ch
import run_bloom_filter as run


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def store(image,model):
    return [[tuple(ch.fp16(v,**model) for v in p) for p in row] for row in image]


def linear_sample_1d(row,u,model):
    position=ch.f32(u*len(row)-.5)
    base=math.floor(position)
    fraction=position-base
    if model['fraction_bits']:
        scale=1<<model['fraction_bits']
        value=fraction*scale
        mode=model['fraction_rounding']
        fraction=(math.floor(value) if mode=='toward_zero' else round(value)
                  if mode=='nearest_even' else math.floor(value+.5))/scale
    a,b=row[min(max(base,0),len(row)-1)],row[min(max(base+1,0),len(row)-1)]
    values=tuple(ch.f32(x*(1-fraction)+y*fraction) for x,y in zip(a,b))
    return values if model['result_precision']=='float32' else tuple(
        ch.fp16(v,model['result_precision'],model['flush_subnormal']) for v in values)


def tent_1d(row,u,model):
    """Fused tent's source-level float32 coordinates, measured sampler precision.

    The independent ideal oracle remains ref.tent (nine unfused taps). The
    diagnostic math here is intentionally the authored four-fetch algorithm;
    it isolates implementation precision, not algebraic filter correctness.
    For one-pixel height the vertical normalized factors sum to one.
    """
    position=ch.f32(u*len(row)-.5)
    base=math.floor(position)
    fraction=ch.f32(position-base)
    ld,rd=ch.f32(3-2*fraction),ch.f32(1+2*fraction)
    inverse=ch.f32(1/len(row))
    left=ch.f32(ch.f32(ch.f32(2-fraction)*ch.f32(1/ld)+base-.5)*inverse)
    right=ch.f32(ch.f32(fraction*ch.f32(1/rd)+base+1.5)*inverse)
    a,b=linear_sample_1d(row,left,model),linear_sample_1d(row,right,model)
    return tuple(ch.f32(x*ch.f32(ld*.25)+y*ch.f32(rd*.25)) for x,y in zip(a,b))


def up_1d(fine,coarse,scatter,uvs,sampler,store_model):
    if len(fine)!=1 or len(coarse)!=1 or len(uvs)!=len(fine[0]):
        raise ValueError('measured model is restricted to one-pixel height')
    row=[]
    for x,pixel in enumerate(fine[0]):
        # The independently measured quad interpolant must still address the
        # correct fine point texel; never hide a half-pixel/rasterization error.
        if math.floor(uvs[x][0]*len(fine[0]))!=x:
            raise ValueError('measured UV does not address the matching fine texel')
        coarse_pixel=tent_1d(coarse[0],uvs[x][0],sampler)
        row.append(tuple(ch.fp16(ch.f32((1-scatter)*a+scatter*b),**store_model)
                         for a,b in zip(pixel,coarse_pixel)))
    return [row]


def modeled_stages(case,uv_maps,sampler,store_model,actual_inputs=None):
    image=run.quantize(case['image'],alpha=True)
    if len(image)!=1:
        raise ValueError('measured whole-chain model is 1D only')
    p=case['params']
    current=[[run.ref.prefilter(pixel,p,case['exposure'],case['clamp'],case['mode']) for pixel in image[0]]]
    stages={};down=[]
    for i,_ in enumerate(run.ref.layout(len(image[0]),1,p.levels)):
        if i and actual_inputs is not None:
            current=actual_inputs[f'd{i-1}']
        current=store(run.ref.downsample(current),store_model)
        stages[f'd{i}']=current
        down.append(current)
    for i in range(len(down)-2,-2,-1):
        width=len(down[i][0]) if i>=0 else len(image[0])
        fine=(actual_inputs[f'd{i}'] if actual_inputs is not None else down[i]) if i>=0 else [[(0.,0.,0.)]*width]
        if actual_inputs is not None:
            current=actual_inputs[f'd{len(down)-1}' if i==len(down)-2 else f'u{i+1}']
        current=up_1d(fine,current,p.scatter if i>=0 else 1.,uv_maps[width,1],sampler,store_model)
        stages[f'u{i}' if i>=0 else 'final']=current
    return stages


def compare(actual,expected):
    maximum=0.;ratio=0.;failures=0;exact=0;channels=0
    for row,other in zip(actual,expected):
        for pixel,wanted in zip(row,other):
            for a,b in zip(pixel,wanted):
                channels+=1
                error=abs(a-b)
                maximum=max(maximum,error)
                relative=error/(run.ABS_TOLERANCE+run.REL_TOLERANCE*abs(b))
                ratio=max(ratio,relative)
                failures+=relative>1
                exact+=a==b
    return dict(passed=not failures,channels=channels,bit_exact_channels=exact,
                failure_count=failures,max_absolute_error=maximum,max_tolerance_ratio=ratio)


def current_ideal_verdicts(cases,records,modeled_cases):
    """Recompute every ideal verdict; historical pass flags are never evidence.

    Only historically affected 1D cases may use the measured precision model.
    New ideal failures elsewhere must reject qualification, including any 2D
    failure, without silently expanding the modeled scope.
    """
    expected={i:run.reference_stages(case) for i,case in enumerate(cases)}
    verdicts={}
    for key,record in sorted(records.items()):
        generation,i,stage=key
        verdicts[key]=dict(generation=generation,case=i,name=cases[i]['name'],stage=stage,
            **run.compare_image(Path(record['file']),expected[i][stage],
                                cases[i]['name'].startswith('black_')))
    failures=[key for key,row in verdicts.items() if not row['passed']]
    two_d=sum(len(cases[i]['image'])!=1 for _,i,_ in failures)
    unmodeled=sum(i not in modeled_cases for _,i,_ in failures)
    return verdicts,dict(images=len(verdicts),failed_images=len(failures),
        two_dimensional_failed_images=two_d,unmodeled_failed_images=unmodeled,
        required_checks_passed=not two_d and not unmodeled)


def analyze(bloom_path,characterization_path):
    bloom=json.loads(Path(bloom_path).read_text())
    calibration=json.loads(Path(characterization_path).read_text())
    if bloom.get('exit_code')!=0 or calibration.get('exit_code')!=0 \
        or not bloom.get('inputs_unchanged') or not calibration.get('inputs_unchanged'):
        raise ValueError('GPU runs must have completed with stable inputs')
    for name in ('case_bundle_sha256','compiler_sha256_before','compiled_shaders','runtime_files_before'):
        if bloom[name]!=calibration[name]:
            raise ValueError('GPU/calibration provenance mismatch: '+name)
    cases=run.make_cases()
    with tempfile.TemporaryDirectory(prefix='x3-bloom-oracle-input-') as temp:
        bundle=Path(temp)/'cases.bin';run.write_bundle(cases,bundle)
        if digest(bundle)!=bloom['case_bundle_sha256']:
            raise ValueError('Current authored case bundle differs from GPU input')
        controls=Path(temp)/'characterization.bin';ch.write_inputs(controls)
        if digest(controls)!=calibration['characterization_bundle_sha256']:
            raise ValueError('Current independent controls differ from GPU input')
    retained=Path(calibration['retained'])
    prior_path=retained/'prior-summary.json'
    prior=json.loads(prior_path.read_text())
    if prior.get('passed') is not False or prior.get('exit_code')!=0 or not prior.get('inputs_unchanged'):
        raise ValueError('Prior characterization rejection record is missing or invalid')
    for name,sha in calibration['characterization_artifacts'].items():
        if digest(retained/name)!=sha:
            raise ValueError('Independent control readback changed: '+name)
    caps=calibration.get('caps',[])
    match=re.fullmatch(r'CAPS ps=\S+ vs=\S+ max_width=(\d+) max_height=(\d+)',caps[0]) if len(caps)==1 else None
    if not match or bloom.get('skipped_cases'):
        raise ValueError('This 40-case retained analysis requires complete admitted dimensions')
    characterized=ch.analyze(retained,cases,int(match[1]),int(match[2]))
    if not characterized['passed']:
        raise ValueError('Independent precision controls failed')
    uv_maps={(p['width'],p['height']):ch.read_pixels(p['file'],'f',p['width']*p['height'])
             for p in characterized['uv_maps']}
    actual={};records={}
    expected_keys=set()
    for i,case in enumerate(cases):
        levels=len(run.ref.layout(len(case['image'][0]),len(case['image']),case['params'].levels))
        names=[*(f'd{j}' for j in range(levels)),*(f'u{j}' for j in range(levels-1)),'final']
        for generation in range(2):
            expected_keys.update((generation,i,name) for name in names)
    for row in bloom['images']:
        key=row['generation'],row['case'],row['stage']
        if key in records or key not in expected_keys or digest(row['file'])!=row['sha256']:
            raise ValueError('Unexpected/duplicate/modified retained readback')
        case=cases[row['case']]
        sizes=run.ref.layout(len(case['image'][0]),len(case['image']),case['params'].levels)
        width,height=(len(case['image'][0]),len(case['image'])) if row['stage']=='final' else sizes[int(row['stage'][1:])]
        pixels=ch.read_pixels(row['file'],'e',width*height)
        if any(p[3]!=0 or any(v<0 or v>run.ref.FP16_MAX for v in p[:3]) for p in pixels):
            raise ValueError('Readback alpha/radiance bounds failed')
        actual[key]=[[p[:3] for p in pixels[y*width:(y+1)*width]] for y in range(height)]
        records[key]=row
    if set(records)!=expected_keys:
        raise ValueError('Incomplete case/generation/stage coverage')
    affected=sorted({row['case'] for row in bloom['images'] if not row['passed']})
    if any(len(cases[i]['image'])!=1 for i in affected):
        raise ValueError('A failed 2D ideal verdict cannot use the restricted 1D model')
    ideal,ideal_summary=current_ideal_verdicts(cases,records,set(affected))
    report=dict(schema=1,passed=False,scope='Retained 40-case precision qualification; no new GPU execution',
        original_ideal_verdict_passed=bloom['passed'],original_ideal_failed_images=sum(not r['passed'] for r in bloom['images']),
        original_gpu_summary=str(Path(bloom_path).resolve()),original_gpu_summary_sha256=digest(bloom_path),
        characterization_gpu_summary=str(Path(characterization_path).resolve()),characterization_gpu_summary_sha256=digest(characterization_path),
        prior_characterization_gpu_summary=str(prior_path.resolve()),prior_characterization_gpu_summary_sha256=digest(prior_path),
        characterization=characterized,ideal_2d_checks_preserved=ideal_summary['two_dimensional_failed_images']==0,
        current_ideal_summary=ideal_summary,current_ideal_verdicts=list(ideal.values()),modeled_dimensions='height=1 only',
        native_windows_runtime_verified=False,renderer_integration_verified=False,game_launched=False,
        model_chronology='Nearest-up result mode and one-ULP 2D envelope added after their respective training residuals; unchanged held-outs validate both',
        precision_tolerance=dict(absolute=run.ABS_TOLERANCE,relative=run.REL_TOLERANCE),cases=len(cases),generations=2,
        images=len(records),affected_cases=[cases[i]['name'] for i in affected],comparisons=[])
    for i in affected:
        whole=modeled_stages(cases[i],uv_maps,characterized['sampler_model'],characterized['store_model'])
        for generation in range(2):
            inputs={stage:actual[generation,i,stage] for stage in whole}
            local=modeled_stages(cases[i],uv_maps,characterized['sampler_model'],characterized['store_model'],inputs)
            for stage in whole:
                key=generation,i,stage
                old=records[key]
                report['comparisons'].append(dict(generation=generation,case=i,name=cases[i]['name'],stage=stage,
                    file=old['file'],sha256=old['sha256'],whole_chain=compare(actual[key],whole[stage]),
                    local_stage=compare(actual[key],local[stage]),
                    ideal_nearest_store=ideal[key],
                    historical_ideal_nearest_store=dict(passed=old['passed'],failure_count=old['failure_count'],
                        max_absolute_error=old['max_absolute_error'],max_tolerance_ratio=old['max_tolerance_ratio'])))
    report['modeled_channels']=sum(r['whole_chain']['channels'] for r in report['comparisons'])
    report['whole_chain_bit_exact_channels']=sum(r['whole_chain']['bit_exact_channels'] for r in report['comparisons'])
    report['local_stage_bit_exact_channels']=sum(r['local_stage']['bit_exact_channels'] for r in report['comparisons'])
    report['passed']=ideal_summary['required_checks_passed'] and all(
        r['whole_chain']['passed'] and r['local_stage']['passed'] for r in report['comparisons'])
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bloom',type=Path,default=run.ROOT/'verification/results/bloom-filter-summary.json')
    parser.add_argument('--characterization',type=Path,default=run.ROOT/'verification/results/bloom-filter-characterization-summary.json')
    parser.add_argument('--output',type=Path,default=run.ROOT/'verification/results/bloom-filter-precision-summary.json')
    args=parser.parse_args()
    sources=[Path(__file__),Path(ch.__file__),Path(run.__file__),Path(run.ref.__file__),
             run.ROOT/'tools/analysis/agx_reference.py']
    before={str(p.resolve()):digest(p) for p in sources}
    result=analyze(args.bloom,args.characterization)
    result['analysis_sources_before']=before
    result['analysis_sources_after']={p:digest(p) for p in before}
    if result['analysis_sources_after']!=before:
        raise ValueError('Analysis source changed')
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:result[k] for k in ('passed','cases','images','affected_cases','modeled_channels',
                                          'whole_chain_bit_exact_channels','local_stage_bit_exact_channels')}))
    if not result['passed']:
        raise SystemExit(1)


if __name__=='__main__':
    main()
