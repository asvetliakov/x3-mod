"""Original matrices test the new engine-memory/upload comparison and scope gates."""
from copy import deepcopy
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/analysis'))
from analyze_object_capture import analyze, multiply, transpose, inverse


def bits(row):
    return ','.join(f'{struct.unpack("<I",struct.pack("<f",v))[0]:08x}' for v in row)


def fixture():
    world=[[2.,0,0,0],[0,3.,0,0],[0,0,4.,0],[10.,20.,30.,1.]]
    view=[[1.,0,0,0],[0,1.,0,0],[0,0,1.,0],[-2.,-3.,-4.,1.]]
    projection=[[.8,0,0,0],[0,1.2,0,0],[0,0,1.01,1.],[0,0,-1.,0]]
    camera=inverse(transpose(view)); wvp=transpose(multiply(multiply(world,view),projection))
    constants={}
    for start,m,count in ((24,wvp,4),(28,transpose(world),3),(34,camera,3)):
        constants.update({str(start+i):bits(m[i]) for i in range(count)})
    context=dict(scoped='1',valid='127',session='1',node='1000',node_handle='3',camera='2000',camera_handle='4',
                 model='5',lod='0',flags130='00000200')
    draw=dict(index=1,vs='synthetic',ps='synthetic',states={'7':1,'14':1},object_context=context,
              object_context_matches_draw=True,draw_result={'result':'00000000'},
              constants={'vs':{'f':constants}},constant_status={'vs':{'f':{'count':'256','result':'00000000','encoding':'sparse_zero'}}},
              object_matrix=[dict(role=role,row=str(i),bits=bits(row)) for role,m in
                             [('world',world),('world_basis',world),('view',view),('projection',projection)] for i,row in enumerate(m)],
              buffer_content=[dict(kind='vertex',identity='1',result='00000000',status='00000000',requested='1',known='1',
                                   ambiguous='0',revision='1',pending='0')])
    events=[dict(op='clear',seq=str(i+1),after_draw=str(i),result='00000000',details=[
        dict(event='clear',flags=str(3 if i==0 else 2),z='1',rect_count='0')]) for i in range(2)]
    frame=dict(draws=[draw],complete=True,draw_count_matches=True,event_sequence_contiguous=True,present_result='00000000',events=events)
    metadata={'vs_synthetic':[dict(name=name,register_set=2,parameter_class=3,rows=4,columns=4,register=reg,count=count)
               for name,reg,count in [('g_mWorldViewProjection',24,4),('g_mWorld',28,3),('g_mViewInverse',34,3)]]}
    return frame,metadata


