/* Real AppKit fixture, not the complete Wine driver. Root owns all launches.
 * Exact production root and native-window identity seams are included below.
 * Adapter fields model only the root contract; no PE/provider code is linked. */
#import <Cocoa/Cocoa.h>
#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>
#include <stdint.h>
#include "fixture_ownership.h"
@interface WineApplication : NSApplication
- (NSArray *)fixtureInheritedWindows;
- (NSArray *)fixtureInheritedChildren;
@end
@implementation WineApplication
- (NSArray *)fixtureInheritedWindows { return [super accessibilityWindows]; }
- (NSArray *)fixtureInheritedChildren { return [super accessibilityChildren]; }
@end
@interface WineWindow : NSPanel {
@public void *hwnd; BOOL disabled,noForeground;
}
@property BOOL closing,emptyContent;
- (BOOL)presentsVisibleContent;
@end
typedef void *macdrv_window;
static void *macdrv_get_window_hwnd(macdrv_window window) { return ((WineWindow *)window)->hwnd; }
#include "../source/winemac.drv/ax_window_root_policy.h"
@implementation WineWindow
#include "../source/winemac.drv/ax_window_identity.inc"
/* Production disabled/noForeground/cycle focusability checks. The internal
 * causing_becomeKeyWindow dispatcher path is not used by this fixture. */
- (BOOL)canBecomeKeyWindow {
 if (disabled || noForeground) return NO;
 if ([self isKeyWindow]) return YES;
 return ![self isExcludedFromWindowsMenu];
}
- (BOOL)canBecomeMainWindow { return [self canBecomeKeyWindow]; }
- (BOOL)isExcludedFromWindowsMenu { return !([self collectionBehavior] & NSWindowCollectionBehaviorParticipatesInCycle); }
- (BOOL)presentsVisibleContent { return !_emptyContent && NSWidth(self.frame)>0 && NSHeight(self.frame)>0; }
@end
#include "../source/winemac.drv/ax_application.inc"

