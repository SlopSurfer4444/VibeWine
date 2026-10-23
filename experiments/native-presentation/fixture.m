// SPDX-License-Identifier: MIT
/* Private native-only Metal visibility discriminator. ARC; no Wine or input. */
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <libproc.h>
#include <sys/proc_info.h>

#if defined(__x86_64__)
#define FIXTURE_ARCH "x86_64"
#else
#define FIXTURE_ARCH "arm64"
#endif
static const NSUInteger eventCap=2048, frameCap=160;
static NSMutableArray *records;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static NSUInteger lost, gpuPending, presentedCallbacks;
static BOOL finalized;
static int outputFD=-1, outcomeCode;
static BOOL requestedWaitForStart;
static NSInteger phaseForAge(double age) { return age>=8?2:age>=4?1:0; }
static BOOL startWaitExpired(double age,BOOL running) { return !running&&age>=30; }
static uint64_t receiptNS(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC_RAW,&t);return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec; }
static BOOL validTime(double t) { return isfinite(t)&&t>0; }
static uint64_t bits(double t) { uint64_t b;memcpy(&b,&t,sizeof(b));return b; }
static void emit(NSDictionary *value) {
 pthread_mutex_lock(&lock);
 if (!finalized) { if(records.count<eventCap)[records addObject:[value copy]];else ++lost; }
 pthread_mutex_unlock(&lock);
}
static int finalCode(BOOL started,NSUInteger mask,int failure) { return failure?failure:started&&mask==7?0:70; }
static void flushAndExit(NSArray *snapshot,int status) __attribute__((noreturn));
static void flushAndExit(NSArray *snapshot,int status) {
 for(NSDictionary *row in snapshot){NSData *data=[NSJSONSerialization dataWithJSONObject:row options:0 error:nil];if(!data) _exit(70);const char *p=data.bytes;size_t n=data.length;while(n){ssize_t wrote=write(outputFD,p,n);if(wrote<=0)_exit(74);p+=wrote;n-=(size_t)wrote;}if(write(outputFD,"\n",1)!=1)_exit(74);}
 if(fsync(outputFD)<0||close(outputFD)<0)_exit(74);outputFD=-1;
 /* NSApplication terminate: exits0 itself; do not lose intended failure status. */
 _exit(status);
}
static void pending(int delta) { pthread_mutex_lock(&lock);if(delta>0)++gpuPending;else if(gpuPending)--gpuPending;pthread_mutex_unlock(&lock); }
static NSUInteger pendingCount(void) { pthread_mutex_lock(&lock);NSUInteger n=gpuPending;pthread_mutex_unlock(&lock);return n; }
static id ownCG(BOOL onscreen) {
 CFArrayRef list=CGWindowListCopyWindowInfo(onscreen?kCGWindowListOptionOnScreenOnly:kCGWindowListOptionAll,kCGNullWindowID);
 if(!list)return NSNull.null;NSMutableArray *out=[NSMutableArray array];
 for(NSDictionary *row in (__bridge NSArray *)list)if([row[(id)kCGWindowOwnerPID] intValue]==getpid())
  [out addObject:@{@"id":row[(id)kCGWindowNumber]?:NSNull.null,@"layer":row[(id)kCGWindowLayer]?:NSNull.null,@"bounds":row[(id)kCGWindowBounds]?:NSNull.null,@"alpha":row[(id)kCGWindowAlpha]?:NSNull.null,@"optionalIsOnscreen":row[(id)kCGWindowIsOnscreen]?:NSNull.null}];
 CFRelease(list);return out;
}
@interface Fixture : NSObject<NSApplicationDelegate,NSWindowDelegate>
@property NSWindow *window;
@property CAMetalLayer *layer;
@property id<MTLCommandQueue> queue;
@property NSTimer *timer;
@property double started, stoppingAt;
@property NSInteger phase;
@property NSUInteger frame;
@property BOOL stopping;
@property BOOL running, readyObserved;
@property double launchedAt;
@property NSUInteger settledMask;
@property NSButton *startButton;
@end
@implementation Fixture
- (void)snapshot:(NSString *)event {
 NSWindow *w=self.window;NSView *v=w.contentView;CAMetalLayer *l=self.layer;
 struct proc_bsdinfo b={0};BOOL identity=proc_pidinfo(getpid(),PROC_PIDTBSDINFO,0,&b,sizeof(b))==sizeof(b);
 emit(@{@"event":event,@"receipt_clock":@"cpu_monotonic_raw_ns",@"receipt_ns":@(receiptNS()),@"phase":@(self.phase),@"pid":@(getpid()),@"birthAvailable":@(identity),@"birthSec":@(b.pbi_start_tvsec),@"birthUsec":@(b.pbi_start_tvusec),@"windowId":@(w.windowNumber),@"isVisible":@(w.visible),@"isMiniaturized":@(w.miniaturized),@"isOnActiveSpace":@(w.onActiveSpace),@"occlusionState":@(w.occlusionState),@"isKey":@(w.keyWindow),@"isMain":@(w.mainWindow),@"hidesOnDeactivate":@(w.hidesOnDeactivate),@"appActive":@(NSApp.active),@"appHidden":@(NSApp.hidden),@"frontmostPid":@(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier),@"viewOwnsWindow":@(v.window==w),@"viewWindowId":v.window?@(v.window.windowNumber):NSNull.null,@"viewBounds":NSStringFromRect(v.bounds),@"screenNumber":w.screen.deviceDescription[@"NSScreenNumber"]?:NSNull.null,@"viewHidden":@(v.hidden),@"layerHidden":@(l.hidden),@"layerOpacity":@(l.opacity),@"layerBounds":NSStringFromRect(l.bounds),@"windowFrame":NSStringFromRect(w.frame),@"drawableWidth":@(l.drawableSize.width),@"drawableHeight":@(l.drawableSize.height),@"contentsScale":@(l.contentsScale),@"cgAll":ownCG(NO),@"cgOnscreen":ownCG(YES)});
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
 (void)note;self.phase=-1;self.launchedAt=NSProcessInfo.processInfo.systemUptime;
 id<MTLDevice> device=MTLCreateSystemDefaultDevice();self.queue=[device newCommandQueue];
 if(!device||!self.queue){outcomeCode=70;emit(@{@"event":@"SETUP_FAILED",@"reason":@"Metal unavailable"});[self finish];return;}
 self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(180,220,640,360) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
 self.window.title=requestedWaitForStart?@"Native Metal WAITING - Start disabled until settled":@"Native Metal preparing";self.window.delegate=self;self.window.releasedWhenClosed=NO;self.window.hidesOnDeactivate=NO;
 NSView *view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,640,360)];view.wantsLayer=YES;
 self.layer=[CAMetalLayer layer];self.layer.device=device;self.layer.pixelFormat=MTLPixelFormatBGRA8Unorm;self.layer.framebufferOnly=YES;self.layer.allowsNextDrawableTimeout=YES;self.layer.displaySyncEnabled=YES;self.layer.drawableSize=CGSizeMake(640,360);self.layer.contentsScale=1;view.layer=self.layer;self.window.contentView=view;
 if(requestedWaitForStart){
  self.startButton=[NSButton buttonWithTitle:@"Start presentation phases" target:self action:@selector(startPressed:)];self.startButton.frame=NSMakeRect(180,145,280,60);self.startButton.enabled=NO;self.startButton.accessibilityIdentifier=@"native-present-start";[view addSubview:self.startButton];
 }
 [self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
 self.timer=[NSTimer scheduledTimerWithTimeInterval:.1 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
}
- (void)tick:(NSTimer *)timer {
 (void)timer;double now=NSProcessInfo.processInfo.systemUptime;
 if(self.stopping){if(!pendingCount()||now-self.stoppingAt>=2)[self finish];return;}
 if(!self.readyObserved && now-self.launchedAt>=.5){
  self.readyObserved=YES;[self snapshot:@"PREFLIGHT_SETTLED"];
  if(requestedWaitForStart){self.window.title=@"Native Metal READY - press Start";self.startButton.enabled=YES;}
  else [self startPressed:nil];
 }
 if(!self.running){if(startWaitExpired(now-self.launchedAt,self.running)){outcomeCode=70;[self beginStop:@"start_wait_timeout"];}return;}
 double age=now-self.started;
 if(age>=12||self.frame>=frameCap){[self beginStop:@"scheduled_end"];return;}
 NSInteger desired=phaseForAge(age);
 if(desired!=self.phase){self.phase=desired;self.window.title=desired==1?@"Native Metal RUNNING - ordered out 1":@"Native Metal RUNNING - visible 2";if(desired==1)[self.window orderOut:nil];else[self.window makeKeyAndOrderFront:nil];[self snapshot:desired==1?@"ORDERED_OUT":@"VISIBLE_RESTORED"];}
 NSUInteger phaseBit=(NSUInteger)1<<desired;
 if(age>=2+4*desired && !(self.settledMask&phaseBit)){self.settledMask|=phaseBit;[self snapshot:@"PHASE_SETTLED"];}
 @autoreleasepool {
  NSUInteger frame=++self.frame;NSInteger phase=self.phase;uint64_t acquired=receiptNS();id<CAMetalDrawable> drawable=[self.layer nextDrawable];uint64_t acquisitionNS=receiptNS()-acquired;
  if(!drawable){emit(@{@"event":@"NO_DRAWABLE",@"frame":@(frame),@"phase":@(phase),@"receipt_ns":@(receiptNS()),@"nextDrawable_cpu_ns":@(acquisitionNS)});return;}
  uint64_t drawableID=drawable.drawableID;
  MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=drawable.texture;pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
  pass.colorAttachments[0].clearColor=MTLClearColorMake((double)(frame%30)/30.0,phase==1?.1:.6,phase==2?.9:.2,1);
  id<MTLCommandBuffer> buffer=[self.queue commandBuffer];id<MTLRenderCommandEncoder> encoder=[buffer renderCommandEncoderWithDescriptor:pass];
  if(!buffer||!encoder){outcomeCode=70;emit(@{@"event":@"ENCODE_FAILED",@"frame":@(frame),@"phase":@(phase)});[self beginStop:@"encode_failed"];return;}
  [encoder endEncoding];
  [drawable addPresentedHandler:^(id<MTLDrawable> callbackDrawable){
   double t=callbackDrawable.presentedTime;uint64_t received=receiptNS();
   pthread_mutex_lock(&lock);++presentedCallbacks;pthread_mutex_unlock(&lock);
   emit(@{@"event":@"PRESENTED_CALLBACK",@"frame":@(frame),@"phaseAtSubmission":@(phase),@"registeredDrawableID":@(drawableID),@"callbackDrawableID":@(callbackDrawable.drawableID),@"callbackDrawableNil":@(callbackDrawable==nil),@"presentedTimeRawBits":@(bits(t)),@"presentedTimeSeconds":isfinite(t)?@(t):NSNull.null,@"presentedTimeValid":@(validTime(t)),@"presentedClock":@"CoreAnimation_host_seconds",@"callbackReceipt_ns":@(received),@"callbackReceiptClock":@"cpu_monotonic_raw_ns"});
  }];
  pending(1);
  [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed){
   double a=completed.GPUStartTime,b=completed.GPUEndTime;
   emit(@{@"event":@"GPU_COMPLETED_CALLBACK",@"frame":@(frame),@"phaseAtSubmission":@(phase),@"drawableID":@(drawableID),@"commandBufferStatus":@(completed.status),@"gpuStartSeconds":isfinite(a)?@(a):NSNull.null,@"gpuEndSeconds":isfinite(b)?@(b):NSNull.null,@"gpuDurationSeconds":completed.status==MTLCommandBufferStatusCompleted&&validTime(a)&&isfinite(b)&&b>=a?@(b-a):NSNull.null,@"callbackReceipt_ns":@(receiptNS()),@"callbackReceiptClock":@"cpu_monotonic_raw_ns"});pending(-1);
  }];
  emit(@{@"event":@"SUBMITTED",@"frame":@(frame),@"phase":@(phase),@"drawableID":@(drawableID),@"receipt_ns":@(receiptNS()),@"nextDrawable_cpu_ns":@(acquisitionNS),@"width":@(drawable.texture.width),@"height":@(drawable.texture.height)});
  [buffer presentDrawable:drawable];[buffer commit];
 }
}
- (void)startPressed:(id)sender {
 (void)sender;if(!self.readyObserved||self.running||self.stopping)return;
 self.running=YES;self.started=NSProcessInfo.processInfo.systemUptime;self.phase=0;
 [self.startButton removeFromSuperview];self.startButton=nil;self.window.title=@"Native Metal RUNNING - visible 0";
 emit(@{@"event":@"START_PHASES",@"receipt_ns":@(receiptNS()),@"waitForStart":@(requestedWaitForStart)});[self snapshot:@"VISIBLE_INITIAL"];
}
- (void)beginStop:(NSString *)reason {
 if(self.stopping)return;self.stopping=YES;self.stoppingAt=NSProcessInfo.processInfo.systemUptime;
 emit(@{@"event":@"STOP_REQUEST",@"reason":reason,@"receipt_ns":@(receiptNS())});[self snapshot:@"STOP_SNAPSHOT"];
}
- (BOOL)windowShouldClose:(NSWindow *)sender { (void)sender;[self beginStop:@"user_closed_window"];return NO; }
- (void)finish {
 [self.timer invalidate];self.timer=nil;
 pthread_mutex_lock(&lock);
 int intendedCode=finalCode(self.running,self.settledMask,outcomeCode);
 NSDictionary *summary=@{@"event":@"SUMMARY",@"intendedExitCode":@(intendedCode),@"framesAttempted":@(self.frame),@"phasesStarted":@(self.running),@"settledPhaseMask":@(self.settledMask),@"recordCount":@(records.count),@"recordOverflow":@(lost),@"gpuPendingAtClose":@(gpuPending),@"presentedCallbackCount":@(presentedCallbacks),@"nativeCallbacksFinalized":@NO,@"note":@"callback count is not presented frame count; only finite positive raw native timestamps can qualify; no fallback clock"};
 [records addObject:summary];finalized=YES;NSArray *snapshot=[records copy];pthread_mutex_unlock(&lock);
 flushAndExit(snapshot,intendedCode);
}
@end
int main(int argc,char **argv){@autoreleasepool{
 sigset_t signals;sigemptyset(&signals);sigaddset(&signals,SIGINT);sigaddset(&signals,SIGTERM);pthread_sigmask(SIG_UNBLOCK,&signals,NULL);
 if(argc==3&&(!strcmp(argv[1],"--self-test-no-start-exit")||!strcmp(argv[1],"--self-test-complete-exit"))){
  if(argv[2][0]!='/')return 64;outputFD=open(argv[2],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(outputFD<0)return 73;
  BOOL started=!strcmp(argv[1],"--self-test-complete-exit");int code=finalCode(started,started?7:0,started?0:70);
  flushAndExit(@[@{@"event":@"SUMMARY",@"offlineExitFlowTest":@YES,@"phasesStarted":@(started),@"settledPhaseMask":@(started?7:0),@"intendedExitCode":@(code)}],code);
 }
 if(argc==2&&!strcmp(argv[1],"--self-test")){
  if(validTime(0)||validTime(-1)||validTime(NAN)||validTime(INFINITY)||!validTime(1.25)||bits(0)!=0||bits(1.0)!=UINT64_C(0x3ff0000000000000))return 1;
  if(phaseForAge(3.999)!=0||phaseForAge(4)!=1||phaseForAge(7.999)!=1||phaseForAge(8)!=2||startWaitExpired(29.999,NO)||!startWaitExpired(30,NO)||startWaitExpired(31,YES))return 1;
  if(finalCode(NO,0,0)!=70||finalCode(YES,3,0)!=70||finalCode(YES,7,0)!=0||finalCode(YES,7,74)!=74)return 1;
  records=[NSMutableArray array];for(NSUInteger i=0;i<eventCap+3;++i)emit(@{@"event":@"test"});if(records.count!=eventCap||lost!=3)return 1;puts("SELF_TEST_OK no AppKit application or Metal device created");return 0;
 }
 if((argc!=2 && !(argc==3 && !strcmp(argv[2],"--wait-for-start")))||argv[1][0]!='/'){fputs("usage: fixture ABS_NEW_JSONL [--wait-for-start] | --self-test\n",stderr);return 64;}
 requestedWaitForStart=argc==3;
 outputFD=open(argv[1],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(outputFD<0)return 73;
 records=[NSMutableArray array];emit(@{@"event":@"START",@"pid":@(getpid()),@"architecture":@(FIXTURE_ARCH),@"receipt_ns":@(receiptNS()),@"durationSeconds":@12,@"waitForStart":@(requestedWaitForStart),@"maxWaitSeconds":@30,@"frameCap":@(frameCap),@"eventCap":@(eventCap)});
 NSApplication *app=NSApplication.sharedApplication;[app setActivationPolicy:NSApplicationActivationPolicyRegular];__attribute__((objc_precise_lifetime)) Fixture *delegate=[Fixture new];app.delegate=delegate;[app run];return outcomeCode;
}}
