"""Receipt admission, independent of fixture generation. Offline tests are parser tests only."""
from pathlib import Path
import re

PHASES = ('low_initial', 'target_high_zero', 'high', 'target_low_zero', 'low_return', 'zero_both', 'zero_width', 'zero_height', 'recovery_after_rejection')
GOLDEN = ((255,0,0,255),(0,255,0,255),(0,0,255,255),(255,255,0,255),(255,0,255,255),
          (255,0,0,255),(0,255,0,255),(0,0,255,255),(255,255,0,255))

def evidence(text, lab, mode, natural, returncode, threaded=False, queued=False, failure='none'):
    lab = Path(lab)
    rows = re.findall(r'^PIXEL phase=(\w+) point=(\d+) xy=(\d+),(\d+) rgba=(\d+),(\d+),(\d+),(\d+) valid=([01])$', text, re.M)
    keys = [(r[0], int(r[1])) for r in rows]
    expected = {(phase, point) for phase in PHASES for point in range(9)}
    pixel_values = all(int(r[1]) < 9 and tuple(map(int,r[4:8])) == GOLDEN[int(r[1])] and r[8] == '1' for r in rows)
    pixel_set = len(keys) == len(expected) and set(keys) == expected
    dimensions = re.findall(r'^DIMENSIONS phase=(\w+) expected=(\d+)x(\d+) texture=(\d+)x(\d+) public=(\d+)x(\d+) format=(\d+) samples=(\d+)$', text, re.M)
    dim_map = {}
    dimensions_valid = True
    for phase, ew, eh, tw, th, pw, ph, fmt, samples in dimensions:
        row = tuple(map(int,(ew,eh,tw,th,pw,ph,fmt,samples)))
        dimensions_valid &= row[:2] == row[2:4] == row[4:6] and row[6:] == (28,1)
        dim_map.setdefault(phase,[]).append(row[:2])
    required_dims = {'low_initial':(1920,1080),'high':(2560,1440),'low_return':(1920,1080)}
    required_dims.update(zero_both=(1920,1080) if mode=='borderless' else (960,540),zero_width=(1920,720) if mode=='borderless' else (960,720),zero_height=(1280,720) if mode=='borderless' else (1280,540),recovery_after_rejection=(1280,720) if mode=='borderless' else (1280,540))
    required_dims.update(target_high_zero=(1920,1080) if mode=='borderless' else (1280,720),target_low_zero=(2560,1440) if mode=='borderless' else (960,540))
    dimensions_valid &= all(dim_map.get(name) == [size] for name,size in required_dims.items())
    dimensions_valid &= all(len(dim_map.get(name,[])) == 1 for name in PHASES)
    # Coordinates must address the actual expected texture locations, including all four corners.
    for r in rows:
        p, point, x, y = r[:4]; point,x,y = int(point),int(x),int(y)
        if p not in dim_map or point >= 9: dimensions_valid = False; continue
        w,h = dim_map[p][0]
        xs=(0,w-1,0,w-1,w//2,w//4,3*w//4,w//4,3*w//4)
        ys=(0,0,h-1,h-1,h//2,h//4,h//4,3*h//4,3*h//4)
        dimensions_valid &= (x,y) == (xs[point],ys[point])
    modules = re.findall(r'^MODULE ([^ ]+) (.+)$', text, re.M)
    local = lab / 'fixtures' / ('Game' if mode == 'borderless' else '')
    engine = lab / 'Wine Wave7 Smoke Lab.app/Contents/Resources/engine/lib/wine/x86_64-windows'
    modules_bound = len(modules) == 3 and {n for n,_ in modules} == {'d3d11.dll','dxgi.dll','winemetal.dll'}
    for name,path in modules:
        allowed = {('C:\\windows\\system32\\'+name).casefold()}
        allowed.update(('Z:'+str(folder/name).replace('/','\\')).casefold() for folder in (local,engine))
        modules_bound &= path.strip().casefold() in allowed
    closes = all(token in text for token in ('EVENT WM_CLOSE tid=', 'CHECK normal_destroy_observed valid=1',
                'CHECK normal_close_destroy_quit valid=1'))
    thread_rows = re.findall(r'^RENDER_THREAD gui=(\d+) render=(\d+) separate=([01])$',text,re.M)
    topology = len(thread_rows)==1 and (thread_rows[0][0] != thread_rows[0][1]) == threaded and thread_rows[0][2] == str(int(threaded))
    close_events=list(re.finditer(r'^EVENT (WM_CLOSE|WM_DESTROY|WM_NCDESTROY) tid=(\d+) tick=(\d+)$',text,re.M))
    close_returns=list(re.finditer(r'^CLOSE_RETURN result=([01]) last_error=(\d+) alive=([01]) tid=(\d+) tick=(\d+)$',text,re.M))
    close_waits=list(re.finditer(r'^CLOSE_WAIT elapsed_ms=(\d+) completion=([01]) destroy_event=([01]) destroyed=([01]) returned=([01]) result=(-?\d+) return_alive=([01]) alive=([01]) timed_out=([01]) wait_failed=([01]) last_error=(\d+) tid=(\d+)$',text,re.M))
    close_protocol=(len(close_events)==3 and [row.group(1) for row in close_events]==['WM_CLOSE','WM_DESTROY','WM_NCDESTROY'] and len(close_returns)==1 and len(close_waits)==1 and len(thread_rows)==1)
    if close_protocol:
        cr=tuple(map(int,close_returns[0].groups())); cw=tuple(map(int,close_waits[0].groups()))
        owner=int(thread_rows[0][0]); expected_waiter=owner if failure=='corrupt' else int(thread_rows[0][1])
        close_protocol &= all(int(row.group(2))==owner for row in close_events) and cr[:4]==(1,0,0,owner)
        close_protocol &= cw[0]<=3000 and cw[1:6]==(1,1,1,1,1) and cw[6:11]==(0,0,0,0,0) and cw[11]==expected_waiter
        close_protocol &= close_events[0].start()<close_events[1].start()<close_events[2].start()<close_returns[0].start()<close_waits[0].start()
        close_protocol &= all('CHECK '+name+' valid=1' in text for name in ('close_wait_handle_valid','close_wait_deadline','close_return_observed','close_return_success','close_return_hwnd_invalid'))
        if queued and failure!='corrupt':
            close_protocol &= text.find('CLOSE_QUEUED ')<close_events[0].start() and close_waits[0].start()<text.find('RELEASE_BEGIN\n')
    closes &= close_protocol
    queues = re.findall(r'^CLOSE_QUEUED submitted=8 query_hr=0x([0-9a-f]+) completed=([01]) explicit_wait_before_close=0$',text,re.M)
    queued_valid = len(queues)==(1 if queued else 0) and all(int(hr,16) in (0,1) for hr,_ in queues)
    phases = re.findall(r'^PHASE_END (\w+) presents=(\d+)$',text,re.M)
    presented = len(phases)==len(PHASES) and {name for name,_ in phases}==set(PHASES) and all(int(n)>0 for _,n in phases)
    target_rows = re.findall(r'^RESIZE_TARGET requested=(\d+)x(\d+) hr=0x([0-9a-f]+)$',text,re.M)
    target_valid = len(target_rows)==2 and [(int(w),int(h)) for w,h,_ in target_rows]==[(1280,720),(960,540)]
    observation_pattern = r'^TARGET_OBSERVATION stage=(before|after) requested=(\d+)x(\d+) client=(\d+)x(\d+) window=(-?\d+),(-?\d+),(-?\d+),(-?\d+) maxtrack=(\d+)x(\d+) virtual=(\d+)x(\d+) frame=(\d+)x(\d+)$'
    observations=list(re.finditer(observation_pattern,text,re.M))
    obs=[(match.group(1),*map(int,match.groups()[1:])) for match in observations]
    observed_valid=len(obs)==4 and [(r[0],r[1],r[2]) for r in obs]==[('before',1280,720),('after',1280,720),('before',960,540),('after',960,540)]
    target_matches=list(re.finditer(r'^RESIZE_TARGET requested=\d+x\d+ hr=0x[0-9a-f]+$',text,re.M))
    if observed_valid and len(target_matches)==2:
        observed_valid &= observations[0].start()<target_matches[0].start()<observations[1].start()<observations[2].start()<target_matches[1].start()<observations[3].start()
        observed_valid &= len({row[9:] for row in obs})==1
        observed_valid &= all(row[3]>0 and row[4]>0 and row[7]-row[5]>=row[3] and row[8]-row[6]>=row[4] and all(value>0 for value in row[9:]) for row in obs)
        if mode=='borderless':
            observed_valid &= all(row[3:9]==obs[0][3:9] for row in obs)
        else:
            observed_valid &= all(after[3:5]==after[1:3] for after in (obs[1],obs[3]))
            observed_valid &= obs[2][3:9]==obs[1][3:9]
            observed_valid &= all(before[1]+before[7]-before[5]-before[3]<=before[9] and before[2]+before[8]-before[6]-before[4]<=before[10] for before in (obs[0],obs[2]))
    else: observed_valid=False
    target_valid &= observed_valid
    if mode=='borderless':
        target_valid &= text.count('CHECK target_truthful_native_client_result valid=1')==2
        target_valid &= text.count('CHECK target_preserves_native_borderless_geometry valid=1')==2
        target_valid &= all(int(hr,16) in (0,0x887a0004) for _,_,hr in target_rows)
        clients=re.findall(r'^GEOMETRY client=(\d+)x(\d+) ',text,re.M)
        target_valid &= len(clients)==len(PHASES) and len(set(clients))==1
        if observed_valid:
            cw,ch=obs[0][3:5]
            target_valid &= all(int(hr,16)==(0 if (int(w),int(h))==(cw,ch) else 0x887a0004) for w,h,hr in target_rows)
            target_valid &= all((int(w),int(h))==(cw,ch) for w,h in clients)
    else:
        target_valid &= all(int(hr,16)==0 for _,_,hr in target_rows)
        target_valid &= text.count('CHECK target_observed_client_matches_request valid=1')==2
        target_valid &= text.count('CHECK target_fits_default_maxtrack valid=1')==2
    common = natural and modules_bound and closes and topology
    no_unexpected_failure = all(marker not in text for marker in ('PRESENT_NOT_VISIBLE_SUCCESS','FIXTURE_FAILURE','FIXTURE_SETUP_FAILURE'))
    full = pixel_set and pixel_values and dimensions_valid and presented and target_valid and queued_valid
    full &= 'CHECK unsupported_resize_rejected valid=1' in text
    full &= text.count('CHECK windowed_no_exclusive valid=1')==len(PHASES)
    if mode=='borderless': full &= text.count('CHECK native_borderless_geometry valid=1')==len(PHASES)
    if failure=='corrupt':
        corrupt_rows = (len(rows)==9 and set(keys)=={('low_initial',i) for i in range(9)} and
            all(tuple(map(int,r[4:8]))==GOLDEN[int(r[1])] and r[8]==('0' if int(r[1])==0 else '1') for r in rows))
        release_markers=('RELEASE_BEGIN_FAILURE','RELEASE_END_FAILURE','EVENT WM_CLOSE tid=','CHECK normal_close_destroy_quit valid=1')
        released=all(marker in text for marker in release_markers) and [text.find(marker) for marker in release_markers]==sorted(text.find(marker) for marker in release_markers)
        accepted = common and corrupt_rows and released and returncode==24 and 'CHECK gpu_spatial_pattern valid=0' in text and 'FIXTURE_FAILURE code=24' in text and 'SMOKE_PASS' not in text
        state='EXPECTED_READBACK_REJECTION' if accepted else 'FAIL_OR_INCONCLUSIVE'
    elif failure=='controlled':
        accepted = common and full and no_unexpected_failure and returncode==23 and 'CONTROLLED_FAILURE after_gpu_and_cleanup exit=23' in text and 'SMOKE_PASS' not in text and 'valid=0' not in text
        state='EXPECTED_CONTROLLED_FAILURE' if accepted else 'FAIL_OR_INCONCLUSIVE'
    else:
        accepted = common and full and no_unexpected_failure and returncode==0 and 'SMOKE_PASS wave=7 spatial_gpu_readback=1 actual_texture_dimensions=1' in text and 'valid=0' not in text
        state='PASS' if accepted else 'FAIL_OR_INCONCLUSIVE'
    return {'state':state,'naturalExitBeforeCleanup':natural,'returncodeBeforeCleanup':returncode,
            'successfulReadbackPoints':sum(r[8]=='1' for r in rows),'expectedReadbackPoints':81,
            'spatialGoldenValuesValid':pixel_values,'completeSpatialPointSet':pixel_set,
            'targetGeometryObservationsValid':bool(observed_valid),'targetGeometryObservations':[list(row) for row in obs],
            'actualAndPublicDimensionsValid':bool(dimensions_valid),'presentedAllPhases':presented,'targetResultsValid':target_valid,
            'closeCompletionProtocolValid':bool(close_protocol),'threadTopologyValid':topology,'queuedCloseObservationValid':queued_valid,'normalCloseObserved':closes,
            'modules':modules,'modulePathsBoundToPrivateCandidates':bool(modules_bound),
            'scope':'Actual private procedural-fixture GPU evidence only; no game, FPS uplift, exclusive-mode, or installed runtime acceptance'}
