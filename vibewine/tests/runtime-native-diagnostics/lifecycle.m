/* Headless mock AppKit boundary. Production collector/admission/startup and
 * filesystem helpers are included verbatim. No AppKit window, Metal device/drawable or live Wine is created. */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CGGeometry.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#define NSApplication W7TestApplication
#define NSScreen W7TestScreen
#define NSWindow W7TestWindow
#define NSView W7TestView
#define CAMetalLayer W7TestMetalLayer

@interface NSApplication : NSObject
@property BOOL active,hidden,running;
@end
@implementation NSApplication @end
static NSApplication *NSApp;
@interface NSScreen : NSObject
@property(retain) NSDictionary *deviceDescription;
@end
@implementation NSScreen @end
@interface NSWindow : NSObject
@property(retain) NSScreen *screen;
@property CGRect frame;
@property NSInteger windowNumber,level;
@property NSUInteger occlusionState;
@property BOOL visible,miniaturized,onActiveSpace,keyWindow,mainWindow,hidesOnDeactivate;
@property double alphaValue;
@end
@implementation NSWindow @end
@interface NSView : NSObject
@property(retain) NSView *superview;
@property(retain) NSWindow *window;
@property(assign) id layer;
@property CGRect frame,bounds;
@property BOOL hidden,hiddenOrHasHiddenAncestor;
@property double alphaValue;
@end
@implementation NSView @end
@interface WineMetalView : NSView @end
@implementation WineMetalView @end
@interface WineContentView : NSView @end
@implementation WineContentView @end
static int layer_deallocs;
@interface CAMetalLayer : NSObject
@property(assign) id delegate;
@property CGRect bounds;
@property CGSize drawableSize;
@property BOOL hidden,framebufferOnly,displaySyncEnabled,opaque;
@property double opacity,contentsScale;
@end
@implementation CAMetalLayer
- (void)dealloc {layer_deallocs++; [super dealloc];}
@end
static dispatch_block_t queued, delayed;
static double mock_now=100;
static int test_clock_gettime(clockid_t c,struct timespec *t) {(void)c;t->tv_sec=(time_t)mock_now;t->tv_nsec=(long)((mock_now-t->tv_sec)*1e9);return 0;}
void OnMainThreadAsync(dispatch_block_t b) {assert(!queued);queued=[b copy];}
static void test_dispatch_after(dispatch_time_t t,dispatch_queue_t q,dispatch_block_t b) {(void)t;(void)q;assert(!delayed);delayed=[b copy];}
/* Failure injection is confined to the production serializer boundary. */
static int serializer_failure, serializer_calls;
@interface W7TestJSONSerialization : NSObject
+ (BOOL)isValidJSONObject:(id)object;
+ (NSData *)dataWithJSONObject:(id)object options:(NSJSONWritingOptions)options error:(NSError **)error;
@end
@implementation W7TestJSONSerialization
+ (BOOL)isValidJSONObject:(id)object {return [NSJSONSerialization isValidJSONObject:object];}
+ (NSData *)dataWithJSONObject:(id)object options:(NSJSONWritingOptions)options error:(NSError **)error {
 serializer_calls++;
 if(serializer_failure==1)return nil;
 if(serializer_failure==2)@throw [NSException exceptionWithName:@"fixture" reason:@"serializer failure" userInfo:nil];
 if(serializer_failure==3)return [NSMutableData dataWithLength:32769];
 return [NSJSONSerialization dataWithJSONObject:object options:options error:error];
}
@end
#define clock_gettime test_clock_gettime
#define NSJSONSerialization W7TestJSONSerialization
#include "visibility_snapshot.inc"
#undef NSJSONSerialization
#undef clock_gettime
static atomic_int startup_closes, startup_publishes;
static dispatch_semaphore_t startup_cleanup;
static int fixture_close(int fd) {
 int result=close(fd);
 assert(result==0);
 atomic_fetch_add(&startup_closes,1);
 if(startup_cleanup)dispatch_semaphore_signal(startup_cleanup);
 return result;
}
static int fixture_publish(int fd,const char *path,const void *bytes,size_t length) {
 assert(![NSThread isMainThread]);
 assert(atomic_fetch_add(&startup_publishes,1)==0);
 if(serializer_failure)assert(!bytes && !length);
 return w7vis_publish(fd,path,bytes,length);
}
#define dispatch_after test_dispatch_after
#define close fixture_close
#define w7vis_publish fixture_publish
#include "visibility_startup.inc"
#undef w7vis_publish
#undef close
#undef dispatch_after
static void pump(dispatch_block_t *slot) {dispatch_block_t b=*slot;*slot=nil;assert(b);b();[b release];}
static void scope(const char *path) {uint16_t b[256];size_t n=strlen(path);assert(n<256);for(size_t i=0;i<n;i++)b[i]=path[i];macdrv_visibility_set_image_v1(b,n*2);}
/* Existence alone races the worker's bounded write. Wait for complete JSON. */
static NSData *waitjson(NSString *path) {
 for(int i=0;i<100;i++) {
  NSData *data=[NSData dataWithContentsOfFile:path];
  if(data.length && [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL])return data;
  usleep(10000);
 }
 return nil;
}
static void metadata(NSDictionary *record) {
 NSSet *allowed=[NSSet setWithArray:@[@"schema",@"requestID",@"pid",@"sampleMonotonicRawSeconds",@"sampleThread",@"status",@"phase",@"settleDelayMs",@"processScope",@"coordinateSystems",@"birth",@"app",@"owner",@"layer",@"view",@"window"]];
 assert([[NSSet setWithArray:record.allKeys] isSubsetOfSet:allowed]);
 assert([record[@"schema"] intValue]==1);
 assert([record[@"pid"] intValue]==getpid());
}
int main(int argc,char **argv) {@autoreleasepool {
 assert(argc>=2);NSString *which=@(argv[1]);CAMetalLayer *layer=[[CAMetalLayer alloc]init];
 WineMetalView *view=[[[WineMetalView alloc]init]autorelease];layer.delegate=view;view.layer=layer;
 layer.drawableSize=CGSizeMake(1600,900);layer.contentsScale=2;layer.opacity=1;view.alphaValue=1;
 if([which isEqual:@"scope-steam"] || [which isEqual:@"scope-wrong-fullpath"] || [which isEqual:@"disabled"]) {
  if([which isEqual:@"scope-steam"])scope("C:\\Program Files (x86)\\Steam\\steam.exe");
  else if([which isEqual:@"scope-wrong-fullpath"])scope("D:\\Program Files (x86)\\Steam\\steamapps\\common\\DARK SOULS III\\Game\\DarkSoulsIII.exe");
  else {unsetenv("VIBEWINE_DS3_VISIBILITY_DIR");scope(W7VIS_DS3_IMAGE);}
  w7vis_startup_from_layer(layer);assert(!delayed&&!queued);[layer release];puts("PASS scope/disabled no scheduling/output");return 0;
 }
 if([which isEqual:@"startup-json-nil"])serializer_failure=1;
 if([which isEqual:@"startup-json-exception"])serializer_failure=2;
 if([which isEqual:@"startup-json-oversized"])serializer_failure=3;
 if([which isEqual:@"startup-write"] || [which isEqual:@"startup-retain"] || [which isEqual:@"startup-expired"] || [which isEqual:@"startup-bad-dir"] || serializer_failure){
  if(serializer_failure)startup_cleanup=dispatch_semaphore_create(0);
  assert(argc==3);setenv("VIBEWINE_DS3_VISIBILITY_DIR",argv[2],1);scope(W7VIS_DS3_IMAGE);
  w7vis_startup_from_layer(layer);assert(delayed&&!queued);dispatch_block_t first=delayed;w7vis_startup_from_layer(layer);assert(delayed==first&&!queued);
  [layer release];assert(layer_deallocs==0);mock_now=101.01;
  if([which isEqual:@"startup-expired"])mock_now=104;
  pump(&delayed);
  if([which isEqual:@"startup-expired"] || [which isEqual:@"startup-bad-dir"]){assert(!queued);assert(layer_deallocs==1);puts("PASS one-shot failure/expiry releases layer without main");return 0;}
  assert(queued);assert(layer_deallocs==0);view.layer=nil;layer.delegate=nil; /* retained layer is now detached */
  pump(&queued);
  if(serializer_failure){
   assert(serializer_calls==(serializer_failure==2 ? 2 : 1));
   long waited=dispatch_semaphore_wait(startup_cleanup,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC));
   if(waited || atomic_load(&startup_closes)!=1 || atomic_load(&startup_publishes)!=1){
    fprintf(stderr,"FAIL admitted serialization failure cleanup: mode=%d close=%d publish=%d\n",serializer_failure,atomic_load(&startup_closes),atomic_load(&startup_publishes));
    return 2;
   }
   assert(layer_deallocs==1);dispatch_release(startup_cleanup);startup_cleanup=nil;
   puts("PASS serialization failure completion closes startup directory exactly once");return 0;
  }
  NSString *file=[@(argv[2]) stringByAppendingPathComponent:@W7VIS_RESULT_NAME];
  NSData *data=waitjson(file);assert(data.length);NSDictionary *d=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
  metadata(d);assert([d[@"phase"] isEqual:@"startup"]);assert([d[@"status"] isEqual:@"sampled"]);
  assert([d[@"processScope"][@"windowsImagePath"] isEqual:@W7VIS_DS3_IMAGE]);assert([d[@"processScope"][@"source"] isEqual:@"PEB.ProcessParameters.ImagePathName"]);
  assert([d[@"window"] isEqual:[NSNull null]]);assert([d[@"app"] isEqual:[NSNull null]]);assert(![d[@"owner"][@"viewAttachedToWindow"] boolValue]);
  assert(layer_deallocs==1);puts("PASS exact-source delayed/retained/detached startup write");return 0;
 }
 __block NSData *answer=nil;dispatch_semaphore_t done=dispatch_semaphore_create(0);
 if([which isEqual:@"invalid-json"])layer.drawableSize=CGSizeMake(NAN,INFINITY);
 assert(!macdrv_request_metal_visibility_v1(nil,1,101,^(NSData*d){(void)d;}));
 assert(!macdrv_request_metal_visibility_v1(layer,1,101,nil));
 assert(!macdrv_request_metal_visibility_v1(layer,1,NAN,^(NSData*d){(void)d;}));
 assert(!macdrv_request_metal_visibility_v1(layer,1,INFINITY,^(NSData*d){(void)d;}));
 assert(!macdrv_request_metal_visibility_v1(layer,1,100,^(NSData*d){(void)d;}));
 assert(!macdrv_request_metal_visibility_v1(layer,1,103,^(NSData*d){(void)d;}));
 assert(macdrv_request_metal_visibility_v1(layer,7,101,^(NSData*d){assert(![NSThread isMainThread]);answer=[d retain];dispatch_semaphore_signal(done);}));
 assert(!macdrv_request_metal_visibility_v1(layer,8,101,^(NSData*d){(void)d;}));
 if([which isEqual:@"expired-main"])mock_now=102;
 pump(&queued);assert(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
 NSDictionary*d=[NSJSONSerialization JSONObjectWithData:answer options:0 error:NULL];assert(d);metadata(d);assert([d[@"requestID"] intValue]==7);
 if([which isEqual:@"invalid-json"])assert([d[@"status"] isEqual:@"invalid-main-sample-json"]);
 else if([which isEqual:@"expired-main"])assert([d[@"status"] isEqual:@"expired-before-main-sample"]);
 else {assert([d[@"status"] isEqual:@"sampled"]);assert([d[@"window"] isEqual:[NSNull null]]);assert([d[@"app"] isEqual:[NSNull null]]);}
 [answer release];dispatch_release(done);[layer release];puts("PASS actual-source one-shot/deadline/JSON collector");return 0;
}}
