#!/usr/bin/env python3
"""Print recorded command templates; --execute explicitly compiles into fresh output."""
from pathlib import Path
import argparse,json,re,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('template',type=Path)
p.add_argument('--input',action='append',default=[],metavar='NAME=PATH')
p.add_argument('--execute',action='store_true')
a=p.parse_args();recipe=json.loads(a.template.read_text());values={}
for item in a.input:
 name,sep,value=item.partition('=')
 if not sep or name in values:p.error('Inputs must be unique NAME=PATH pairs')
 values[name]=str(Path(value).expanduser().resolve())
needed=set(re.findall(r'\$\{([A-Z_]+)\}',json.dumps(recipe)))
if needed-set(values):p.error('Missing inputs: '+', '.join(sorted(needed-set(values))))
def expand(x):
 if isinstance(x,str):return re.sub(r'\$\{([A-Z_]+)\}',lambda m:values[m[1]],x)
 if isinstance(x,list):return [expand(v) for v in x]
 if isinstance(x,dict):return {k:expand(v) for k,v in x.items()}
 return x
resolved=expand(recipe)
if not a.execute:
 print(json.dumps(resolved,indent=2));raise SystemExit()
out=Path(resolved['outputDirectory']);root=Path(values['OUTPUT'])
# Refuse source/config/dependency roots as output, including descendants.
for name,value in values.items():
 if name in ('OUTPUT','WINE_PREFIX'):continue
 base=Path(value)
 if root==base or base in root.parents:p.error('OUTPUT must be separate from input roots')
out.mkdir(parents=True,exist_ok=False)
for name,contents in resolved.get('generatedFiles',{}).items():
 target=out/name
 if '..' in Path(name).parts or Path(name).is_absolute():p.error('Unsafe generated path')
 target.parent.mkdir(parents=True,exist_ok=True);target.write_text(contents)
for command in resolved['commands']:
 argv=command['argv'];target=Path(argv[argv.index('-o')+1])
 if out not in target.parents:p.error('Compiler output escapes fresh component directory')
 target.parent.mkdir(parents=True,exist_ok=True)
 subprocess.run(argv,cwd=command['cwd'],check=True)
(out/'resolved-recipe.json').write_text(json.dumps(resolved,indent=2)+'\n')
if 'resolvedPathFlags' in resolved:
 (out/'resolved-path-flags.json').write_text(json.dumps(resolved['resolvedPathFlags'],indent=2)+'\n')
print(out)
