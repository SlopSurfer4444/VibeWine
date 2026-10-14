#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned CGDirectDisplayID;
enum { kCGNullDirectDisplay = 0, kCGErrorSuccess = 0 };
struct macdrv_monitor { unsigned id; NSRect rc_monitor, rc_work; unsigned char *edid; size_t edid_len; bool hdr_enabled; };
static NSArray *fixtureScreens;
static unsigned onlineCount;
static int failRealloc, reallocCalls, edidMode, edidFail;
static void *monitorAllocation;
static size_t monitorSize;
static int frees;
static void *fixture_calloc(size_t n, size_t size) { monitorSize=n*size; return monitorAllocation=calloc(n,size); }
static void *fixture_realloc(void *p, size_t size) {
    if (++reallocCalls == failRealloc) return NULL;
    void *result = realloc(p,size);
    if(result) { memset((char*)result+monitorSize, 0xa5, size-monitorSize); monitorSize=size; monitorAllocation=result; }
    return result;
}
static void *fixture_malloc(size_t size) { return edidFail ? NULL : malloc(size); }
static void fixture_free(void *p) { if(p) frees++; free(p); }
#define calloc fixture_calloc
#define realloc fixture_realloc
#define malloc fixture_malloc
#define free fixture_free
@interface FixtureScreen : NSObject { @public unsigned ident; }
+ (NSArray *)screens;
- (NSRect)frame;
- (NSRect)visibleFrame;
- (NSDictionary *)deviceDescription;
@end
@implementation FixtureScreen
+ (NSArray *)screens { return fixtureScreens; }
- (NSRect)frame { return NSMakeRect(0,0,1920,1080); }
- (NSRect)visibleFrame { return [self frame]; }
- (NSDictionary *)deviceDescription { return @{@"NSScreenNumber": @(ident)}; }
@end
#define NSScreen FixtureScreen
#undef MAC_OS_X_VERSION_10_15
static int CGGetOnlineDisplayList(unsigned max, unsigned *ids, uint32_t *count) { *count=onlineCount; for(unsigned i=0;i<onlineCount;i++) ids[i]=i+1; return 0; }
static unsigned CGDisplayMirrorsDisplay(unsigned display) { return 0; }
static unsigned CGDisplayVendorNumber(unsigned display) { return 1; }
static unsigned CGDisplayModelNumber(unsigned display) { return 1; }
static unsigned CGDisplaySerialNumber(unsigned display) { return 1; }
static NSRect convert_display_rect(NSRect a, NSRect b) { return a; }
static NSRect cgrect_win_from_mac(NSRect a) { return a; }
static CFDataRef get_edid_from_dcpav_service_proxy(unsigned a,unsigned b,unsigned c) { return NULL; }
static CFDataRef get_edid_from_io_display_edid(unsigned a,unsigned b,unsigned c) { return NULL; }
static CFDataRef generate_edid(unsigned display,unsigned a,unsigned b,unsigned c) { static unsigned char data[]={1,2,3,4}; return edidMode ? CFDataCreate(NULL,data,edidMode == 2 ? 0 : 4) : NULL; }
void macdrv_free_monitors(struct macdrv_monitor*,int);
