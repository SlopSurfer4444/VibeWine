#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate by default; --run starts exactly one bounded native GUI child."""
import argparse,hashlib,json,os,signal,subprocess,time,uuid
from pathlib import Path
G=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--arch',choices=['arm64','x86_64'],required=True);p.add_argument('--run',action='store_true');p.add_argument('--wait-for-start',action='store_true');a=p.parse_args()
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
manifest=json.loads((G/'manifest.json').read_text())
for relative,digest in manifest['bindings'].items():
 if sha(G/relative)!=digest:raise RuntimeError('frozen file changed: '+relative)
app=G/('Native Present '+a.arch+'.app');exe=app/'Contents/MacOS/fixture'
if not a.run:print(json.dumps(dict(state='VALIDATED_NOT_LAUNCHED',architecture=a.arch,executable=str(exe))));raise SystemExit(0)
sessions=G/'sessions';sessions.mkdir(exist_ok=True,mode=0o700);session=sessions/(a.arch+'-'+uuid.uuid4().hex);session.mkdir(mode=0o700)
for c in ['"','\\','\n']:
 if c in str(session):raise RuntimeError('unsupported sandbox path')
policy=session/'isolation.sb';policy.write_text('(version 1)\n(allow default)\n(deny network*)\n(deny file-write* (require-all (require-not (subpath "'+str(session)+'")) (require-not (literal "/dev/null"))))\n')
fd=os.open(session/'run.claim',os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600);os.write(fd,str(os.getpid()).encode());os.close(fd)
child=None;start=time.monotonic();result=dict(state='RUNNING',architecture=a.arch,session=str(session),manifestSha256=sha(G/'manifest.json'),childDeadlineSeconds=55 if a.wait_for_start else 25,waitForStart=a.wait_for_start,actionsInjected=False)
def interrupted(*args):raise InterruptedError('supervisor signal')
for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,interrupted)
try:
 env=dict(os.environ)
 for key in ['MTL_HUD_ENABLED','DXMT_CS2_TRACE_DIR']:env.pop(key,None)
 with (session/'console.log').open('xb') as log:
  previous=signal.pthread_sigmask(signal.SIG_BLOCK,{signal.SIGINT,signal.SIGTERM})
  try:child=subprocess.Popen(['/usr/bin/sandbox-exec','-f',str(policy),str(exe),str(session/'events.jsonl')]+(['--wait-for-start'] if a.wait_for_start else []),cwd=session,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=log,start_new_session=True)
  finally:signal.pthread_sigmask(signal.SIG_SETMASK,previous)
  print(json.dumps(dict(state='NATIVE_FIXTURE_DISPATCHED',pid=child.pid,session=str(session),app=str(app))),flush=True)
  child.wait(timeout=55 if a.wait_for_start else 25)
 result.update(state='CHILD_EXITED',childReturnCode=child.returncode)
except BaseException as error:result.update(state='INTERRUPTED_OR_TIMEOUT',error=repr(error))
finally:
 # Own retained child only. Process exit closes its own window; never kill by name.
 previous=signal.pthread_sigmask(signal.SIG_BLOCK,{signal.SIGINT,signal.SIGTERM})
 try:
  if child is not None and child.poll() is None:
   child.kill()
   try:child.wait(timeout=2)
   except subprocess.TimeoutExpired:pass
  result.update(elapsedSeconds=time.monotonic()-start,childPid=child.pid if child else None,childReturnCode=child.poll() if child else None,cleanupVerified=child is None or child.poll() is not None)
  path=session/'events.jsonl'
  if path.exists() and path.stat().st_size<=4*1024*1024:
   try:
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    result['eventCount']=len(rows);result['summaryPresent']=bool(rows and rows[-1].get('event')=='SUMMARY')
    result['phasesCompleted']=bool(result['summaryPresent'] and rows[-1].get('phasesStarted') is True and rows[-1].get('settledPhaseMask')==7 and rows[-1].get('intendedExitCode')==0)
    result['rawSha256']=sha(path)
   except Exception as error:result['parseError']=repr(error)
  result['acceptance']='No automatic presentation/visibility acceptance. Correlate phases, own window state and valid positive native timestamps. Callback count is not frame count.'
  (session/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 finally:signal.pthread_sigmask(signal.SIG_SETMASK,previous)
print(json.dumps(result))
raise SystemExit(0 if result['state']=='CHILD_EXITED' and result.get('childReturnCode')==0 and result.get('cleanupVerified') and result.get('summaryPresent') and result.get('phasesCompleted') else 1)
