#!/usr/bin/env python3
"""Exercise actual Wine relative-path helper with expanded build constants; no Wine launch."""
from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
p=argparse.ArgumentParser(description=__doc__)
for name in ('source','flags','ntdll','engine','output'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--compiler',default='/usr/bin/clang')
a=p.parse_args()
for name in ('source','flags','ntdll','engine','output'):setattr(a,name,getattr(a,name).resolve())
for source in (a.source,a.engine):
 if a.output==source or source in a.output.parents:p.error('Output must be separate from input trees')
a.output.mkdir(parents=True,exist_ok=False)
text=(a.source/'unix/loader.c').read_text();begin=text.index('static char *build_relative_path(');brace=text.index('{',begin);depth=1;end=brace+1
while depth:
 if text[end]=='{':depth+=1
 elif text[end]=='}':depth-=1
 end+=1
function=text[begin:end]
source=a.output/'runtime-paths.c';source.write_text('#include <stdlib.h>\n#include <string.h>\n#include <stdio.h>\n#include <limits.h>\n#include <unistd.h>\n'+function+r'''
int main(int argc,char **argv)
{
 char base[PATH_MAX],normalized_bin[PATH_MAX],normalized_data[PATH_MAX],check[PATH_MAX];
 if(argc!=2)return 64;
 snprintf(base,sizeof(base),"%s/lib/wine",argv[1]);
 char *bin=build_relative_path(base,LIBDIR "/wine",BINDIR);
 char *data=build_relative_path(base,LIBDIR "/wine",DATADIR "/wine");
 if(!realpath(bin,normalized_bin)||!realpath(data,normalized_data)||SYSTEMDLLPATH[0])return 65;
 printf("%s\n%s\n%s\n",normalized_bin,normalized_data,SYSTEMDLLPATH);
 const char *files[]={"nls/normnfc.nls","nls/locale.nls","nls/l_intl.nls","wine.inf"};
 for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++){snprintf(check,sizeof(check),"%s/%s",normalized_data,files[i]);if(access(check,R_OK))return 66;}
 snprintf(check,sizeof(check),"%s/wineserver",normalized_bin);if(access(check,X_OK))return 67;
 snprintf(check,sizeof(check),"%s/lib/wine/x86_64-windows/ntdll.dll",argv[1]);if(access(check,R_OK))return 68;
 free(bin);free(data);return 0;
}
''')
flags=json.loads((a.flags).read_text());binary=a.output/'runtime-paths';cmd=[a.compiler,'-Wall','-Wextra','-Werror','-O2',*flags.values(),str(source),'-o',str(binary)]
q=subprocess.run(cmd,capture_output=True,text=True,timeout=30);assert q.returncode==0,q.stderr
r=subprocess.run([str(binary),str(a.engine)],capture_output=True,text=True,timeout=3);assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
lines=r.stdout.splitlines();assert lines[0]==str((a.engine/'bin').resolve()) and lines[1]==str((a.engine/'share/wine').resolve()) and lines[2]==''
ntdll=a.ntdll;bad=[x for x in [b'${bindir}',b'${libdir}',b'${datadir}',b'${system_dllpath}'] if x in ntdll.read_bytes()];assert not bad,bad
report=dict(state='SOURCE_RELATIVE_RUNTIME_PATHS_PASS',rebuild=str(a.output),engine=str(a.engine),bin=lines[0],data=lines[1],systemDllPath=lines[2],resources=['normnfc.nls','locale.nls','l_intl.nls','wine.inf','wineserver','x86_64-windows/ntdll.dll'],unexpandedBuildPlaceholdersAbsent=True,ntdllSha256=hashlib.sha256(ntdll.read_bytes()).hexdigest(),argv=cmd,wineLaunched=False)
(a.output/'runtime-path-checks.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
