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
# Stored-density range (X3M_VOLUMETRIC_FOG_RANGE=stored) through the same fragment and a real worker.
DENSITY_REQUIRED={
 'legacy_route_never_starts_density','density_reference_legacy_frame','legacy_mode_never_touches_density',
 'stored_refusal_reason_and_no_worker','stored_refused_is_legacy_bit_identical',
 'stored_filling_frames_native_exact_no_transaction','stored_ramps_monotone_bounded','stored_march_composite_repair_once',
 'stored_renders_through_route','stored_ramp_stacks_medium_on_native_cards','cards_never_masked_below_full_far_ramp',
 'heap_token_change_keeps_key_cache_and_image','one_frame_gap_keeps_cache','one_frame_gap_resumes_bit_identical','stored_dynamic_cache_live','stored_steady_frame_bit_identical',
 'sector_change_rekeys_whole_far_node_offset','sector_change_drops_readiness_same_frame','sector_change_refills_and_ramps',
 'sector_change_places_clouds_differently','sector_rekey_is_deterministic',
 'toggle_off_native_and_no_density_work','toggle_on_resumes_without_leak','toggle_on_regenerates_nothing',
 'reset_refills_gpu_and_reapplies_bit_identical','reset_reuploads_without_regeneration',
 'load_gap_invalidates_same_frame','load_gap_refills_and_ramps','small_cut_stays_resident',
 'camera_jump_is_native_same_frame','camera_jump_refills',
 'device_release_with_live_worker_joins_and_balances','stored_route_leaves_device_refcount',
 'abandon_witness_has_live_worker','abandon_then_pass_destructor_is_prompt_and_balanced',
 # The fog dust motes' A/B (Ctrl+Alt+F11; fog-dust-motes.md section 5.3) through the same fragment.
 'motes_ab_option_absent_creates_nothing_rows_absent','motes_ab_launch_on_creates_at_prepare_and_draws','motes_ab_launch_on_row_fields',
 'motes_ab_toggle_off_logs_one_row','motes_ab_toggled_off_frame_is_the_launch_off_frame','motes_ab_toggled_off_row_fields',
 'motes_ab_toggled_off_keeps_the_buffers','motes_ab_alternating_frames_applied','motes_ab_alternating_creates_nothing',
 'motes_ab_toggle_on_before_reset','motes_ab_reset_releases_the_buffers','motes_ab_reset_recreates_the_buffers_once','motes_ab_after_reset_draws',
}
LEGACY_IMAGES=('legacy_warm','legacy_replaced','legacy_captured_cameras_33','legacy_new_family_warm','legacy_after_reset')
DENSITY_SCOPE='DENSITY_SCOPE range=stored worker=real_thread cache=dynamic owner=synthetic native_reset=pass_edges_only process_exit=separate_exit_fixture'
def images(text):
    rows=re.findall(r'^IMAGE (\S+) ([0-9a-f]{16})$',text,re.M)
    if len(rows)!=len(dict(rows)):raise ValueError('duplicate image witness')
    return dict(rows)
def fields(text,tag):
    return [{k:float(v) for k,v in re.findall(r'(\w+)=([-0-9.e+]+)',line)} for line in re.findall(r'^%s (.*)$'%tag,text,re.M)]
def validate_density(text,baseline=None):
    rows=set(re.findall(r'^CHECK (\S+) PASS$',text,re.M));missing=DENSITY_REQUIRED-rows
    if missing:raise ValueError('missing stored-density witnesses: '+','.join(sorted(missing)))
    if text.splitlines().count(DENSITY_SCOPE)!=1:raise ValueError('missing/ambiguous stored-density scope')
    seen=images(text)
    if any(name not in seen for name in (*LEGACY_IMAGES,'legacy_pose_a','stored_sector_a','stored_sector_b')):raise ValueError('missing image witness')
    if len(re.findall(r'^volumetric_fog_cache .*event=refused reason=density_ps30_slots fallback=legacy',text,re.M))!=1:raise ValueError('refusal must log exactly one line')
    if not re.search(r'^volumetric_fog_cache .*event=far_ready ms=',text,re.M) or not re.search(r'^volumetric_fog_cache .*event=fine_ready ms=',text,re.M):raise ValueError('missing readiness lines')
    if re.search(r'^volumetric_fog_cache_frame ',text,re.M):raise ValueError('per-frame density logging without the timing option')
    identical=None
    if baseline is not None:
        before=images(baseline)
        if any(name not in before for name in LEGACY_IMAGES):raise ValueError('baseline lacks legacy image witnesses')
        identical=all(before[name]==seen[name] for name in LEGACY_IMAGES)
        if not identical:raise ValueError('legacy images differ from the baseline build')
    fills=fields(text,'DENSITY_FILL');cost=fields(text,'DENSITY_COST');release=fields(text,'DENSITY_RELEASE');abandon=fields(text,'DENSITY_ABANDON');reset=fields(text,'DENSITY_RESET')
    if len(fills)<2 or len(cost)!=1 or len(release)!=1 or len(abandon)!=1 or len(reset)!=1:raise ValueError('missing stored-density measurements')
    return dict(legacy_bit_identical_to_baseline=identical,images=seen,first_fill=fills[0],sector_change_fill=fills[1],prepare_cost_us=cost[0],reset=reset[0],release=release[0],abandon=abandon[0])
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
 'Stored-density range: real worker, cache, uploads and ramps behind the synthetic owner; process exit under the loader lock is the separate exit fixture; fixture timings are not game FPS or GPU cost.',
]
def validate(text,baseline=None):
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
    return dict(result='PASS',checks=len(rows),witnesses=sorted(set(rows)),limits=LIMITS,density=validate_density(text,baseline))
def main():
    p=argparse.ArgumentParser();p.add_argument('--log',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline-log',type=Path,help='log of the baseline build (previous production fragment and pass): legacy images must be bit-identical');a=p.parse_args()
    report=validate(a.log.read_text(),a.baseline_log.read_text() if a.baseline_log else None);build=json.loads(a.build.read_text());exe=Path(build['executable'])
    digest=hashlib.sha256(exe.read_bytes()).hexdigest()
    if digest!=build['sha256']:raise ValueError('fixture executable changed after build')
    report.update(executable_sha256=digest,build=str(a.build.resolve()),log=str(a.log.resolve()),log_sha256=hashlib.sha256(a.log.read_bytes()).hexdigest())
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(f"PASS checks={report['checks']}")
if __name__=='__main__':main()
