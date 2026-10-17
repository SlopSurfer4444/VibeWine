#!/usr/bin/env python3
from pathlib import Path
import argparse,hashlib,json,re,subprocess
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=root.parents[1]/'dxgi-source');args=p.parse_args();source=args.source.resolve()
s=(source/'src/dxgi/dxgi_output.cpp').read_text()
def extract(token):
 start=s.index(token);begin=s.index('{',start);depth=1;i=begin+1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
config=(source/'src/util/config/config.cpp').read_text()
profile=re.search(r'R"\(([^\n]+)\)"[^\n]*\n\s*\{ "dxgi.limitDisplayModeResolutions", "True"',config).group(1)
assert 'std::regex::extended | std::regex::icase' in config
assert 'config.getOption<bool>("dxgi.limitDisplayModeResolutions", false)' in (source/'src/dxgi/dxgi_options.cpp').read_text()
# Public deterministic fixture: 37 sizes, each at 16/24/32 bpp (111 entries).
sizes=[(640+i*40,360+i*24) for i in range(35)]+[(1920,1080),(2560,1440)]
synthetic111=[dict(width=w,height=h,bits_per_pixel=bpp,refresh_hz=60) for w,h in sizes for bpp in (16,24,32)]
assert len(synthetic111)==111
fixture=(root/'fixture.cpp').read_text().replace('HEADER_PATH',str(source/'src/dxgi/dxgi_mode_limit.hpp'))
fixture=fixture.replace('// METHOD1','HRESULT '+extract('  GetDisplayModeList1(')).replace('// METHOD0','HRESULT '+extract('  GetDisplayModeList(')).replace('// CONVERT',extract('DXGI_MODE_DESC1 ConvertDisplayMode('))
fixture=fixture.replace('PROFILE_REGEX',profile)
fixture=fixture.replace('// SYNTHETIC111',','.join('{%d,%d,%d,{%d,1},false}'%(m['width'],m['height'],m['bits_per_pixel'],m['refresh_hz']) for m in synthetic111))
(root/'generated.cpp').write_text(fixture)
cmd=['clang++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(root/'generated.cpp'),'-o',str(root/'mode-tests')]
c=subprocess.run(cmd,text=True,capture_output=True);(root/'build.log').write_text(c.stdout+c.stderr)
if c.returncode:print(c.stderr);raise SystemExit(c.returncode)
r=subprocess.run([str(root/'mode-tests')],text=True,capture_output=True,timeout=30)
files=['src/dxgi/dxgi_mode_limit.hpp','src/dxgi/dxgi_output.cpp','src/dxgi/dxgi_options.cpp','src/dxgi/dxgi_options.hpp','src/util/config/config.cpp']
result={'source':str(source),'hashes':{f:hashlib.sha256((source/f).read_bytes()).hexdigest() for f in files},'fixture_sha256':hashlib.sha256((root/'fixture.cpp').read_bytes()).hexdigest(),'modeInput':'deterministic synthetic111 (not observed hardware)','compile_command':cmd,'returncode':r.returncode,'stdout':r.stdout,'stderr':r.stderr,'wineStarted':False,'rendererBuild':False,'installedChanges':False}
(root/'result.json').write_text(json.dumps(result,indent=2)+'\n');(root/'run.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='');raise SystemExit(r.returncode)
