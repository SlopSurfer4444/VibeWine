#import <Foundation/Foundation.h>
#include <crt_externs.h>
#include <assert.h>
#include <stdbool.h>
static int fake_argc;
static char **fake_argv;
static int *fixture_argc(void) { return &fake_argc; }
static char ***fixture_argv(void) { return &fake_argv; }
#define _NSGetArgc fixture_argc
#define _NSGetArgv fixture_argv
#define CAMetalLayer TestLayer
#define kCAGravityResizeAspect @"aspect-fit"
#define kCGColorBlack 0
static id CGColorGetConstantColor(int value) { assert(value==0); return @"black"; }
@interface TestLayer:NSObject { id contentsGravity,backgroundColor; }
@property(assign) id contentsGravity,backgroundColor;
@end
@implementation TestLayer
@synthesize contentsGravity,backgroundColor;
@end
#include "wave7_ds3_profile.inc"
static void check(TestLayer *layer,int argc,char **argv,const char *marshal,const char *aspect,bool expected) {
 fake_argc=argc;fake_argv=argv;
 if(marshal) setenv("DXMT_DS3_MARSHAL_FIX",marshal,1);else unsetenv("DXMT_DS3_MARSHAL_FIX");
 if(aspect) setenv("DXMT_DS3_ASPECT_FIT",aspect,1);else unsetenv("DXMT_DS3_ASPECT_FIT");
 layer.contentsGravity=@"untouched";layer.backgroundColor=@"untouched";
 w7ds_apply_aspect_fit_main(layer);
 assert([layer.contentsGravity isEqual:expected?@"aspect-fit":@"untouched"]);
 assert([layer.backgroundColor isEqual:expected?@"black":@"untouched"]);
}
int main(void) { @autoreleasepool {
 TestLayer *layer=[TestLayer new];
 char *posix[]={"/wine/bin/wine","/game/dArKsOuLsIII.ExE","other"};
 char *windows[]={"C:\\Game\\DarkSoulsIII.exe"};
 char *wrong[]={"wine","Other.exe","DarkSoulsIII.exe"};
 char *suffix[]={"wine","DarkSoulsIII.exe.bak"};
 check(layer,2,posix,"1","1",true);check(layer,1,windows,"1","1",true);
 check(layer,0,posix,"1","1",false);check(layer,3,wrong,"1","1",false);
 check(layer,2,suffix,"1","1",false);
 const char *disabled[]={NULL,"","0","01","true","1x"};
 for(unsigned i=0;i<sizeof(disabled)/sizeof(*disabled);++i){
  check(layer,2,posix,disabled[i],"1",false);check(layer,2,posix,"1",disabled[i],false);
 }
 [layer release];puts("PASS: DS3 exact environment/executable gates and aspect properties");
 }return 0;}
