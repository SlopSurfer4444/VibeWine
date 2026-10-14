#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef unsigned CGDirectDisplayID;
typedef int CVReturn;
typedef unsigned long CVOptionFlags;
typedef struct { int unused; } CVTimeStamp;
typedef struct { long timeValue; long timeScale; int flags; } CVTime;
typedef struct { BOOL running; } *CVDisplayLinkRef;
enum { kCVReturnSuccess = 0, kCVReturnError = -1, kCVTimeIsIndefinite = 1 };
static int starts, stops, linksReleased;
static CVReturn CVDisplayLinkCreateWithCGDisplay(CGDirectDisplayID display, CVDisplayLinkRef *link) { *link = calloc(1, sizeof(**link)); return 0; }
static CVReturn CVDisplayLinkSetOutputCallback(CVDisplayLinkRef link, CVReturn (*cb)(CVDisplayLinkRef,const CVTimeStamp*,const CVTimeStamp*,CVOptionFlags,CVOptionFlags*,void*), void *ctx) { return 0; }
static CVReturn CVDisplayLinkStop(CVDisplayLinkRef link) { stops++; link->running = NO; return 0; }
static CVReturn CVDisplayLinkStart(CVDisplayLinkRef link) { starts++; link->running = YES; return 0; }
static void CVDisplayLinkRelease(CVDisplayLinkRef link) { linksReleased++; free(link); }
static BOOL CVDisplayLinkIsRunning(CVDisplayLinkRef link) { return link->running; }
static double CVDisplayLinkGetActualOutputVideoRefreshPeriod(CVDisplayLinkRef link) { return 0; }
static CVTime CVDisplayLinkGetNominalOutputVideoRefreshPeriod(CVDisplayLinkRef link) { return (CVTime){1,60,0}; }

static NSTimeInterval fakeNow = 100;
@interface FixtureProcessInfo : NSObject
+ (id)processInfo;
- (NSTimeInterval)systemUptime;
@end
@implementation FixtureProcessInfo
+ (id)processInfo { static id instance; if (!instance) instance = [self new]; return instance; }
- (NSTimeInterval)systemUptime { return fakeNow; }
@end
#define NSProcessInfo FixtureProcessInfo

static NSMutableArray *queued;
static NSUInteger highWater;
static void fixture_async(dispatch_queue_t queue, dispatch_block_t block) {
    @synchronized(queued) {
        id retained = [block copy];
        [queued addObject:retained];
        [retained release];
        highWater = MAX(highWater, queued.count);
    }
}
#define dispatch_async fixture_async
static NSUInteger pending(void) { @synchronized(queued) { return queued.count; } }
static void drainOne(void) {
    dispatch_block_t block;
    @synchronized(queued) {
        assert(queued.count);
        block = [[queued objectAtIndex:0] copy];
        [queued removeObjectAtIndex:0];
    }
    @try { block(); } @finally { [block release]; }
}

static int windowsReleased;
@interface WineWindow : NSObject {
@public BOOL closing, dirty, throws; int displays; void (^onDisplay)(void);
}
@property(readonly) BOOL closing;
- (BOOL)viewsNeedDisplay;
- (void)displayIfNeeded;
@end
@implementation WineWindow
- (BOOL)closing { return closing; }
- (BOOL)viewsNeedDisplay { return dirty; }
- (void)displayIfNeeded {
    if (throws) [NSException raise:@"test" format:@"display exception"];
    displays++; dirty = NO;
    if (onDisplay) onDisplay();
}
- (void)dealloc { windowsReleased++; [onDisplay release]; [super dealloc]; }
@end
