#import <Foundation/Foundation.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static NSArray *test_screens;
static double test_base=2;
static BOOL fail_alloc;
static void *test_calloc(size_t count,size_t size) {return fail_alloc?NULL:calloc(count,size);}
@interface FakeScreen:NSObject
@property unsigned id;
+ (NSArray*)screens;
- (NSDictionary*)deviceDescription;
@property(readonly) double maximumExtendedDynamicRangeColorComponentValue;
@property(readonly) double maximumReferenceExtendedDynamicRangeColorComponentValue;
@property(readonly) double maximumPotentialExtendedDynamicRangeColorComponentValue;
@end
@implementation FakeScreen
+ (NSArray*)screens {assert([NSThread isMainThread]);return test_screens;}
- (NSDictionary*)deviceDescription {assert([NSThread isMainThread]);return @{ @"NSScreenNumber":@(self.id) };}
- (double)maximumExtendedDynamicRangeColorComponentValue {assert([NSThread isMainThread]);return test_base;}
- (double)maximumReferenceExtendedDynamicRangeColorComponentValue {assert([NSThread isMainThread]);return test_base+1;}
- (double)maximumPotentialExtendedDynamicRangeColorComponentValue {assert([NSThread isMainThread]);return test_base+2;}
@end
#define NSScreen FakeScreen
#define calloc test_calloc
#include "wave7_display_edr.inc"
#undef calloc
static _Atomic unsigned reads;
static void *reader(void *ignored) {
 (void)ignored;
 for(unsigned i=0;i<100000;i++) {
  double v[3]; assert(macdrv_copy_display_edr_v1(42,v)==1);
  assert(v[1]==v[0]+1 && v[2]==v[0]+2);
  atomic_fetch_add(&reads,1);
 }
 return NULL;
}
int main(void) {@autoreleasepool {
 double v[3]={-1,-2,-3};assert(!macdrv_copy_display_edr_v1(42,v));assert(v[0]==-1);
 FakeScreen *screen=[FakeScreen new];screen.id=42;test_screens=@[screen];
 assert(macdrv_refresh_display_edr_main());
 assert(macdrv_copy_display_edr_v1(42,v) && v[0]==2 && v[2]==4);
 pthread_t workers[3];for(unsigned i=0;i<3;i++)assert(!pthread_create(&workers[i],NULL,reader,NULL));
 for(unsigned i=0;i<10000;i++){@autoreleasepool {test_base=i+2;assert(macdrv_refresh_display_edr_main());}}
 for(unsigned i=0;i<3;i++)pthread_join(workers[i],NULL);assert(reads==300000);
 screen.id=43;test_base=888;assert(macdrv_refresh_display_edr_main());v[0]=-1;
 assert(!macdrv_copy_display_edr_v1(42,v) && v[0]==-1);assert(macdrv_copy_display_edr_v1(43,v)&&v[0]==888);
 fail_alloc=YES;assert(!macdrv_refresh_display_edr_main());v[0]=-1;assert(!macdrv_copy_display_edr_v1(43,v)&&v[0]==-1);
 fail_alloc=NO;assert(macdrv_refresh_display_edr_main());assert(macdrv_copy_display_edr_v1(43,v));
 test_screens=@[];assert(macdrv_refresh_display_edr_main());assert(!macdrv_copy_display_edr_v1(43,v));
 assert(!macdrv_copy_display_edr_v1(43,NULL));[screen release];
 puts("PASS native EDR: pre-init unavailable; exact cold copy; 300k concurrent copies; topology replacement; allocation failure invalidation; recovery; empty screen; no AppKit reads from workers");
}}
