#!/usr/bin/env python3
from pathlib import Path
import subprocess,hashlib,json
out=Path(__file__).resolve().parent
src=out.parent/'source/winemac.drv'
def extract(s,marker):
 p=s.index(marker); b=s.index('{',p); i=b+1; d=1
 while d:
  if s[i]=='{':d+=1
  if s[i]=='}':d-=1
  i+=1
 return s[p:i]
app=(src/'cocoa_app.m').read_text(); event=(src/'cocoa_event.m').read_text(); window=(src/'cocoa_window.m').read_text()
perform=extract(app,'static void PerformRequest(void *info)\n{')
asyncfn=extract(app,'void OnMainThreadAsync(dispatch_block_t block)\n{')
syncfn=extract(event,'void OnMainThread(dispatch_block_t block)\n{')
getter=extract(window,'macdrv_metal_layer macdrv_view_get_metal_layer(macdrv_metal_view v)')
prefix=r'''
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/event.h>
#include <stdio.h>
static NSString *WineEventQueueThreadDictionaryKey=@"queue";
@interface MacDrvEvent:NSObject { @public void *event; } @end
@implementation MacDrvEvent @end
@interface WineEventQueue:NSObject { @public int kq; void (*event_handler)(void*); }
-(void)signalEventAvailable;
-(MacDrvEvent*)getEventMatchingMask:(int)mask;
@end
@implementation WineEventQueue
-(void)signalEventAvailable { abort(); }
-(MacDrvEvent*)getEventMatchingMask:(int)mask { (void)mask; abort(); }
@end
#define QUERY_EVENT 1
#define event_mask_for_type(x) (x)
@interface WineApplicationController:NSObject {
@public dispatch_queue_t requestsManipQueue; NSMutableArray *requests; CFRunLoopSourceRef requestSource;
}
+(id)sharedController;
@end
static WineApplicationController *controller;
@implementation WineApplicationController
+(id)sharedController {return controller;}
@end
'''
body=r'''
#include "../source/winemac.drv/wave7_main_executor.inc"
typedef void *macdrv_metal_layer,*macdrv_metal_view;
@interface WineMetalView:NSObject
@property(retain) CALayer *layer;
@end
@implementation WineMetalView
-(void)dealloc { [_layer release]; [super dealloc]; }
@end
'''+getter+r'''
static atomic_int done, calls, raw_submitted;
static int result;
static const CFStringRef querymode=CFSTR("WineAppWaitQueryResponseMode");
static void setup(void) {
 controller=[WineApplicationController new]; controller->requests=[NSMutableArray new];
 controller->requestsManipQueue=dispatch_queue_create("test.request-manip",DISPATCH_QUEUE_SERIAL);
 CFRunLoopSourceContext c={0}; c.perform=PerformRequest;
 controller->requestSource=CFRunLoopSourceCreate(NULL,0,&c);
 CFRunLoopAddSource(CFRunLoopGetMain(),controller->requestSource,kCFRunLoopCommonModes);
 CFRunLoopAddSource(CFRunLoopGetMain(),controller->requestSource,querymode);
}
static void wait_queued(void) {
 for (int i=0;i<10000;i++) {
  __block NSUInteger n; dispatch_sync(controller->requestsManipQueue,^{n=controller->requests.count;});
  if(n) return;
  usleep(100);
 }
 assert(!"worker did not enqueue");
}
static void pump(void) {
 for(int i=0;i<10000 && !atomic_load(&done);i++) CFRunLoopRunInMode(querymode,0.001,true);
 assert(atomic_load(&done));
}
static void *worker(void *arg) { @autoreleasepool {
 const char *mode=arg;
 if (!strcmp(mode,"getter")) {
  WineMetalView *view=[WineMetalView new]; view.layer=[CALayer layer];
  assert(macdrv_view_get_metal_layer(view)==view.layer); [view release]; result=1;
 } else {
 [[NSThread currentThread] threadDictionary][WineEventQueueThreadDictionaryKey]= [[[WineEventQueue alloc] init] autorelease];
 result=macdrv_run_on_main_v1(^{
  assert([NSThread isMainThread]); atomic_fetch_add(&calls,1);
  if (!strcmp(mode,"active")) {macdrv_main_executor_stop_v1(); assert(!atomic_load(&done));}
  if (!strcmp(mode,"exception")) [NSException raise:@"fixture" format:@"expected"];
 });
 }
 atomic_store(&done,1); return NULL;
} }
static void *raw_worker(void *arg) { (void)arg; @autoreleasepool {
 atomic_store(&raw_submitted,1);
 dispatch_sync(dispatch_get_main_queue(),^{atomic_fetch_add(&calls,1);}); atomic_store(&done,1); return NULL;
} }
static void *stress_worker(void *arg) { (void)arg; @autoreleasepool {
 for(int i=0;i<100;i++) assert(macdrv_run_on_main_v1(^{assert([NSThread isMainThread]);atomic_fetch_add(&calls,1);})==1);
 atomic_fetch_add(&done,1); return NULL;
} }
int main(int argc,char **argv) { @autoreleasepool {
 assert(argc==2); setup();
 assert(!macdrv_run_on_main_v1(^{abort();}));
 macdrv_main_executor_start_v1();
 assert(macdrv_run_on_main_v1(^{assert([NSThread isMainThread]);})==1);
 if (!strcmp(argv[1],"raw-comparison")) {
  pthread_t t; assert(!pthread_create(&t,NULL,raw_worker,NULL));
  while(!atomic_load(&raw_submitted)) usleep(100);
  for(int i=0;i<250 && !atomic_load(&done);i++) CFRunLoopRunInMode(querymode,0.001,true);
  BOOL rawInCustom=atomic_load(&done);
  for(int i=0;i<10000 && !atomic_load(&done);i++) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);
  assert(atomic_load(&done)); pthread_join(t,NULL);
  printf("OBSERVED raw dispatch serviced in custom mode: %s\n",rawInCustom ? "yes" : "no");
  assert(!rawInCustom); macdrv_main_executor_stop_v1(); return 0;
 }
 if (!strcmp(argv[1],"stress")) {
  pthread_t threads[32];
  for(int i=0;i<32;i++) assert(!pthread_create(&threads[i],NULL,stress_worker,NULL));
  while(atomic_load(&done)<32) CFRunLoopRunInMode(querymode,0.001,true);
  for(int i=0;i<32;i++) pthread_join(threads[i],NULL);
  assert(atomic_load(&calls)==3200); macdrv_main_executor_stop_v1();
  puts("PASS 3200 concurrent synchronous requests, 32 workers"); return 0;
 }
 if (!strcmp(argv[1],"nested-negative")) {
  WineMetalView *v=[WineMetalView new]; v.layer=[CALayer layer];
  macdrv_run_on_main_v1(^{ macdrv_view_get_metal_layer(v); });
  abort();
 }
 pthread_t t; assert(!pthread_create(&t,NULL,worker,argv[1])); wait_queued();
 assert(!atomic_load(&done));
 if (!strcmp(argv[1],"queued")) {
  macdrv_main_executor_stop_v1(); pthread_join(t,NULL); assert(!result && !atomic_load(&calls));
  PerformRequest(NULL); assert(!atomic_load(&calls));
 } else {
  pump(); pthread_join(t,NULL);
  assert(result==strcmp(argv[1],"exception")!=0);
  assert(atomic_load(&calls)==strcmp(argv[1],"getter")!=0);
 }
 macdrv_main_executor_stop_v1();
 assert(!macdrv_run_on_main_v1(^{abort();}));
 macdrv_main_executor_start_v1(); assert(!macdrv_run_on_main_v1(^{abort();}));
 CFRunLoopSourceInvalidate(controller->requestSource); CFRelease(controller->requestSource);
 [controller->requests release]; dispatch_release(controller->requestsManipQueue); [controller release];
 puts("PASS exact Wine request source, query-response mode, no PE pump, inline, shutdown");
} }
'''
# Parentheses clarify expected booleans in the compiled assertions.
body=body.replace('result==strcmp(argv[1],"exception")!=0','result==(strcmp(argv[1],"exception")!=0)').replace('atomic_load(&calls)==strcmp(argv[1],"getter")!=0','atomic_load(&calls)==(strcmp(argv[1],"getter")!=0)')
(out/'test.m').write_text(prefix+perform+'\n'+asyncfn+'\n'+syncfn+'\n'+body)
subprocess.run(['clang','-fblocks','-fno-objc-arc','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','test.m','-framework','Foundation','-framework','QuartzCore','-o','test_asan'],cwd=out,check=True)
for mode in ['normal','exception','queued','active','getter','stress','raw-comparison']:
 subprocess.run([str(out/'test_asan'),mode],check=True,timeout=15)
try:
 subprocess.run([str(out/'test_asan'),'nested-negative'],check=True,timeout=1)
except subprocess.TimeoutExpired: print('PASS previous nested native getter deterministically deadlocks (expected negative)')
else: raise AssertionError('old placement unexpectedly returned')
assert 'CFRunLoopAddSource(CFRunLoopGetMain(), requestSource, (CFStringRef)WineAppWaitQueryResponseMode)' in app
result={'passed':True,'scenarios':['custom_query_mode','no_pe_pump','inline','exception','queued_shutdown','admitted_shutdown','getter_worker','exact_nested_getter_negative','3200_requests_32_workers','raw_dispatch_custom_mode_negative'],'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [src/'wave7_main_executor.inc',src/'cocoa_app.m',src/'cocoa_event.m',src/'cocoa_window.m']}}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
