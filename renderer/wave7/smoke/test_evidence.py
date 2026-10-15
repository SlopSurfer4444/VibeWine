"""Offline parser controls. These synthetic receipts are never GPU execution evidence."""
from pathlib import Path
import json
from evidence import evidence, PHASES, GOLDEN
LAB=Path(__file__).resolve().parent/'runtime'

def close_receipt(waiter=1):
    return '\n'.join(('EVENT WM_CLOSE tid=1 tick=1000','EVENT WM_DESTROY tid=1 tick=1001','EVENT WM_NCDESTROY tid=1 tick=1002',
      'CLOSE_RETURN result=1 last_error=0 alive=0 tid=1 tick=1003',
      f'CLOSE_WAIT elapsed_ms=4 completion=1 destroy_event=1 destroyed=1 returned=1 result=1 return_alive=0 alive=0 timed_out=0 wait_failed=0 last_error=0 tid={waiter}',
      'CHECK close_wait_handle_valid valid=1','CHECK close_wait_deadline valid=1','CHECK close_return_observed valid=1','CHECK close_return_success valid=1',
      'CHECK close_return_hwnd_invalid valid=1','CHECK normal_destroy_observed valid=1','CHECK normal_close_destroy_quit valid=1'))

def receipt(mode='windowed',threaded=False,queued=False,overrides=None):
    sizes={p:(1920,1080) for p in PHASES}
    sizes.update(high=(2560,1440),target_high_zero=(1920,1080) if mode=='borderless' else (1280,720),
      target_low_zero=(2560,1440) if mode=='borderless' else (960,540),zero_both=(1920,1080) if mode=='borderless' else (960,540),zero_width=(1920,720) if mode=='borderless' else (960,720),
      zero_height=(1280,720) if mode=='borderless' else (1280,540),recovery_after_rejection=(1280,720) if mode=='borderless' else (1280,540))
    if overrides: sizes.update(overrides)
    lines=[f'RENDER_THREAD gui=1 render={2 if threaded else 1} separate={int(threaded)}']
    for n in ('d3d11.dll','dxgi.dll','winemetal.dll'): lines.append('MODULE '+n+' C:\\windows\\system32\\'+n)
    for p in PHASES:
        w,h=sizes[p]; lines.append(f'DIMENSIONS phase={p} expected={w}x{h} texture={w}x{h} public={w}x{h} format=28 samples=1')
        xs=(0,w-1,0,w-1,w//2,w//4,3*w//4,w//4,3*w//4); ys=(0,0,h-1,h-1,h//2,h//4,h//4,3*h//4,3*h//4)
        for i,color in enumerate(GOLDEN): lines.append(f'PIXEL phase={p} point={i} xy={xs[i]},{ys[i]} rgba='+','.join(map(str,color))+' valid=1')
        lines.extend((f'PHASE_END {p} presents=2','CHECK windowed_no_exclusive valid=1'))
        if mode=='borderless':
            lines.append('CHECK native_borderless_geometry valid=1')
            lines.append('GEOMETRY client=1710x1107 window=0,0,1710,1107 owner=1 caller=1')
    def observation(stage,w,h,client,outer):
        return f'TARGET_OBSERVATION stage={stage} requested={w}x{h} client={client[0]}x{client[1]} window='+','.join(map(str,outer))+' maxtrack=1722x1119 virtual=1710x1107 frame=4x4'
    client=(1710,1107) if mode=='borderless' else (792,566)
    outer=(0,0,1710,1107) if mode=='borderless' else (80,80,880,680)
    for w,h in ((1280,720),(960,540)):
        lines.append(observation('before',w,h,client,outer))
        if mode!='borderless': lines.append('CHECK target_fits_default_maxtrack valid=1')
        lines.append(f'RESIZE_TARGET requested={w}x{h} hr=0x'+('887a0004' if mode=='borderless' else '00000000'))
        if mode!='borderless': client,outer=(w,h),(80,80,88+w,114+h)
        lines.append(observation('after',w,h,client,outer))
        if mode=='borderless': lines.extend(('CHECK target_truthful_native_client_result valid=1','CHECK target_preserves_native_borderless_geometry valid=1'))
        else: lines.append('CHECK target_observed_client_matches_request valid=1')
    lines.append('CHECK unsupported_resize_rejected valid=1')
    if queued: lines.append('CLOSE_QUEUED submitted=8 query_hr=0x00000001 completed=0 explicit_wait_before_close=0')
    else: lines.append('RELEASE_BEGIN')
    lines.append(close_receipt(2 if threaded else 1))
    if queued: lines.append('RELEASE_BEGIN')
    lines.append('SMOKE_PASS wave=7 spatial_gpu_readback=1 actual_texture_dimensions=1 frames=18 natural_return=1')
    return '\n'.join(lines)+'\n'

def main():
    results=[]
    def check(name,value):
        results.append({'case':name,'passed':bool(value)}); assert value,name
    base=receipt()
    def classify(text=base,mode='windowed',natural=True,code=0,threaded=False,queued=False,failure='none'):
        return evidence(text,LAB,mode,natural,code,threaded,queued,failure)['state']
    check('admit_complete_synthetic_fixture',classify()=='PASS')
    check('admit_threaded_queued_borderless',classify(receipt('borderless',True,True),'borderless',threaded=True,queued=True)=='PASS')
    for name,changed in (
        ('missing_pixel',base.replace(next(line for line in base.splitlines(True) if line.startswith('PIXEL ')),'')),
        ('duplicate_pixel',base+next(line for line in base.splitlines(True) if line.startswith('PIXEL '))),
        ('flipped_color',base.replace('rgba=255,0,0,255','rgba=0,0,255,255',1)),
        ('swapped_corner',base.replace('xy=0,0','xy=1919,0',1)),
        ('wrong_actual_texture',base.replace('texture=1920x1080','texture=2560x1440',1)),
        ('wrong_public_dimensions',base.replace('public=1920x1080','public=1280x720',1)),
        ('no_close',base.replace('EVENT WM_CLOSE tid=1','')),
        ('no_destroy',base.replace('CHECK normal_close_destroy_quit valid=1','')),
        ('external_module',base.replace('C:\\windows\\system32\\d3d11.dll','Z:\\Users\\other\\d3d11.dll')),
        ('thread_claim_without_worker',base.replace('separate=0','separate=1')),
        ('missing_phase',base.replace('PHASE_END high presents=2','')),
        ('unsupported_target_general',base.replace('requested=1280x720 hr=0x00000000','requested=1280x720 hr=0x887a0004')),
    ): check('reject_'+name,classify(changed)=='FAIL_OR_INCONCLUSIVE')
    border=receipt('borderless')
    check('reject_wrong_partial_zero_windowed',classify(receipt(overrides={'zero_width':(960,1080)}))=='FAIL_OR_INCONCLUSIVE')
    check('reject_failed_borderless_target',classify(border.replace('hr=0x887a0004','hr=0x80004005'),'borderless')=='FAIL_OR_INCONCLUSIVE')
    check('reject_falsely_successful_borderless_target',classify(border.replace('hr=0x887a0004','hr=0x00000000'),'borderless')=='FAIL_OR_INCONCLUSIVE')
    check('reject_occluded_present_even_with_full_receipt',classify(base+'PRESENT_NOT_VISIBLE_SUCCESS hr=0x087a0001\n')=='FAIL_OR_INCONCLUSIVE')
    check('reject_fixture_failure_even_with_full_receipt',classify(base+'FIXTURE_FAILURE code=11\n')=='FAIL_OR_INCONCLUSIVE')
    first_observation=next(line for line in base.splitlines(True) if line.startswith('TARGET_OBSERVATION '))
    check('reject_missing_target_observation',classify(base.replace(first_observation,''))=='FAIL_OR_INCONCLUSIVE')
    check('reject_duplicate_target_observation',classify(base+first_observation)=='FAIL_OR_INCONCLUSIVE')
    check('reject_clamped_target_client_even_with_correct_backbuffers',classify(base.replace('stage=after requested=1280x720 client=1280x720','stage=after requested=1280x720 client=1200x700'))=='FAIL_OR_INCONCLUSIVE')
    check('reject_target_exceeding_maxtrack',classify(base.replace('maxtrack=1722x1119','maxtrack=1000x700'))=='FAIL_OR_INCONCLUSIVE')
    check('reject_metric_change_during_target',classify(base.replace('maxtrack=1722x1119','maxtrack=1723x1119',1))=='FAIL_OR_INCONCLUSIVE')
    check('reject_native_target_geometry_change',classify(border.replace('stage=after requested=1280x720 client=1710x1107','stage=after requested=1280x720 client=1280x720'),'borderless')=='FAIL_OR_INCONCLUSIVE')
    check('reject_cleanup_made_exit',classify(natural=False)=='FAIL_OR_INCONCLUSIVE')
    check('reject_nonzero_exit',classify(code=23)=='FAIL_OR_INCONCLUSIVE')
    controlled=base.replace('SMOKE_PASS wave=7 spatial_gpu_readback=1 actual_texture_dimensions=1 frames=18 natural_return=1','CONTROLLED_FAILURE after_gpu_and_cleanup exit=23')
    check('controlled_failure_is_not_positive',classify(controlled,code=23)=='FAIL_OR_INCONCLUSIVE')
    check('admit_expected_controlled_failure',classify(controlled,code=23,failure='controlled')=='EXPECTED_CONTROLLED_FAILURE')
    corrupt='RENDER_THREAD gui=1 render=1 separate=0\n'+''.join(line+'\n' for line in base.splitlines() if line.startswith('MODULE '))+ ''.join(line.replace('valid=1','valid=0')+'\n' if line.startswith('PIXEL phase=low_initial point=0 ') else line+'\n' for line in base.splitlines() if line.startswith('PIXEL phase=low_initial ')) + 'RELEASE_BEGIN_FAILURE\nRELEASE_END_FAILURE\n'+close_receipt()+'\nCHECK gpu_spatial_pattern valid=0\nFIXTURE_FAILURE code=24\n'
    check('admit_expected_readback_rejection',classify(corrupt,code=24,failure='corrupt')=='EXPECTED_READBACK_REJECTION')
    check('reject_corrupt_without_failure_release',classify(corrupt.replace('RELEASE_END_FAILURE',''),code=24,failure='corrupt')=='FAIL_OR_INCONCLUSIVE')
    check('reject_corrupt_release_after_close',classify(corrupt.replace('RELEASE_BEGIN_FAILURE\nRELEASE_END_FAILURE\n','')+'RELEASE_BEGIN_FAILURE\nRELEASE_END_FAILURE\n',code=24,failure='corrupt')=='FAIL_OR_INCONCLUSIVE')
    for name,changed in (
        ('destroy_return_false',base.replace('CLOSE_RETURN result=1','CLOSE_RETURN result=0')),
        ('destroy_return_handle_alive',base.replace('CLOSE_RETURN result=1 last_error=0 alive=0','CLOSE_RETURN result=1 last_error=0 alive=1')),
        ('close_wait_timeout',base.replace('elapsed_ms=4 completion=1','elapsed_ms=3000 completion=0').replace('timed_out=0','timed_out=1')),
        ('close_wait_error',base.replace('wait_failed=0 last_error=0','wait_failed=1 last_error=6')),
        ('close_wait_after_deadline',base.replace('elapsed_ms=4','elapsed_ms=3001')),
        ('destroy_event_without_return',base.replace('returned=1 result=1','returned=0 result=-1')),
        ('missing_ncdestroy',base.replace('EVENT WM_NCDESTROY tid=1 tick=1002\n','')),
        ('missing_destroy_return',base.replace('CLOSE_RETURN result=1 last_error=0 alive=0 tid=1 tick=1003\n','')),
        ('wrong_destroy_owner',base.replace('CLOSE_RETURN result=1 last_error=0 alive=0 tid=1','CLOSE_RETURN result=1 last_error=0 alive=0 tid=2')),
    ): check('reject_'+name,classify(changed)=='FAIL_OR_INCONCLUSIVE')
    queued_receipt=receipt(threaded=True,queued=True)
    early_release=queued_receipt.replace('RELEASE_BEGIN\n',''); early_release=early_release.replace('EVENT WM_CLOSE','RELEASE_BEGIN\nEVENT WM_CLOSE')
    check('reject_queued_release_before_close_completion',classify(early_release,threaded=True,queued=True)=='FAIL_OR_INCONCLUSIVE')
    check('reject_unexercised_corrupt_control',classify(controlled,code=23,failure='corrupt')=='FAIL_OR_INCONCLUSIVE')
    Path(__file__).with_name('offline-test-results.json').write_text(json.dumps({'scope':'Synthetic parser tests only; no Wine or GPU executed','cases':results,'passed':len(results)},indent=2)+'\n')
    print(f'{len(results)} offline evidence parser controls passed')

if __name__=='__main__': main()
