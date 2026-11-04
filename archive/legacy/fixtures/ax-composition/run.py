from pathlib import Path
import re, subprocess, tempfile, json
root=Path(__file__).resolve().parent
wave=root.parent
src=wave.parent/'source/dlls/winemac.drv'
text=(src/'ax_provider.inc').read_text()
def extract(name):
 m=re.search(r'^static [^\n]*\b'+name+r'\(',text,re.M); a=m.start(); b=text.index('{',m.end()); depth=1;c=b+1
 while depth:
  if text[c]=='{':depth+=1
  elif text[c]=='}':depth-=1
  c+=1
 return text[a:c]+'\n'
prefix=(wave.parent/'tests/provider/harness.c').read_text().split('/* ACTUAL_FUNCTIONS */')[0]
header=root/'oleacc-constants.h'
constants='\n'.join(l for l in header.read_text().splitlines() if re.match(r'#define (ROLE_SYSTEM_|STATE_SYSTEM_)[A-Z_]+\s+',l))
prefix=prefix.replace('/* ACTUAL_PROTOCOL */','#include "'+str(src/'ax_protocol.h')+'"').replace('/* ACTUAL_CONSTANTS */',constants)
prefix=prefix.replace('static uint64_t GetTickCount64(void){return now;}','extern uint64_t bridge_time(void);\nstatic uint64_t GetTickCount64(void){return bridge_time();}')
prefix=prefix.replace('static BOOL ax_lease_valid(const struct macdrv_ax_request *r){lease_checks++;return lease && !ax_provider_abandoned;}','extern int bridge_validate(const struct macdrv_ax_request *r);\nstatic BOOL ax_lease_valid(const struct macdrv_ax_request *r){return bridge_validate(r);}')
extra='''
static IAccessible object;
struct ax_walk { const struct macdrv_ax_request *request; struct macdrv_ax_snapshot *snapshot; struct ax_cache *cache,*previous; HWND resolved_hwnds[64]; unsigned int resolved_count; BOOL expired; };
static struct ax_cache *ax_cache_get(uint32_t hwnd) { return &ax_caches[0]; }
static void ax_cache_clear(struct ax_cache *cache) { memset(cache,0,sizeof(*cache)); }
static BOOL ax_acquire_client(const struct macdrv_ax_request *r, HWND h, IAccessible **out, HWND *standard, uint32_t *cookie) { *out=&object; *standard=0; *cookie=0; return TRUE; }
/* Traversal/COM boundary double. Binding-table ownership and revision switching
 * are executed by the unmodified ax_snapshot function. */
static void ax_visit(struct ax_walk *walk, IAccessible *o, VARIANT child, uint32_t parent, unsigned depth, HWND hwnd, uint32_t cookie) {
 struct ax_binding *b=&walk->cache->bindings[0]; b->object=o;b->role=MACDRV_AX_ROLE_BUTTON;b->child=child;b->token=100;
 walk->cache->count=walk->snapshot->count=1;
 struct macdrv_ax_node *n=&walk->snapshot->nodes[0];n->id=1;n->token=100;n->role=MACDRV_AX_ROLE_BUTTON;n->flags=MACDRV_AX_ENABLED|MACDRV_AX_PRESSABLE;
}
'''
functions='\n'.join(extract(n) for n in ['ax_role_state','ax_has_default_action','ax_press_role','ax_action','ax_snapshot'])
api='''
void provider_init(void) { memset(&object,0,sizeof(object));object.role=ROLE_SYSTEM_PUSHBUTTON; object.role_vt=object.state_vt=VT_I4;object.default_nonempty=TRUE; }
void provider_snapshot(const struct macdrv_ax_request *r,struct macdrv_ax_snapshot *s) { ax_snapshot(r,s); }
uint32_t provider_action(const struct macdrv_ax_request *r) { return ax_action(r); }
unsigned int provider_effects(void) { return object.presses; }
uint64_t provider_revision(void) { return ax_caches[0].revision; }
'''
native=(wave/'ax/refresh-deadline.m').read_text().split('int main(')[0]
native=native.replace('#define AX_SOURCE "../../source/dlls/winemac.drv/ax_native.inc"','#define AX_SOURCE "'+str(src/'ax_native.inc')+'"')
main=(root/'scenario.inc').read_text()
results=[]
with tempfile.TemporaryDirectory(prefix='ax-composition-') as tmp:
 t=Path(tmp);(t/'provider.c').write_text(prefix+extra+functions+api);(t/'native.m').write_text(native+main)
 common=['xcrun','clang','-fsanitize=address,undefined','-Wno-unused-function','-I'+str(src)]
 subprocess.run(common+['-c',str(t/'provider.c'),'-o',str(t/'provider.o')],check=True)
 for version in ['before','after']:
  binary=t/version
  args=common+['-fblocks','-framework','Cocoa']
  if version=='before':args+=['-DAX_SOURCE="'+str(wave/'ax/ax_native.before.inc')+'"']
  subprocess.run(args+[str(t/'native.m'),str(t/'provider.o'),'-o',str(binary)],check=True)
  p=subprocess.run([str(binary)],capture_output=True,text=True);results.append(dict(version=version,code=p.returncode,stdout=p.stdout,stderr=p.stderr))
(root/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
assert results[0]['code'] != 0 and results[1]['code']==0
