#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Translate prepared Wine DXBC with an explicitly supplied owned converter.

Consumes one compiler slot serially. Does not build/install a converter or use GPU.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile
from bounded_child import run as bounded_run

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--airconv',type=Path,required=True)
    p.add_argument('--corpus',type=Path,required=True)
    a=p.parse_args(); converter=a.airconv.resolve(); corpus=a.corpus.resolve()
    binary=converter.read_bytes(); manifest_bytes=(corpus/'corpus.json').read_bytes()
    manifest=json.loads(manifest_bytes); entries=manifest['entries']; assert len(entries)==5
    assert {e['name'] for e in entries}=={'blend-vs','blend-ps','mrt-vs','mrt-ps','noop-cs'}
    receipt={'converter':str(converter),'converter_sha256':sha(binary),'corpus_sha256':sha(manifest_bytes),
             'scope':'DXBC-to-AIR-only (no GPU)', 'commands':[]}
    # Reconcile an existing complete generation rather than overwriting it.
    assert not any((corpus/(e['name']+'.metallib')).exists() for e in entries), 'use a fresh prepared corpus attempt'
    with tempfile.TemporaryDirectory(prefix='metal4-air-',dir=corpus) as temporary:
        staged=[]
        for entry in entries:
            name=entry['name']; assert name in ['blend-vs','blend-ps','mrt-vs','mrt-ps','noop-cs']
            source=corpus/(name+'.dxbc'); data=source.read_bytes(); assert sha(data)==entry['sha256']
            target=corpus/(name+'.metallib'); output=Path(temporary)/(name+'.metallib')
            command=[str(converter),'-A',str(source),'-o',str(output)]
            result=bounded_run(command,timeout=30)
            assert result.returncode==0,(command,result.returncode,result.stderr.decode(errors='replace'))
            air=output.read_bytes(); assert 0<len(air)<1024*1024
            assert converter.read_bytes()==binary and source.read_bytes()==data,'consumed input changed'
            staged.append((output,target))
            receipt['commands'].append({'argv':command,'exit':result.returncode,'input_sha256':sha(data),
                  'output_sha256':sha(air),'output_bytes':len(air),
                  'stdout_sha256':sha(result.stdout),'stderr_sha256':sha(result.stderr)})
        assert (corpus/'corpus.json').read_bytes()==manifest_bytes
        assert converter.read_bytes()==binary
        for entry in entries:
            assert sha((corpus/(entry['name']+'.dxbc')).read_bytes())==entry['sha256']
        # No AIR is published for failed conversion attempts. Publication below is
        # serial and a mid-publication OS failure remains a manual held attempt.
        for source,target in staged:
            assert not target.exists(),'target appeared during translation'
        for source,target in staged:
            source.rename(target)
    assert (corpus/'corpus.json').read_bytes()==manifest_bytes
    receipt['status']='PASS';(corpus/'translation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'status':'PASS','translations':len(entries),'gpu':False}))

if __name__=='__main__':
    main()