static FILE *events;
static NSArray *fixture_windows;
static NSString *fixture_phase=@"starting";
static void emit(NSDictionary *row) {
 NSData *data=[NSJSONSerialization dataWithJSONObject:row options:0 error:nil];
 if(!data)exit(66);fwrite(data.bytes,1,data.length,events);fputc('\n',events);fflush(events);
}
static NSArray *ids(NSArray *elements) {
 NSMutableArray *out=[NSMutableArray array];
 for(id element in elements) {
  if(![element isKindOfClass:[NSWindow class]])continue;
  NSString *identifier=[element accessibilityIdentifier];if(identifier)[out addObject:identifier];
 }
 return out;
}
static void snapshot(NSString *event) {
 WineApplication *app=(WineApplication *)NSApp;
 NSMutableArray *windows=[NSMutableArray array];
 for(NSWindow *w in fixture_windows) {
  NSMutableDictionary *row=[@{@"identifier":[w accessibilityIdentifier]?:@"",@"title":w.title?:@"",@"visible":@(w.visible),@"miniaturized":@(w.miniaturized),@"key":@(w.keyWindow),@"main":@(w.mainWindow)} mutableCopy];
  if([w isKindOfClass:[WineWindow class]]) { WineWindow *wine=(WineWindow*)w;row[@"hwnd"]=@((uintptr_t)wine->hwnd);row[@"closing"]=@(wine.closing);row[@"content"]=@(wine.presentsVisibleContent); }
  [windows addObject:row];
 }
 emit(@{@"event":event,@"phase":fixture_phase,@"pid":@(getpid()),@"PEBridgeEnabled":@(macdrv_ax_bridge_enabled()),@"nativeRootEligible":@(wine_ax_native_application_root_enabled()),@"rootEnabled":@(wine_ax_window_root_enabled()),@"active":@(app.active),@"inheritedWindowIds":ids([app fixtureInheritedWindows]),@"inheritedChildWindowIds":ids([app fixtureInheritedChildren]),@"windowIds":ids([app accessibilityWindows]),@"childWindowIds":ids([app accessibilityChildren]),@"keyIdentifier":app.keyWindow.accessibilityIdentifier?:[NSNull null],@"mainIdentifier":app.mainWindow.accessibilityIdentifier?:[NSNull null],@"objects":windows});
}
static WineWindow *make_window(NSString *title,uintptr_t handle,NSRect frame,NSWindowStyleMask style) {
 WineWindow *w=[[WineWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
 w->hwnd=(void*)handle;w.title=title;w.releasedWhenClosed=NO;w.collectionBehavior=NSWindowCollectionBehaviorParticipatesInCycle;
 return w;
}
@interface FixtureDelegate : NSObject <NSApplicationDelegate> @end
@implementation FixtureDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)note {
 (void)note;
 WineWindow *ordinary=make_window(@"Native root ordinary",0x1001,NSMakeRect(70,240,420,220),NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable);
 WineWindow *borderless=make_window(@"Native root borderless",0x1002,NSMakeRect(530,240,420,220),NSWindowStyleMaskBorderless);
 WineWindow *hidden=make_window(@"Native root hidden",0x1003,NSMakeRect(70,20,300,150),NSWindowStyleMaskTitled);
 WineWindow *closing=make_window(@"Native root closing",0x1004,NSMakeRect(980,240,250,150),NSWindowStyleMaskTitled);closing.closing=YES;
 WineWindow *empty=make_window(@"Native root empty content",0x1005,NSMakeRect(980,30,250,150),NSWindowStyleMaskTitled);empty.emptyContent=YES;
 WineWindow *helper=make_window(@"Native root HWND-zero helper",0,NSMakeRect(1250,240,250,150),NSWindowStyleMaskTitled);[helper setAccessibilityIdentifier:@"fixture-hwnd-zero-helper"];
 NSWindow *host=[[NSWindow alloc] initWithContentRect:NSMakeRect(70,20,420,140) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];host.title=@"Native host baseline control";host.releasedWhenClosed=NO;host.accessibilityIdentifier=@"fixture-native-host";
 fixture_replace_windows(&fixture_windows,@[ordinary,borderless,hidden,closing,empty,helper,host]);
 for(NSWindow *w in fixture_windows)if(w!=hidden)[w orderFront:nil];
 [hidden orderOut:nil];[ordinary makeKeyAndOrderFront:nil];[ordinary makeMainWindow];
 if(NSApp.activationPolicy==NSApplicationActivationPolicyRegular)[NSApp activateIgnoringOtherApps:YES];
 fixture_phase=@"ordinary";
 struct proc_bsdinfo birth={0};BOOL have=proc_pidinfo(getpid(),PROC_PIDTBSDINFO,0,&birth,sizeof(birth))==sizeof(birth);
 emit(@{@"event":@"IDENTITY",@"pid":@(getpid()),@"birthAvailable":@(have),@"birthSec":@(birth.pbi_start_tvsec),@"birthUsec":@(birth.pbi_start_tvusec),@"bundleId":NSBundle.mainBundle.bundleIdentifier?:@"",@"bundlePath":NSBundle.mainBundle.bundlePath?:@"",@"executablePath":NSBundle.mainBundle.executablePath?:@"",@"noPEProviderLinked":@YES});
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{ snapshot(@"READY"); });
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,20*NSEC_PER_SEC),dispatch_get_main_queue(),^{
  [borderless makeKeyAndOrderFront:nil];[borderless makeMainWindow];fixture_phase=@"borderless";dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/4),dispatch_get_main_queue(),^{snapshot(@"PHASE");});
 });
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,40*NSEC_PER_SEC),dispatch_get_main_queue(),^{
  [ordinary miniaturize:nil];fixture_phase=@"miniaturized";dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/2),dispatch_get_main_queue(),^{snapshot(@"PHASE");});
 });
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,55*NSEC_PER_SEC),dispatch_get_main_queue(),^{snapshot(@"LIFETIME_STOP");[NSApp terminate:nil];});
}
- (void)applicationWillTerminate:(NSNotification *)note { (void)note;snapshot(@"EXIT"); }
@end
int main(int argc,char **argv) { @autoreleasepool {
 if(argc!=2||!argv[1][0]||argv[1][0]!='/')return 64;
 if(macdrv_ax_bridge_enabled())return 78;
 events=fopen(argv[1],"wx");if(!events)return 65;
 WineApplication *app=[WineApplication sharedApplication];
 BOOL helper=[[NSBundle.mainBundle objectForInfoDictionaryKey:@"LSUIElement"] boolValue];
 [app setActivationPolicy:helper?NSApplicationActivationPolicyAccessory:NSApplicationActivationPolicyRegular];
 NSMenu *menu=[NSMenu new];NSMenuItem *root=[NSMenuItem new];NSMenu *application=[NSMenu new];NSMenuItem *quit=[[NSMenuItem alloc] initWithTitle:@"Quit Native Root Fixture" action:@selector(terminate:) keyEquivalent:@"q"];quit.target=app;[application addItem:quit];root.submenu=application;[menu addItem:root];app.mainMenu=menu;
 FixtureDelegate *delegate=[FixtureDelegate new];app.delegate=delegate;[app run];
 fixture_replace_windows(&fixture_windows,nil);fclose(events);return 0;
} }
