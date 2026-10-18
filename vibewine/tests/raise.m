/* Compiles the shipping native identity/action include against lifecycle seams.
 * Tests request admission, refusal, and exception/reentry boundaries; it does
 * not claim actual AppKit activation, Windows focus, or game behavior. */
#import <Foundation/Foundation.h>
#include <assert.h>
#import <dispatch/dispatch.h>
typedef NSUInteger NSWindowCollectionBehavior;
enum{NSWindowCollectionBehaviorCanJoinAllSpaces=1,NSWindowCollectionBehaviorMoveToActiveSpace=2};
static int behaviorWrites;static BOOL throwOrder;
typedef NSString *NSAccessibilityActionName;
static NSString *NSAccessibilityRaiseAction=@"AXRaise";
static NSString *NSAccessibilityWindowRole=@"AXWindow";
static BOOL enabled=YES,active=NO,allowActivation=YES,deferFocus=NO,reenter=NO; static int orders,activations,fallback;
static BOOL wine_ax_window_root_enabled(void){return enabled;}
@interface Base:NSObject
-(BOOL)isAccessibilityElement;-(NSString*)accessibilityRole;-(id)accessibilityParent;-(NSString*)accessibilityIdentifier;
-(BOOL)accessibilityPerformRaise;
-(void)accessibilityPerformAction:(NSAccessibilityActionName)a;
@end
@implementation Base
-(BOOL)isAccessibilityElement{return NO;}-(NSString*)accessibilityRole{return @"base";}-(id)accessibilityParent{return nil;}-(NSString*)accessibilityIdentifier{return @"base";}
-(BOOL)accessibilityPerformRaise{fallback++;return YES;}
-(void)accessibilityPerformAction:(NSAccessibilityActionName)a{fallback++;}
@end
@interface FakeApp:NSObject
-(void)activateIgnoringOtherApps:(BOOL)b;-(BOOL)isActive;-(BOOL)isHidden;-(NSInteger)activationPolicy;
@end
@implementation FakeApp
-(void)activateIgnoringOtherApps:(BOOL)b{assert(b);activations++;active=allowActivation;}
-(BOOL)isActive{return active;}-(BOOL)isHidden{return NO;}-(NSInteger)activationPolicy{return 0;}
@end
static FakeApp *testapp;
#define NSApp testapp
@interface WineApplicationController:NSObject
+(id)sharedController;-(void)tryToActivateIgnoringOtherApps:(BOOL)b;
@end
@implementation WineApplicationController
+(id)sharedController{static id x; if(!x)x=[self new];return x;}
-(void)tryToActivateIgnoringOtherApps:(BOOL)b{activations++;active=allowActivation;}
@end
@interface Window:Base { @public void *hwnd; }
@property BOOL closing,disabled,noForeground,keyable,visible,key,onSpace;
@property(nonatomic) NSUInteger collectionBehavior;
-(BOOL)isOnActiveSpace;-(BOOL)isVisible;-(BOOL)isMiniaturized;-(NSInteger)level;
-(BOOL)canBecomeKeyWindow;-(BOOL)presentsVisibleContent;-(BOOL)isKeyWindow;-(void)makeKeyAndOrderFront:(id)x;
@end
@implementation Window
@synthesize collectionBehavior=_collectionBehavior;
-(void)setCollectionBehavior:(NSUInteger)x{behaviorWrites++;_collectionBehavior=x;}
-(BOOL)isOnActiveSpace{return self.onSpace;}-(BOOL)isVisible{return self.visible;}-(BOOL)isMiniaturized{return NO;}-(NSInteger)level{return 0;}
-(BOOL)canBecomeKeyWindow{return self.keyable;}-(BOOL)presentsVisibleContent{return self.visible;}-(BOOL)isKeyWindow{return self.key;}
-(void)makeKeyAndOrderFront:(id)x{orders++;if(throwOrder)@throw [NSException exceptionWithName:@"Expected" reason:nil userInfo:nil];if(self.collectionBehavior&2)self.onSpace=YES;if(reenter)assert(![self accessibilityPerformRaise]);if(!deferFocus)self.key=YES;}
#include "../../dlls/winemac.drv/ax_window_identity.inc"

@end
int main(){@autoreleasepool{testapp=[FakeApp new];Window*w=[Window new];w->hwnd=(void*)1;w.keyable=YES;w.visible=YES;w.onSpace=NO;
allowActivation=NO;deferFocus=YES;assert([w accessibilityPerformRaise]);assert(orders==1&&activations==1&&!active&&!w.key);/* submission not completion */
w.closing=YES;assert(![w accessibilityPerformRaise]);w.closing=NO;w.disabled=YES;assert(![w accessibilityPerformRaise]);w.disabled=NO;w.noForeground=YES;assert(![w accessibilityPerformRaise]);w.noForeground=NO;w.keyable=NO;assert(![w accessibilityPerformRaise]);w.keyable=YES;w.visible=NO;assert(![w accessibilityPerformRaise]);w.visible=YES;assert(orders==1&&activations==1);
enabled=NO;assert([w accessibilityPerformRaise]);assert(fallback==1);enabled=YES;w->hwnd=NULL;assert([w accessibilityPerformRaise]);assert(fallback==2);w->hwnd=(void*)1;
assert([w accessibilityPerformRaise]);assert(orders==2&&activations==2);
dispatch_group_t g=dispatch_group_create();dispatch_group_async(g,dispatch_get_global_queue(0,0),^{assert(![w accessibilityPerformRaise]);});dispatch_group_wait(g,DISPATCH_TIME_FOREVER);assert(orders==2);
reenter=YES;assert([w accessibilityPerformRaise]);reenter=NO;assert(orders==3&&activations==3);assert(behaviorWrites==0);
throwOrder=YES;@try{[w accessibilityPerformRaise];assert(0);}@catch(NSException*e){}throwOrder=NO;assert([w accessibilityPerformRaise]);assert(orders==5&&activations==4);
puts("PASS request-admitted while inactive and focus deferred; guards, fallback, nonmain, reentry; no Space mutation");}}
