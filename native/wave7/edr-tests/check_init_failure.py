#!/usr/bin/env python3
"""Compile the actual controller init tail with allocation/activation/observer mocks."""
from pathlib import Path
import hashlib,json,subprocess,re
base=Path(__file__).resolve().parent
source=base.parent/'source/winemac.drv/cocoa_app.m'
s=source.read_text(); segment=s[s.index('    - (id) init'):s.index('    - (void) dealloc')]
start=min(segment.index('            [self setupObservations];'),segment.index('            keyboardType = LMGetKbdType();'))
end=segment.rindex('        }\n        return self;')
tail=segment[start:end]
activation=s[s.index('    - (void)applicationDidBecomeActive:'):]
assert re.search(r'\{\s*macdrv_refresh_display_edr_main\(\);',activation)
assert tail.count('if (!macdrv_refresh_display_edr_main())')==1
preamble=r'''
#import <Foundation/Foundation.h>
#include <assert.h>
static BOOL outcomes[3], active, cache_ready;
static unsigned refresh_calls, live_controllers, observed;
static id observer_owner;
static BOOL macdrv_refresh_display_edr_main(void) { assert(refresh_calls<3); cache_ready=outcomes[refresh_calls++]; return cache_ready; }
static int LMGetKbdType(void) { return 0; }
@interface TestApp:NSObject
- (BOOL)isActive;
@end
@implementation TestApp
- (BOOL)isActive { return active; }
@end
static TestApp *NSApp;
@interface TestController:NSObject { int keyboardType; }
- (void)setupObservations;
- (void)applicationDidBecomeActive:(NSNotification *)notification;
@end
@implementation TestController
- (void)setupObservations { assert(!observer_owner); observer_owner=[self retain]; ++observed; }
- (void)applicationDidBecomeActive:(NSNotification *)notification { (void)notification; macdrv_refresh_display_edr_main(); }
- (void)dealloc { --live_controllers; [super dealloc]; }
- (id)init {
 self=[super init];
 if(self!=nil) { ++live_controllers;
'''
postamble=r'''
 }
 return self;
}
@end
static void scenario(BOOL app_active,BOOL first,BOOL last,BOOL expected,unsigned calls) {
 active=app_active; outcomes[0]=first; outcomes[1]=last; refresh_calls=0; cache_ready=NO; observed=0;
 TestController *controller=[[TestController alloc] init];
 assert((controller!=nil)==expected); assert(refresh_calls==calls);
 if(expected) { assert(cache_ready); assert(observed==1); [observer_owner release]; observer_owner=nil; [controller release]; }
 else { assert(!observer_owner); assert(observed==0); }
 assert(live_controllers==0);
}
int main(void) { @autoreleasepool {
 NSApp=[[TestApp alloc] init];
 scenario(YES,YES,NO,NO,2); /* exact second-refresh allocation failure */
 scenario(YES,NO,YES,YES,2); /* failed activation refresh recovered before publication */
 scenario(YES,YES,YES,YES,2);
 scenario(NO,NO,NO,NO,1);
 scenario(NO,YES,YES,YES,1);
 [NSApp release];
 puts("PASS extracted init: fail-on-second refresh rejects startup without orphan observers; final refresh recovery; active/inactive paths; full controller disposal");
} return 0; }
'''
fixture=base/'init_failure.m';fixture.write_text(preamble+tail+postamble)
exe=fixture.with_suffix('')
subprocess.run(['clang','-x','objective-c','-fno-objc-arc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(fixture),'-framework','Foundation','-o',str(exe)],check=True)
r=subprocess.run([str(exe)],text=True,capture_output=True)
print(r.stdout,end='')
result={'exit':r.returncode,'result':r.stdout.strip(),'stderr':r.stderr.strip(),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'extractedTailSha256':hashlib.sha256(tail.encode()).hexdigest(),'realControllerSetupOrAppKit':False}
(base/'init-failure-result.json').write_text(json.dumps(result,indent=2)+'\n')
if r.returncode: raise SystemExit(f'FAIL extracted controller init tail: {r.stderr}')

# These source-tail mutations model both independently observed startup holes.
guard=re.search(r"            if \(!macdrv_refresh_display_edr_main\(\)\)\n            \{\n                \[self release\];\n                return nil;\n            \}",tail)
assert guard
without_guard=tail[:guard.start()]+tail[guard.end():]
observer="            [self setupObservations];"
mutations={"guard_before_activation":guard.group()+"\n"+without_guard,
           "observer_before_final_guard":observer+"\n"+tail.replace(observer,"")}
result["negativeControls"]={}
for name,broken in mutations.items():
    neg=base/(name+".m");neg.write_text(preamble+broken+postamble)
    subprocess.run(["clang","-x","objective-c","-fno-objc-arc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",str(neg),"-framework","Foundation","-o",str(neg.with_suffix(""))],check=True)
    rejected=subprocess.run([str(neg.with_suffix(""))],text=True,capture_output=True)
    assert rejected.returncode!=0, name+" unexpectedly passed"
    result["negativeControls"][name]={"exit":rejected.returncode,"stderr":rejected.stderr.strip()}
(base/'init-failure-result.json').write_text(json.dumps(result,indent=2)+'\n')
print("PASS negative controls: guarded-before-activation and observer-before-final-guard both rejected")
