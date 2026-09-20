#!/usr/bin/env python3
"""Validate the completed bridge log; never launches or builds anything."""
import argparse, hashlib, json, re
from collections import Counter
from pathlib import Path
REQUIRED={
 'captured_camera_suppression','captured_camera_actual_pass','captured_camera_no_transition','captured_camera_sequence_33',
 'malformed_camera_parameter_refusal','inverse_gram_boundary_refusal','malformed_camera_native_exact','malformed_camera_no_pass','malformed_camera_recovery',
 'refusal_final_RT0_exact','execution_failure_suppressed_first','actual_pass_reopen_failure',
 'actual_result_reconciles_unknown_scene_poison','failed_pass_bindings_restored','failed_pass_source_clean',
 'failed_pass_RT1_RT2_bytes','poisoned_fallback_no_second_transaction','execution_poison_survives_F9',
 'execution_poison_reset_rewarm','execution_poison_reset_applies',
 'native_card_LastError_preserved','native_card_forward_once','warmup_native_source','matching_warmup_arms','replacement_source_clean',
 'replacement_transition_requests_history_clear','transition_once_per_frame',
 'replacement_pass_applied','native_draw_count_exact','same_family_sector_rewarm',
 'new_family_generation_rewarm','stale_generation_native','query_fault_suppressed_first',
 'late_query_fault_latched','F9_cannot_erase_fault','reset_requires_warmup',
 'source_HRESULT_count_fault','route_pass_state_restored','once_only_hook_fallback_guard',
 'one_fog_transaction_maximum','route_RT1_RT2_bytes','refusal_no_medium',
 *(f'native_exact_refusal_{i}' for i in range(5)),
}
CAMERA_COUNTS={
 'captured_camera_suppression':33,'captured_camera_actual_pass':33,'captured_camera_no_transition':33,
 'captured_camera_sequence_33':1,'malformed_camera_parameter_refusal':4,'inverse_gram_boundary_refusal':1,
 'malformed_camera_native_exact':4,'malformed_camera_no_pass':4,'malformed_camera_recovery':4,
}
CAMERA_SCOPE='CAMERA_BRIDGE captured=33 first_person=32 worst=1 malformed=4 inverse_boundary=1 PASS'
LIMITS=[
 'Synthetic owner/sun/sector and captured camera rotations; no shaft-map publication, game reader or shader recognition is exercised.',
 'Whole production fog fragment, real native card calls and FogPass; actual scene-hook/selector dispatch is covered separately.',
 'TAA invalidation request endpoint only; no claim of GPU temporal-history clearing or motion quality.',
 'Pass/policy Reset rewarm here; native device Reset is covered by the separate production state fixture.',
 'CrossOver results do not establish native Windows runtime parity.',
]
def validate(text):
    if re.search(r'^(?:RESULT FAIL|FAIL |GAP |CHECK .* FAIL)',text,re.M):raise ValueError('fixture failure/gap')
    rows=re.findall(r'^CHECK (\S+) PASS$',text,re.M)
    result=re.findall(r'^RESULT fog_route_bridge checks=(\d+) PASS$',text,re.M)
    if len(result)!=1 or int(result[0])!=len(rows):raise ValueError('missing/ambiguous result or check count mismatch')
    missing=REQUIRED-set(rows)
    if missing:raise ValueError('missing witnesses: '+','.join(sorted(missing)))
    counts=Counter(rows)
    if any(counts[name]!=count for name,count in CAMERA_COUNTS.items()):raise ValueError('incomplete camera sequence')
    if text.splitlines().count(CAMERA_SCOPE)!=1:raise ValueError('missing/ambiguous camera scope')
    scope='BRIDGE_SCOPE methods=production_fog_fragment native_d3d=1 synthetic_owner=1 cached_shader_identity=synthetic selector_hook=not_exercised taa_history=request_endpoint_only native_reset=reused_state_fixture'
    if scope not in text:raise ValueError('missing explicit fixture scope')
    return dict(result='PASS',checks=len(rows),witnesses=sorted(set(rows)),limits=LIMITS)
def main():
    p=argparse.ArgumentParser();p.add_argument('--log',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    report=validate(a.log.read_text());build=json.loads(a.build.read_text());exe=Path(build['executable'])
    digest=hashlib.sha256(exe.read_bytes()).hexdigest()
    if digest!=build['sha256']:raise ValueError('fixture executable changed after build')
    report.update(executable_sha256=digest,build=str(a.build.resolve()),log=str(a.log.resolve()),log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest())
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(f"PASS checks={report['checks']}")
if __name__=='__main__':main()
