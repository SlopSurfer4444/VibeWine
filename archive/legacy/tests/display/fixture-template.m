#import <Foundation/Foundation.h>
#include <stdio.h>

typedef id CGDisplayModeRef;
typedef unsigned int CGDirectDisplayID;
typedef int CGError;
#define CGDisplayNoErr 0
#define ERR(...) do {} while (0)
static NSMutableDictionary *physicalModes;
static NSDictionary *desktopModes;
static int releaseError, captureError, setError, releaseCalls, captureCalls, setCalls, restoreCalls, copyCalls;
static BOOL invalidDisplay;
static int failCopyCall;
static NSMutableDictionary **pendingDestination;
static NSString *requestDuringSet;
static CGDisplayModeRef CGDisplayModeRetain(CGDisplayModeRef mode) { return [mode retain]; }
static void CGDisplayModeRelease(CGDisplayModeRef mode) { [mode release]; }
static CGDisplayModeRef CGDisplayCopyDisplayMode(CGDirectDisplayID displayID)
{ copyCalls++; return (invalidDisplay || copyCalls == failCopyCall) ? nil : [physicalModes[@(displayID)] retain]; }
static void CGRestorePermanentDisplayConfiguration(void)
{ restoreCalls++; [physicalModes setDictionary:desktopModes]; }
static CGError CGReleaseAllDisplays(void) { releaseCalls++; return releaseError; }
static CGError CGCaptureAllDisplays(void) { captureCalls++; return captureError; }
static CGError CGDisplaySetDisplayMode(CGDirectDisplayID displayID, CGDisplayModeRef mode, void *options)
{ setCalls++; if (requestDuringSet) (*pendingDestination)[@(displayID)] = requestDuringSet; if (!setError) physicalModes[@(displayID)] = mode; return setError; }
@interface FakeApp : NSObject
@property BOOL active;
- (BOOL)isActive;
@end
@implementation FakeApp
- (BOOL)isActive { return self.active; }
@end
static FakeApp *NSApp;
@interface Controller : NSObject {
@public NSMutableDictionary *originalDisplayModes, *latentDisplayModes;
@public BOOL displaysCaptured, displaysCapturedForFullscreen, beenActive, forceNextMouseMoveAbsolute;
}
- (BOOL)setMode:(CGDisplayModeRef)mode forDisplay:(CGDirectDisplayID)displayID;
- (BOOL)releaseDisplayCaptureSavingModes:(BOOL)saveModes;
- (BOOL)areDisplaysCaptured;
- (BOOL)mode:(CGDisplayModeRef)a matchesMode:(CGDisplayModeRef)b;
- (NSArray *)modesMatchingMode:(CGDisplayModeRef)mode forDisplay:(CGDirectDisplayID)displayID;
- (void)transformProcessToForeground:(BOOL)force;
- (void)adjustWindowLevels;
- (void)adjustWindowLevels:(BOOL)active;
- (void)updateFullscreenWindows;
- (void)unminimizeWindowIfNoneVisible;
- (void)sendDisplaysChanged:(BOOL)activating;
- (void)applicationDidBecomeActive:(NSNotification *)notification;
@end
@implementation Controller
- (instancetype)init { if ((self = [super init])) { originalDisplayModes = [NSMutableDictionary new]; latentDisplayModes = [NSMutableDictionary new]; } return self; }
- (void)dealloc { [originalDisplayModes release]; [latentDisplayModes release]; [super dealloc]; }
- (BOOL)mode:(CGDisplayModeRef)a matchesMode:(CGDisplayModeRef)b { return [a isEqual:b]; }
- (NSArray *)modesMatchingMode:(CGDisplayModeRef)mode forDisplay:(CGDirectDisplayID)displayID { return [mode isEqual:@"unsupported"] ? @[] : @[mode]; }
- (void)transformProcessToForeground:(BOOL)force {}
- (void)adjustWindowLevels {}
- (void)adjustWindowLevels:(BOOL)active {}
- (void)updateFullscreenWindows {}
- (void)unminimizeWindowIfNoneVisible {}
- (void)sendDisplaysChanged:(BOOL)activating {}
@@SET@@
@@RELEASE@@
@@CAPTURED@@
@@ACTIVE@@
@end
static int failures, checks;
static void check(BOOL value, const char *name)
{ checks++; if (!value) { failures++; fprintf(stderr, "FAIL %s\n", name); } }
static Controller *reset(void)
{
 [physicalModes release]; [NSApp release];
 physicalModes = [@{@1:@"desktop", @2:@"desktop2"} mutableCopy];
 desktopModes = @{@1:@"desktop", @2:@"desktop2"};
 releaseError = captureError = setError = releaseCalls = captureCalls = setCalls = restoreCalls = copyCalls = 0;
 failCopyCall = 0; invalidDisplay = NO; pendingDestination=nil; requestDuringSet=nil; NSApp = [FakeApp new]; NSApp.active = YES; return [[[Controller alloc] init] autorelease];
}
int main(void) { @autoreleasepool {
 Controller *c; BOOL result;
 c=reset(); failCopyCall=2; releaseError=7;
 result=[c setMode:@"game" forDisplay:1];
 check(!result && captureCalls==1 && releaseCalls==1,"query failure after successful capture attempts release");
 check(!c->originalDisplayModes.count && [c areDisplaysCaptured],"failed release retains ownership without original mode");
 releaseError=0; check([c releaseDisplayCaptureSavingModes:NO],"cleanup retries orphan capture");
 check(releaseCalls==2 && ![c areDisplaysCaptured],"successful release clears orphan capture");
 c=reset(); captureError=7; result=[c setMode:@"game" forDisplay:1];
 check(!result && ![c areDisplaysCaptured] && !releaseCalls,"failed capture creates no release obligation");
 c=reset(); failCopyCall=2; result=[c setMode:@"game" forDisplay:1];
 check(!result && releaseCalls==1 && ![c areDisplaysCaptured],"successful rollback clears capture after missing mode");
 c=reset(); c->originalDisplayModes[@1]=@"desktop"; c->latentDisplayModes[@1]=@"game"; physicalModes[@1]=@"game"; releaseError=7;
 result=[c setMode:@"desktop" forDisplay:1];
 check(!result,"failed final release reports failure"); check(c->originalDisplayModes.count==1,"failed final release keeps capture ownership");
 check([c->latentDisplayModes[@1] isEqual:@"game"],"failed final release preserves pending mode"); check([physicalModes[@1] isEqual:@"desktop"],"restore may precede release failure");
 releaseError=0; result=[c setMode:@"desktop" forDisplay:1];
 check(result && !c->originalDisplayModes.count && !c->latentDisplayModes.count,"successful retry settles state"); check(releaseCalls==2,"retry releases again");

 c=reset(); c->originalDisplayModes[@1]=@"desktop"; c->latentDisplayModes[@1]=@"game";
 c->displaysCapturedForFullscreen=YES; result=[c setMode:@"desktop" forDisplay:1];
 check(result && !c->originalDisplayModes.count && c->displaysCapturedForFullscreen,"geometric fullscreen retains capture"); check(!releaseCalls && !c->latentDisplayModes.count,"geometric restore does not release capture");

 c=reset(); c->originalDisplayModes[@1]=@"desktop"; c->latentDisplayModes[@1]=@"game";
 result=[c setMode:@"game" forDisplay:1];
 check(result && setCalls==1 && [physicalModes[@1] isEqual:@"game"],"active request realizes stale latent game mode");
 check(!c->latentDisplayModes.count && [c->originalDisplayModes[@1] isEqual:@"desktop"],"active mode success cancels stale latent request");

 c=reset(); c->originalDisplayModes[@1]=@"desktop"; c->latentDisplayModes[@1]=@"oldgame";
 result=[c setMode:@"newgame" forDisplay:1];
 check(result && setCalls==1 && !c->latentDisplayModes.count,"active resolution change clears older latent request");
 check([physicalModes[@1] isEqual:@"newgame"],"active resolution changed physically");

 c=reset(); c->latentDisplayModes[@1]=@"desktop";
 result=[c setMode:@"desktop" forDisplay:1];
 check(result && !setCalls && copyCalls==1 && !c->latentDisplayModes.count,"active physical no-op clears redundant latent request");

 c=reset(); NSApp.active=NO; c->latentDisplayModes[@1]=@"game";
 result=[c setMode:@"game" forDisplay:1];
 check(result && !setCalls && !copyCalls && [c->latentDisplayModes[@1] isEqual:@"game"],"inactive latent no-op remains deferred");
 result=[c setMode:@"newgame" forDisplay:1];
 check(result && !setCalls && !captureCalls && [c->latentDisplayModes[@1] isEqual:@"newgame"],"inactive resolution change updates pending mode only");

 c=reset(); setError=3; c->latentDisplayModes[@1]=@"pending";
 result=[c setMode:@"game" forDisplay:1];
 check(!result && [c->latentDisplayModes[@1] isEqual:@"pending"],"failed active mode set preserves pending request");
 check(captureCalls==1 && releaseCalls==1 && !c->originalDisplayModes.count,"failed first set releases new capture");

 c=reset(); setError=3; releaseError=7;
 result=[c setMode:@"game" forDisplay:1];
 check(!result && [c->originalDisplayModes[@1] isEqual:@"desktop"],"failed set and release keeps retry ownership");
 setError=0; releaseError=0; result=[c releaseDisplayCaptureSavingModes:NO];
 check(result && ![c areDisplaysCaptured],"failed set cleanup retry settles capture");

 c=reset(); c->originalDisplayModes[@1]=@"desktop"; c->originalDisplayModes[@2]=@"desktop2"; c->latentDisplayModes[@1]=@"game"; physicalModes[@1]=@"game";
 result=[c setMode:@"desktop" forDisplay:1];
 check(result && setCalls==1 && !restoreCalls && !releaseCalls,"multidisplay restore touches only requested display");
 check(!c->originalDisplayModes[@1] && c->originalDisplayModes[@2] && !c->latentDisplayModes[@1],"multidisplay restore preserves other ownership");

 c=reset(); captureError=7; result=[c setMode:@"game" forDisplay:1];
 check(!result && !setCalls && ![c areDisplaysCaptured],"capture failure rejects mode change");
 c=reset(); invalidDisplay=YES; result=[c setMode:@"game" forDisplay:1];
 check(!result && !captureCalls && !setCalls,"invalid display rejects without capture");
 c=reset(); result=[c setMode:@"unsupported" forDisplay:1];
 check(!result && !captureCalls && !setCalls,"unsupported mode rejects without capture");

 c=reset(); c->originalDisplayModes[@1]=@"desktop"; physicalModes[@1]=@"game"; releaseError=7;
 result=[c releaseDisplayCaptureSavingModes:YES];
 check(!result && [c->latentDisplayModes[@1] isEqual:@"game"],"deactivation failure saves actual game mode");
 result=[c releaseDisplayCaptureSavingModes:YES];
 check(!result && [c->latentDisplayModes[@1] isEqual:@"game"],"deactivation retry does not overwrite saved mode with desktop");
 releaseError=0; result=[c releaseDisplayCaptureSavingModes:NO];
 check(result && ![c areDisplaysCaptured] && !c->latentDisplayModes.count,"final exit cleanup discards pending modes");

 c=reset(); c->latentDisplayModes[@1]=@"game"; setError=3;
 [c applicationDidBecomeActive:nil];
 check([c->latentDisplayModes[@1] isEqual:@"game"] && [physicalModes[@1] isEqual:@"desktop"],"failed reactivation retains requested mode");
 check(c->beenActive && c->forceNextMouseMoveAbsolute,"activation bookkeeping retained on mode failure");
 setError=0; [c applicationDidBecomeActive:nil];
 check(!c->latentDisplayModes.count && [physicalModes[@1] isEqual:@"game"],"next activation retries failed requested mode");

 c=reset(); c->latentDisplayModes[@1]=@"game"; invalidDisplay=YES;
 [c applicationDidBecomeActive:nil];
 check([c->latentDisplayModes[@1] isEqual:@"game"],"unavailable display reactivation request survives");

 c=reset(); c->latentDisplayModes[@1]=@"game"; setError=3;
 pendingDestination=&c->latentDisplayModes; requestDuringSet=@"newer";
 [c applicationDidBecomeActive:nil];
 check([c->latentDisplayModes[@1] isEqual:@"newer"],"failed reactivation preserves newer concurrent pending request");

 printf("{\"checks\":%d,\"failures\":%d,\"physicalDisplayApisInvoked\":false}\n",checks,failures);
 [physicalModes release]; [NSApp release]; return failures ? 1 : 0;
} }
