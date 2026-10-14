"""Subprocess native decoding and immutable NDJSON analysis; no live mapping reads."""
import json,pathlib,subprocess
from analyze import analyze
base=pathlib.Path(__file__).resolve().parent
r=json.loads((base/'test-results.json').read_text())
p=subprocess.run([str(base/'cs2trace'),'decode',r['cli_sync_fixture'],'71',r['cli_sync_identity']],capture_output=True,text=True,check=True)
(base/'synthetic-sync.ndjson').write_text(p.stdout)
rows=[json.loads(s)for s in p.stdout.splitlines()];es=rows[:-1];assert len(es)==30 and rows[-1]['wrong_generation']==1
new=[e for e in es if 30<=e['type']<=33];assert len(new)==24
assert sum(e['probe_semantics_valid'] for e in new)==18
assert all(e['clock_domain']=='cpu_steady_ns' for e in new)
assert all(e['duration_ns'] is None for e in new if e['type']==33 or not e['probe_semantics_valid'])
assert {e['flags_name']for e in new if e['type']==33 and e['probe_semantics_valid']}=={'Unknown','Present','StagingMap','GetDataNotReady','ExplicitFlush','GPUIdle','FenceSignal','FenceWait','PassPromotion'}
for e in new:
 if e['type']==31:assert e['async_pointer']==1 and 'awaited_sequence'not in e
 if e['type']in(30,32):assert e['awaited_sequence']==1 and e['call_frame']==4
out=analyze(rows);sync=out['sync_probes'];assert len(sync['cpu_scopes'])==9 and len(sync['commit_reason_records'])==9
assert out['chunks'][0]['commit_reason']=='Unknown'
for e in sync['cpu_scopes']:
 if e['type']==31:assert e['awaited_chunk_if_observed']is None
 else:assert e['call_frame']==4 and e['awaited_chunk_if_observed']['frame']==3
assert out['rejections']['invalid_sync_flags']==3 and out['rejections']['invalid_sync_interval']==1
assert out['rejections']['invalid_commit_reason_code']==1 and out['rejections']['noninstant_commit_reason']==1
(base/'synthetic-sync-analysis.json').write_text(json.dumps(out,indent=2)+'\n')
(base/'sync-cli-results.json').write_text(json.dumps({'result':'pass','new_types':[30,31,32,33],'enum_codes_tested':list(range(9)),'valid_new_records':18,'rejected_semantic_records':6,'runtime_capture':False})+'\n')
print('Native decode -> immutable analyzer sync fixture passed')
