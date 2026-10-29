#!/usr/bin/env python3
"""Prepare a source-bound native image/cost probe. Never executes it."""
import argparse,hashlib,importlib.util,json
from pathlib import Path
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[3]
spec=importlib.util.spec_from_file_location('adjacent_clear',HERE.parent/'run.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
contents=shared.shared.sources(REPO);s=shared.shared.generate(contents)
s=s[:s.index('static void dead_store_cases()')]
s='#import <Foundation/Foundation.h>\n#import <Metal/Metal.h>\n'+s
# The production helper's WMT native getters hit the actual Metal object here.
s=s.replace('return native_extents.at(handle).first;', 'return [(__bridge id<MTLTexture>)(void*)handle width];')
s=s.replace('return native_extents.at(handle).second;', 'return [(__bridge id<MTLTexture>)(void*)handle height];')
s+='\n'+(HERE/'fixture.mm.in').read_text()
a.output.mkdir(parents=True,exist_ok=False);source=a.output/'probe.mm';source.write_text(s)
command=['/usr/bin/clang++','-std=c++20','-O2','-g','-fobjc-arc','-DDXMT_DEAD_ATTACHMENT_STORES=1','-Wno-pragma-once-outside-header',*['-I'+str(REPO/q) for q in ('src/winemetal','src/dxmt','src/util','src/airconv','include')],str(source),'-framework','Foundation','-framework','Metal','-o',str(a.output/'probe')]
manifest={'scope':'source prepared only; compiler and GPU execution owned by parent','source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'production_sha256':{p:hashlib.sha256(s.encode()).hexdigest() for p,s in contents.items()},'build_command':command,'run_commands':[[str(a.output/'probe')],[str(a.output/'probe'),'--wrong-golden']]}
(a.output/'prepared.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
