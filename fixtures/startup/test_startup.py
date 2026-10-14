#!/usr/bin/env python3
"""Compile actual cocoa_main.m with headless app stand-ins and real CF/NS locks.
Only imports, the five-second wait (50ms), and a deterministic post-signal
scheduling barrier are substituted. No Wine/game processes are started.
"""
from pathlib import Path
import subprocess, os, json, sys
root=Path(__file__).resolve().parent
source=Path(sys.argv[1]) if len(sys.argv)>1 else root.parents[1]/'source/dlls/winemac.drv/cocoa_main.m'
s=source.read_text()
s=s.replace('#import <AppKit/AppKit.h>', '#import <Foundation/Foundation.h>\n#include <CoreFoundation/CoreFoundation.h>\n#include <pthread.h>\n#include <unistd.h>\n#include <stdatomic.h>\n#include <assert.h>\n#include <stdio.h>')
s=s.replace('#include "macdrv_cocoa.h"','').replace('#import "cocoa_app.h"','')
preamble=r'''
static _Atomic int finished, configured, ran, released;
static int mode, result;
static id NSApp;
@interface WineApplication : NSObject
+ (id)sharedApplication;
- (void)setWineController:(id)c;
- (void)run;
@end
@interface WineApplicationController : NSObject
+ (id)sharedController;
- (void)computeEventTimeAdjustmentFromTicks:(unsigned long long)t uptime:(uint64_t)u;
@end
@implementation WineApplication
+ (id)sharedApplication { NSApp = [self new]; return NSApp; }
- (void)setWineController:(id)c {}
- (void)run { atomic_fetch_add(&ran, 1); }
- (BOOL)respondsToSelector:(SEL)s { if (mode == 5 && s == @selector(setWineController:)) return NO; return [super respondsToSelector:s]; }
@end
@implementation WineApplicationController
+ (id)sharedController { static id obj; if (!obj) obj=[self new]; return obj; }
- (void)computeEventTimeAdjustmentFromTicks:(unsigned long long)t uptime:(uint64_t)u
{
    assert(t == 123);
    atomic_fetch_add(&configured, 1);
    if (mode == 2) { while (!atomic_load(&finished)) usleep(100); }
}
@end
static void after_signal(void) {
    if (mode == 3) while (!atomic_load(&finished)) usleep(100);
}
'''
s=s.replace('#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"', preamble)
s=s.replace('free(startup_info);', 'atomic_fetch_add(&released, 1); free(startup_info);')
s=s.replace('dateWithTimeIntervalSinceNow:5','dateWithTimeIntervalSinceNow:0.05')
s=s.replace('[lock unlockWithCondition:COCOA_APP_RUNNING];','[lock unlockWithCondition:COCOA_APP_RUNNING];\n        after_signal();')
s=s.replace('source = CFRunLoopSourceCreate(NULL, 0, &source_context);', 'source = mode == 4 ? NULL : CFRunLoopSourceCreate(NULL, 0, &source_context);')
s+=r'''
static void* worker(void* unused) {
    result=macdrv_start_cocoa_app(123);
    atomic_store(&finished, 1);
    return NULL;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        mode=atoi(argv[1]); pthread_t thread;
        CFRunLoopGetMain();
        assert(!pthread_create(&thread,NULL,worker,NULL));
        if (mode == 1) { while (!atomic_load(&finished)) usleep(100); }
        else { while (!atomic_load(&finished)) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,true); }
        pthread_join(thread,NULL);
        /* Dispatch any previously pending source after the waiter's stack died. */
        for (int i=0; i<3; ++i) CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,true);
        assert(result == ((mode == 1 || mode == 2 || mode == 4) ? -1 : (mode == 5 ? 1 : 0)));
        assert(atomic_load(&configured) == ((mode == 1 || mode == 4 || mode == 5) ? 0 : 1));
        assert(atomic_load(&ran) == ((mode == 1 || mode == 4 || mode == 5) ? 0 : 1));
        printf("PASS mode=%d result=%d configured=%d run=%d released=%d\n",mode,result,atomic_load(&configured),atomic_load(&ran),atomic_load(&released));
    }
}
'''
if 'retain_startup_info' in s:
    s=s.replace('printf("PASS mode=', 'assert(atomic_load(&released) == 1);\n        printf("PASS mode=')
(root/'extracted_startup.m').write_text(s)
cmd=['clang','-g','-O1','-fsanitize=address','-fno-omit-frame-pointer','-Wall','-Wextra','-Wno-unused-parameter','-Werror','-framework','Foundation','-framework','CoreFoundation',str(root/'extracted_startup.m'),'-o',str(root/'startup-test')]
p=subprocess.run(cmd,text=True,capture_output=True)
(root/'compile.log').write_text(p.stdout+p.stderr)
if p.returncode: print(p.stderr);sys.exit(p.returncode)
results=[]
for mode in range(6):
    p=subprocess.run([str(root/'startup-test'),str(mode)],capture_output=True,text=True,timeout=10,env={**os.environ,'ASAN_OPTIONS':'detect_stack_use_after_return=1'})
    (root/f'mode-{mode}.log').write_text(p.stdout+p.stderr)
    results.append({'mode':mode,'returncode':p.returncode,'stdout':p.stdout})
(root/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
sys.exit(any(r['returncode'] for r in results))
