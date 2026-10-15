/* Foundation-only predicate/merge regression tests. No app/window is allocated. */
#import <Cocoa/Cocoa.h>
#include <assert.h>
@interface WineApplication : NSApplication @end
@implementation WineApplication @end
@interface WineWindow : NSObject
@property void *hwnd;
@property BOOL closing, visible, miniaturized, content;
- (BOOL)isVisible;
- (BOOL)isMiniaturized;
- (BOOL)presentsVisibleContent;
@end
@implementation WineWindow
- (BOOL)isVisible { return _visible; }
- (BOOL)isMiniaturized { return _miniaturized; }
- (BOOL)presentsVisibleContent { return _content; }
@end
typedef void *macdrv_window;
static void *macdrv_get_window_hwnd(macdrv_window window) { return [(WineWindow *)window hwnd]; }
#include "../source/winemac.drv/ax_application.inc"
static unsigned checks;
#define CHECK(x) do { assert(x); ++checks; } while(0)
static BOOL allow(NSDictionary *info, NSString *identifier, BOOL actual, NSInteger policy, BOOL app, BOOL executable)
{ return wine_ax_native_root_metadata_eligible(info,identifier,actual,policy,app,executable); }
static WineWindow *window(BOOL visible,BOOL mini,BOOL closing,BOOL content,void *hwnd)
{ WineWindow *w=[WineWindow new];w.visible=visible;w.miniaturized=mini;w.closing=closing;w.content=content;w.hwnd=hwnd;return w; }
int main(void) { @autoreleasepool {
 NSDictionary *good=@{@"WineNativeIdentity":@YES,@"NSPrincipalClass":@"WineApplication",@"LSUIElement":@NO};
 CHECK(allow(good,@"local.wine.native",YES,NSApplicationActivationPolicyRegular,YES,YES));
 for(id bad in @[@NO,@1,@"1",@"YES",[NSNull null]]) { NSMutableDictionary *d=[good mutableCopy];d[@"WineNativeIdentity"]=bad;CHECK(!allow(d,@"local.wine.native",YES,0,YES,YES)); }
 NSMutableDictionary *d=[good mutableCopy];[d removeObjectForKey:@"WineNativeIdentity"];CHECK(!allow(d,@"local.wine.native",YES,0,YES,YES));
 for(id bad in @[@"OtherApplication",@1,[NSNull null]]) { d=[good mutableCopy];d[@"NSPrincipalClass"]=bad;CHECK(!allow(d,@"local.wine.native",YES,0,YES,YES)); }
 d=[good mutableCopy];[d removeObjectForKey:@"NSPrincipalClass"];CHECK(!allow(d,@"local.wine.native",YES,0,YES,YES));
 for(NSString *key in @[@"LSUIElement",@"LSBackgroundOnly"])for(id bad in @[@YES,@1,@"NO",[NSNull null]]) { d=[good mutableCopy];d[key]=bad;CHECK(!allow(d,@"local.wine.native",YES,0,YES,YES)); }
 CHECK(!allow(good,@"",YES,0,YES,YES));CHECK(!allow(good,nil,YES,0,YES,YES));CHECK(!allow(good,@"local.wine.native",NO,0,YES,YES));
 CHECK(!allow(good,@"local.wine.native",YES,NSApplicationActivationPolicyAccessory,YES,YES));CHECK(!allow(good,@"local.wine.native",YES,NSApplicationActivationPolicyProhibited,YES,YES));
 CHECK(!allow(good,@"local.wine.native",YES,0,NO,YES));CHECK(!allow(good,@"local.wine.native",YES,0,YES,NO));
 /* No active/key/frontmost fact participates in admission. */
 CHECK(allow(good,@"local.wine.native",YES,NSApplicationActivationPolicyRegular,YES,YES));
 WineWindow *a=window(YES,NO,NO,YES,(void*)1),*b=window(YES,NO,NO,YES,(void*)2),*hidden=window(NO,NO,NO,YES,(void*)3),*closing=window(YES,NO,YES,YES,(void*)4),*empty=window(YES,NO,NO,NO,(void*)5),*helper=window(YES,NO,NO,YES,NULL),*mini=window(NO,YES,NO,YES,(void*)6);
 NSObject *menu=[NSObject new],*native_helper=[NSObject new];
 NSArray *merged=wine_ax_merge_application_elements(@[menu,a,closing,hidden,helper,empty,native_helper],@[a,b,hidden,closing,empty,helper,mini]);
 CHECK(merged.count==5);CHECK([merged containsObject:menu]);CHECK([merged containsObject:native_helper]);CHECK([merged containsObject:a]);CHECK([merged containsObject:b]);CHECK([merged containsObject:mini]);
 CHECK(![merged containsObject:hidden]);CHECK(![merged containsObject:closing]);CHECK(![merged containsObject:empty]);CHECK(![merged containsObject:helper]);CHECK([merged indexOfObject:a]==1);
 unsetenv("WINE_MACDRV_AX_BRIDGE");CHECK(!macdrv_ax_bridge_enabled());CHECK(!wine_ax_window_root_enabled());
 setenv("WINE_MACDRV_AX_BRIDGE","1",1);CHECK(macdrv_ax_bridge_enabled());CHECK(wine_ax_window_root_enabled());
 setenv("WINE_MACDRV_AX_BRIDGE","true",1);CHECK(!macdrv_ax_bridge_enabled());unsetenv("WINE_MACDRV_AX_BRIDGE");
 printf("PASS %u native metadata/merge/legacy-gate checks; no AppKit application or window allocated\n",checks);
 return 0;
} }
