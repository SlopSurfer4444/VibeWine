#!/usr/bin/env python3
from pathlib import Path
import subprocess,sys
p=Path(__file__).resolve().parent
source=Path(sys.argv[1]) if len(sys.argv)>1 else p.parents[1]/'source/dlls/winemac.drv/cocoa_display.m'
s=source.read_text(); a=s.index('int macdrv_get_monitors('); b=s.index('\n}',s.index('void macdrv_free_monitors(',a))+2
(p/'extracted.m').write_text((p/'prefix.m').read_text()+s[a:b]+(p/'tests.m').read_text())
subprocess.run(['clang','-g','-fsanitize=address','-fno-omit-frame-pointer','-fno-objc-arc','-framework','Foundation',str(p/'extracted.m'),'-o',str(p/'fixture')],check=True)
failed=False
for case in ['growth','fail-first','fail-second','edid-malloc-fail','empty-edid']:
 r=subprocess.run([str(p/'fixture'),case]); failed |= bool(r.returncode)
sys.exit(failed)