class ObjectCaptureAnalysisTests(unittest.TestCase):
    def test_transpose_and_row_vector_product_are_supported_by_original_matrices(self):
        frame,metadata=fixture();report=analyze({'frames':{'1:1':frame}},metadata)['frames']['1:1']
        checks=report['matrix_checks']
        self.assertEqual(checks['engine_world_transpose_vs_shader_relative']['maximum'],0)
        self.assertGreater(checks['wrong_untransposed_world_relative']['maximum'],.5)
        self.assertLess(checks['transpose_engine_WVP_vs_shader_relative']['maximum'],1e-6)
        self.assertLess(checks['engine_view_transpose_vs_inverse_shader_camera_relative']['maximum'],1e-6)

    def test_integer_scale_is_not_decoded_as_nonfinite_float(self):
        frame,metadata=fixture()
        frame['draws'][0]['object_matrix'].append(dict(role='scale',row='0',bits='ffffffff,00010000,00010000,00010000'))
        report=analyze({'frames':{'1:1':frame}},metadata)['frames']['1:1']
        self.assertNotIn('nonfinite',report['unavailable'])
        self.assertEqual(report['node_candidates'],1)

    def test_mismatched_context_is_not_world_evidence(self):
        frame,metadata=fixture();frame['draws'][0]['object_context_matches_draw']=False
        report=analyze({'frames':{'1:1':frame}},metadata)['frames']['1:1']
        self.assertEqual(report['node_candidates'],0)
        self.assertEqual(report['unavailable']['object_status'],1)

    def test_unknown_buffer_revision_is_not_continuity_evidence(self):
        frame,metadata=fixture();frame['draws'][0]['buffer_content'][0].update(known='0',pending='1',status='00000001')
        report=analyze({'frames':{'1:1':frame}},metadata)['frames']['1:1']
        self.assertEqual(report['buffer_allocations'],0)
        self.assertEqual(report['buffer_status'][0]['pending'],'1')

    def test_unusable_contexts_do_not_create_camera_delta_across_frames_or_gaps(self):
        for state in ('unscoped', 'mismatched'):
            for next_frame in ('1:2', '1:3'):
                with self.subTest(state=state, next_frame=next_frame):
                    frame, metadata = fixture()
                    if state == 'unscoped':
                        frame['draws'][0]['object_context'].update(scoped='0', valid='0')
                    else:
                        frame['draws'][0]['object_context_matches_draw'] = False
                    report = analyze({'frames': {'1:1': frame, next_frame: deepcopy(frame)}}, metadata)
                    self.assertIsNone(report['frames'][next_frame]['dominant_camera'])
                    self.assertEqual(report['burst_camera_changes'], [])
                    for comparison in report['adjacent_frames']:
                        self.assertNotIn('dominant_camera_delta', comparison)

    def test_malformed_matrix_rows_are_unavailable_not_camera_evidence(self):
        for change in ('wrong_key', 'duplicate', 'short', 'long', 'nonfinite', 'invalid_bits'):
            with self.subTest(change=change):
                frame, metadata = fixture()
                rows = frame['draws'][0]['object_matrix']
                if change == 'wrong_key': rows[0]['row'] = '4'
                elif change == 'duplicate': rows.append(deepcopy(rows[0]))
                elif change == 'short': rows[0]['bits'] = bits([1., 0, 0])
                elif change == 'long': rows[0]['bits'] = bits([1., 0, 0, 0, 0])
                elif change == 'nonfinite': rows[0]['bits'] = bits([float('nan'), 0, 0, 0])
                else: rows[0]['bits'] = 'not_hex'
                report = analyze({'frames': {'1:1': frame, '1:2': deepcopy(frame)}}, metadata)
                self.assertEqual(report['frames']['1:1']['unavailable']['matrix_rows'], 1)
                self.assertIsNone(report['frames']['1:1']['dominant_camera'])
                self.assertNotIn('dominant_camera_delta', report['adjacent_frames'][0])

    def test_gaps_do_not_create_adjacent_node_matches(self):
        frame,metadata=fixture();report=analyze({'frames':{'1:1':frame,'1:3':deepcopy(frame)}},metadata)
        self.assertEqual(report['adjacent_frames'],[])
        self.assertEqual(len(report['burst_camera_changes']),1)

    def test_multiple_world_values_reject_node_candidate_and_changed_buffers_are_reported(self):
        frame,metadata=fixture();following=deepcopy(frame)
        duplicate=deepcopy(following['draws'][0]);duplicate['index']=2
        duplicate['object_matrix'][0]['bits']=bits([4.,0,0,0])
        duplicate['buffer_content'][0]['revision']='2';following['draws'].append(duplicate)
        following['draws'][0]['buffer_content'][0]['revision']='2'
        report=analyze({'frames':{'1:1':frame,'1:2':following}},metadata)
        self.assertEqual(report['frames']['1:2']['conflicting_world_candidates'],1)
        self.assertEqual(report['adjacent_frames'][0]['unambiguous_world_candidates'],0)
        self.assertEqual(len(report['adjacent_frames'][0]['changed_buffer_revisions']),1)


if __name__=='__main__':unittest.main()
