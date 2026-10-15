#!/usr/bin/env python3
"""Run extracted production acquisition/release seams without any GUI/display call."""
from pathlib import Path
import subprocess,json,hashlib,sys
root=Path(__file__).resolve().parents[1]; out=Path(__file__).resolve().parent
negative='--baseline-negative' in sys.argv
source_root=root/('baseline' if negative else 'source')
if negative: out=out/'baseline-negative'; out.mkdir(exist_ok=True)

def block(src, marker):
 start=src.index(marker); brace=src.index('{',start);depth=1;i=brace+1
 while depth:
  if src[i]=='{':depth+=1
  elif src[i]=='}':depth-=1
  i+=1
 return src[start:i]
bridge=(source_root/'bridge/src/winemetal/unix/winemetal_ds3.c').read_text()
fn=block(bridge,'static NTSTATUS\n_CreateMetalViewFromHWND')
types=bridge[bridge.index('typedef struct opaque_HWND *HWND;'):bridge.index('static NTSTATUS\n_CreateMetalViewFromHWND')]
c='''#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef int BOOL; typedef int NTSTATUS; typedef uintptr_t obj_handle_t;
typedef void *macdrv_window,*macdrv_view,*macdrv_metal_device,*macdrv_metal_view,*macdrv_metal_layer;
typedef void (*dispatch_block_t)(void);
typedef void CAMetalLayer;
static int on_main;
#define execute_on_main_checked(b) (scenario==7 ? 0 : ({ assert(!on_main); on_main=1; b(); on_main=0; 1; }))
static int w7ds_prime_layer_main(CAMetalLayer *layer);
#define STATUS_SUCCESS 0
#define RTLD_DEFAULT ((void*)0)
struct unixcall_create_metal_view_from_hwnd {uintptr_t hwnd,device; obj_handle_t ret_view,ret_layer;};
'''+types+'''
static int scenario, locks, acquisitions, releases;
static int w7ds_prime_layer_main(CAMetalLayer *layer) { assert(on_main && layer); return scenario!=6; }
static struct macdrv_win_data data;
static struct macdrv_win_data *get_data(HWND h) { (void)h; if (scenario==1) return NULL; locks++; return &data; }
static void put_data(struct macdrv_win_data *p) { assert(p==&data); locks--; }
static macdrv_metal_view acquire(macdrv_view v,macdrv_metal_device d) { assert(v); (void)d; acquisitions++; return scenario==3?NULL:(void*)3; }
static macdrv_metal_layer layer(macdrv_metal_view v) { assert(!on_main && v==(void*)3); return scenario==4?NULL:(void*)4; }
static void release_view(macdrv_metal_view v) { assert(v==(void*)3); releases++; }
static struct macdrv_functions_t table = { .get_win_data=get_data,.release_win_data=put_data,.macdrv_view_create_metal_view=acquire,.macdrv_view_get_metal_layer=layer,.macdrv_view_release_metal_view=release_view };
static void *dlsym(void *scope,const char *name) { (void)scope; return scenario==0?NULL:(!strcmp(name,"macdrv_functions")?&table:NULL); }
'''+fn+'''
int main(void) {
 for(scenario=0;scenario<=7;scenario++) {
  struct unixcall_create_metal_view_from_hwnd p={1,2,0xdead,0xbeef};
  data.client_cocoa_view=scenario==2?NULL:(void*)1; acquisitions=releases=locks=0;
  assert(_CreateMetalViewFromHWND(&p)==STATUS_SUCCESS); assert(locks==0);
  if (scenario==5) { assert(p.ret_view==3 && p.ret_layer==4); assert(acquisitions==1 && releases==0); }
  else { assert(!p.ret_view && !p.ret_layer); assert(releases==(scenario==4 || scenario==6 || scenario==7)); }
 }
 puts("PASS bridge: missing symbols, dead HWND, absent client view, failed acquire, failed layer/snapshot cleanup, successful lease");
}
'''
(out/'bridge.c').write_text(c)
src=(source_root/'winemac.drv/cocoa_window.m').read_text();fn=block(src,'    - (WineMetalView*) newMetalViewWithDevice:(id<MTLDevice>)device\n    {');release=block(src,'void macdrv_view_release_metal_view(macdrv_metal_view v)')
m='''#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdio.h>
@protocol MTLDevice @end
#define NSViewWidthSizable 1
#define NSViewHeightSizable 2
#define NSWindowBelow 0
#define OnMainThread(b) b()
typedef void *macdrv_metal_view;
static int created,destroyed,fail_init;
@class WineContentView;
@interface WineWindow:NSObject
@property BOOL closing;
- (void)windowDidDrawContent;
@end
@implementation WineWindow
- (void)windowDidDrawContent {}
@end
@interface WineMetalView:NSObject
@property(assign) WineContentView *owner;
- (id)initWithFrame:(NSRect)r device:(id<MTLDevice>)d;
- (void)setAutoresizingMask:(int)m;
- (void)removeFromSuperview;
@end
@interface WineContentView:NSObject { @public WineMetalView *_metalView; }
@property(assign) WineWindow *window;
- (NSRect)bounds;
- (void)setAutoresizesSubviews:(BOOL)b;
- (void)addSubview:(WineMetalView*)v positioned:(int)p relativeTo:(id)x;
- (WineMetalView*)newMetalViewWithDevice:(id<MTLDevice>)d;
@end
@implementation WineMetalView
- (id)initWithFrame:(NSRect)r device:(id<MTLDevice>)d { (void)r;(void)d; self=[super init]; if(self){created++; if(fail_init){[self release];return nil;}} return self; }
- (void)setAutoresizingMask:(int)m { (void)m; }
- (void)removeFromSuperview { if(self.owner) { self.owner->_metalView=nil; self.owner=nil; [self release]; } }
- (void)dealloc { destroyed++; [super dealloc]; }
@end
@implementation WineContentView
- (NSRect)bounds {return NSZeroRect;}
- (void)setAutoresizesSubviews:(BOOL)b {(void)b;}
- (void)addSubview:(WineMetalView*)v positioned:(int)p relativeTo:(id)x {(void)p;(void)x;v.owner=self;[v retain];}
'''+fn+'''
@end
'''+release+'''
int main(void) { @autoreleasepool {
 WineWindow *w=[WineWindow new]; WineContentView *v=[WineContentView new]; v.window=w;
 id<MTLDevice> device=(id<MTLDevice>)[NSObject new];
 WineMetalView *a=[v newMetalViewWithDevice:device]; assert(a);
 assert(![v newMetalViewWithDevice:device]); assert(v->_metalView==a); assert(!destroyed);
 macdrv_view_release_metal_view(a); assert(!v->_metalView); assert(destroyed==1);
 WineMetalView *b=[v newMetalViewWithDevice:device]; assert(b);
 /* Detach old native generation while its consumer still owns it. */
 [b removeFromSuperview]; assert(!v->_metalView);
 WineMetalView *c=[v newMetalViewWithDevice:device]; assert(c && c!=b);
 macdrv_view_release_metal_view(b); assert(v->_metalView==c);
 w.closing=YES; assert(![v newMetalViewWithDevice:device]);
 macdrv_view_release_metal_view(c); assert(![v newMetalViewWithDevice:device]);
 w.closing=NO; v.window=nil; assert(![v newMetalViewWithDevice:device]);
 v.window=w; assert(![v newMetalViewWithDevice:nil]);
 fail_init=1; assert(![v newMetalViewWithDevice:device]); assert(!v->_metalView);
 assert(created==destroyed); [device release];[v release];[w release];
 puts("PASS native lease: reject overlap, reacquire, detached generation isolation, closing, missing window/device, allocation failure, balanced ownership");
} }
'''
(out/'lease.m').write_text(m)
results=[]
for name,flags in [('bridge',[]),('lease',['-fblocks','-framework','Foundation'])]:
 source=out/(name+('.c' if name=='bridge' else '.m')); exe=out/name
 cmd=['/usr/bin/clang','-g','-O1','-fsanitize=address,undefined','-fblocks','-Wall','-Wextra',str(source),'-o',str(exe)]+flags
 p=subprocess.run(cmd,text=True,capture_output=True); (out/(name+'-build.log')).write_text(p.stdout+p.stderr); assert not p.returncode,p.stderr
 p=subprocess.run([str(exe)],text=True,capture_output=True); (out/(name+'-run.log')).write_text(p.stdout+p.stderr); assert bool(p.returncode)==negative,p.stdout+p.stderr;print(('EXPECTED FAILURE '+name+'\n') if negative else p.stdout,end='');results.append({'name':name,'exit':p.returncode})
(out/'result.json').write_text(json.dumps({'results':results,'sourceSha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source_root/'winemac.drv/cocoa_window.m',source_root/'bridge/src/winemetal/unix/winemetal_ds3.c']},'guiActions':False,'sanitizers':['address','undefined'],'scope':'extracted actual production functions with mocked native ownership/driver lookup'},indent=2)+'\n')
