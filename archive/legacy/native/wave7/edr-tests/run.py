from pathlib import Path
import json,hashlib,subprocess
root=Path(__file__).resolve().parents[1];out=Path(__file__).resolve().parent
src=root/'source/winemac.drv/wave7_display_edr.inc'
subprocess.run(['clang','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(src.parent),str(out/'test_edr.m'),'-framework','Foundation','-o',str(out/'test_edr')],check=True)
r=subprocess.run([str(out/'test_edr')],text=True,capture_output=True,check=True);print(r.stdout,end='')
app=(src.parent/'cocoa_app.m').read_text();main=(src.parent/'cocoa_main.m').read_text();init=app[app.index('    - (id) init'):app.index('    - (void) dealloc')];assert init.index('if (!macdrv_refresh_display_edr_main())')>init.index('[self applicationDidBecomeActive:nil]');startup=main[main.index('static void run_cocoa_app'):];assert startup.index('[WineApplicationController sharedController]')<startup.index('unlockWithCondition:COCOA_APP_RUNNING');assert 'if (controller)' in startup
(out/'result.json').write_text(json.dumps({'exit':0,'result':r.stdout.strip(),'sourceSha256':hashlib.sha256(src.read_bytes()).hexdigest(),'startupOrdering':'source verified: controller init snapshot before COCOA_APP_RUNNING; nil controller cannot report success','realAppKitOrDisplay':False},indent=2)+'\n')
