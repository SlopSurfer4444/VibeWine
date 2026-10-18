#import <AppKit/AppKit.h>
#include <pthread.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <string>
#include "wine/list.h"
using HWND=int;using UINT=unsigned;using LONG=int32_t;using ULONG=unsigned;using HDC=int;
struct RECT{LONG left,top,right,bottom;};struct POINT{LONG x,y;};
static bool retina_on=false,fail_alloc=false,fail_native=false,fail_bridge_alloc=false,fail_array_alloc=false;
static unsigned bridge_allocs=0,bridge_frees=0,array_allocs=0,array_releases=0;
static void *bridge_calloc(size_t n,size_t z){if(fail_bridge_alloc)return nullptr;bridge_allocs++;return calloc(n,z);}
static void bridge_free(void*p){if(p)bridge_frees++;free(p);}
static CFMutableArrayRef bridge_array_create(CFAllocatorRef a,CFIndex c,const CFArrayCallBacks*b){if(fail_array_alloc)return nullptr;array_allocs++;return CFArrayCreateMutable(a,c,b);}
static void bridge_array_release(CFTypeRef a){array_releases++;CFRelease(a);}
static unsigned observer_calls=0;
static unsigned allocations=0,frees=0,views=0,disposals=0,hidden_calls=0,frames=0,parents=0,client_queries=0;
static void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
static void *tracked_calloc(size_t n,size_t z){if(fail_alloc)return nullptr;allocations++;return calloc(n,z);}
static void tracked_free(void*p){frees++;free(p);}
@interface WineContentView:NSView
-(void)updateGLContexts;
@end
@implementation WineContentView
-(id)initWithFrame:(NSRect)r{if(fail_native){[self release];return nil;}self=[super initWithFrame:r];if(self)views++;return self;}
-(BOOL)isFlipped{return YES;}
-(void)updateGLContexts{}
@end
@interface RecordingCenter:NSNotificationCenter
@end
@implementation RecordingCenter
-(void)addObserver:(id)o selector:(SEL)s name:(NSNotificationName)n object:(id)obj{observer_calls++;check(o,"native nil observer registration");[super addObserver:o selector:s name:n object:obj];}
@end
static NSNotificationCenter* fixture_center;
using macdrv_view=void*;using macdrv_window=void*;
static void OnMainThread(void (^b)(void)){b();}
#define TRACE(...) ((void)0)
// RECT_CONVERSION
// RETINA_CONVERSION
// NATIVE_CREATE
struct client_surface;
struct client_surface_funcs{void(*destroy)(client_surface*);void(*detach)(client_surface*);void(*update)(client_surface*);void(*present)(client_surface*,HDC);};
struct client_surface{const client_surface_funcs*funcs;list entry;LONG ref;HWND hwnd;LONG updated,offscreen;};
struct macdrv_client_surface{client_surface client;void *cocoa_view,*metal_view,*metal_device;};
struct macdrv_win_data{HWND hwnd;struct{RECT client,visible,window;}rects;void *client_view,*cocoa_window,*drag_event;CFMutableArrayRef d3dmetal_client_surfaces;int pixel_format;unsigned on_screen,shaped,layered,ulw_layered,per_pixel_alpha,minimized;};
static pthread_mutex_t surfaces_lock=PTHREAD_MUTEX_INITIALIZER;
static list client_surfaces=LIST_INIT(client_surfaces);
static macdrv_win_data data,child_data;
static WineContentView *rootview,*oldview;
static HWND ancestor=1;static unsigned rawdpi=144,topdpi=144,w=400,h=300;static POINT delta={-35,70};static bool locked=false,missing=false;
static macdrv_client_surface *impl_from_client_surface(client_surface*c){return reinterpret_cast<macdrv_client_surface*>(c);}
static HWND NtUserGetAncestor(HWND,int){return ancestor;}
static UINT NtUserGetWinMonitorDpi(HWND n,int){check(!locked,"DPI query under data lock");return n==ancestor?topdpi:rawdpi;}
static void NtUserGetClientRect(HWND,RECT*r,UINT d){check(!locked,"query under data lock");client_queries++;*r={0,0,LONG((uint64_t(w)*d+48)/96),LONG((uint64_t(h)*d+48)/96)};}
static void NtUserMapWindowPoints(HWND,HWND,POINT*p,unsigned n,UINT){check(!locked,"mapping under data lock");for(unsigned i=0;i<n;i++){p[i].x+=delta.x;p[i].y+=delta.y;}}
static macdrv_win_data *get_win_data(HWND hwnd){if(missing)return nullptr;check(!locked,"recursive data lock");locked=true;return hwnd==ancestor?&data:&child_data;}
static void release_win_data(macdrv_win_data*){check(locked,"unbalanced unlock");locked=false;}
static bool OffsetRect(RECT*r,LONG x,LONG y){r->left+=x;r->right+=x;r->top+=y;r->bottom+=y;return true;}
static void macdrv_set_view_hidden(void*v,bool b){check(v,"NULL view hidden");hidden_calls++;[(NSView*)v setHidden:b];}
static void macdrv_set_view_frame(void*v,CGRect r){check(v,"NULL frame");frames++;[(NSView*)v setFrame:NSRectFromCGRect(cgrect_mac_from_win(r))];}
static void macdrv_set_view_superview(void*v,void*p,void*,void*,void*){check(v,"NULL parent view");parents++;[(p?(NSView*)p:rootview) addSubview:(NSView*)v];}
static void macdrv_dispose_view(void*v){check(v,"NULL dispose");disposals++;[fixture_center removeObserver:(id)v];[(NSView*)v removeFromSuperview];[(NSView*)v release];}
static void macdrv_view_release_metal_view(void*){}
static void macdrv_release_metal_device(void*){}
static LONG InterlockedDecrement(LONG*p){return --*p;}
#define calloc tracked_calloc
#define free tracked_free
// CORE_CREATE
// CORE_RELEASE
#undef calloc
#undef free
// DESTROY
// DETACH
// UPDATE
// PRESENT
static const client_surface_funcs macdrv_client_surface_funcs={macdrv_client_surface_destroy,macdrv_client_surface_detach,macdrv_client_surface_update,macdrv_client_surface_present};
constexpr int GA_ROOT=2,MDT_RAW_DPI=2;
// CREATE
// D3DMETAL_CALLER
static std::string filter;
static unsigned cases=0,failures=0;
static void reset(){check(list_empty(&client_surfaces),"leaked list entry");retina_on=fail_alloc=fail_native=missing=locked=fail_bridge_alloc=fail_array_alloc=false;bridge_allocs=bridge_frees=array_allocs=array_releases=0;observer_calls=0;allocations=frees=views=disposals=hidden_calls=frames=parents=client_queries=0;ancestor=1;rawdpi=topdpi=144;w=400;h=300;delta={-35,70};rootview=[[WineContentView alloc] initWithFrame:NSMakeRect(0,0,4000,3000)];oldview=[[WineContentView alloc] initWithFrame:NSMakeRect(20,40,1000,800)];[oldview setHidden:NO];[rootview addSubview:oldview];data={};child_data={};data.hwnd=1;child_data.hwnd=2;data.rects.visible={-1500,100,1920,2314};data.rects.client={-1480,140,1900,2300};data.client_view=oldview;views=0;}
static void cleanup(){[oldview removeFromSuperview];[oldview release];[rootview release];}
template<class F>static void test(std::string name,F f){if(!filter.empty()&&name.find(filter)==std::string::npos)return;try{reset();f();check(!locked,"lock leaked");check(list_empty(&client_surfaces),"surface list leaked");cleanup();cases++;}catch(const std::exception&e){failures++;fprintf(stderr,"FAIL %s: %s\n",name.c_str(),e.what());}catch(NSException*e){failures++;fprintf(stderr,"EXCEPTION %s: %s\n",name.c_str(),[[e reason] UTF8String]);}}
static void expectRect(NSRect r,double x,double y,double rw,double rh){check(r.origin.x==x&&r.origin.y==y&&r.size.width==rw&&r.size.height==rh,"native composed rectangle mismatch");}
int main(int argc,char**argv){if(argc>1)filter=argv[1];@autoreleasepool{fixture_center=[RecordingCenter new];
 test("client allocation failure",[]{fail_alloc=true;auto*s=macdrv_client_surface_create(1);check(!s,"failed allocation returned surface");check(allocations==0&&frees==0&&views==0&&hidden_calls==0&&frames==0&&parents==0,"allocation failure had side effects");check(data.client_view==oldview&&![oldview isHidden],"old view lost");});
 test("native allocation failure",[]{fail_native=true;auto*s=macdrv_client_surface_create(1);check(!s,"native failure returned incomplete surface");check(allocations==1&&frees==1&&views==0&&observer_calls==0&&disposals==0&&hidden_calls==0&&frames==0&&parents==0,"native fail unwind mismatch");check(data.client_view==oldview&&![oldview isHidden],"old view lost");});
 test("d3dmetal bridge calloc failure",[]{fail_bridge_alloc=true;check(!my_get_win_data(1),"bridge allocation failure accepted");check(!allocations&&!views&&!bridge_allocs&&!bridge_frees&&!array_allocs,"surface touched after bridge allocation failure");check(data.client_view==oldview&&![oldview isHidden]&&!locked,"old view lost");});
 test("d3dmetal array allocation failure",[]{fail_array_alloc=true;check(!my_get_win_data(1),"array allocation failure accepted");check(!allocations&&!views&&bridge_allocs==1&&bridge_frees==1&&!array_allocs,"bridge unwind mismatch");check(data.client_view==oldview&&![oldview isHidden]&&!locked,"old view lost");});
 test("d3dmetal success owned bridge lifetime",[]{auto*b=my_get_win_data(1);check(b&&locked,"bridge not returned locked");check(data.d3dmetal_client_surfaces&&CFArrayGetCount(data.d3dmetal_client_surfaces)==1,"bridge surface not retained");check(b->client_cocoa_view==data.client_view&&[oldview isHidden],"success view not published");release_win_data(&data);bridge_free(b);bridge_array_release(data.d3dmetal_client_surfaces);data.d3dmetal_client_surfaces=nullptr;check(allocations==1&&frees==1&&disposals==1&&bridge_allocs==bridge_frees&&array_allocs==array_releases,"bridge lifetime mismatch");});
 test("d3dmetal caller allocation failure",[]{fail_alloc=true;check(!my_get_win_data(1),"caller returned success after create failure");check(!locked&&data.client_view==oldview&&![oldview isHidden],"caller failure changed old view");check(bridge_allocs==bridge_frees&&array_allocs==array_releases,"bridge failure leaked");});
 test("d3dmetal caller native failure",[]{fail_native=true;check(!my_get_win_data(1),"caller returned success after native failure");check(frees==1&&!locked&&data.client_view==oldview&&![oldview isHidden],"caller unwind mismatch");check(bridge_allocs==bridge_frees&&array_allocs==array_releases,"bridge native failure leaked");});
 test("native null rectangle",[]{auto*v=macdrv_create_view(CGRectNull);check(v,"null rect no view");expectRect([(NSView*)v frame],0,0,0,0);macdrv_dispose_view(v);});
 for(bool retina:{false,true})for(bool parent:{false,true})for(UINT dpi:{96u,144u,171u})test("child composition retina="+std::to_string(retina)+" parent="+std::to_string(parent)+" dpi="+std::to_string(dpi),[=]{retina_on=retina;rawdpi=dpi;topdpi=128;w=401;h=303;delta={-35,71};double scale=retina?2:1;[oldview setFrame:NSMakeRect(20/scale,40/scale,1000,800)];if(!parent)data.client_view=nullptr;auto*s=macdrv_client_surface_create(2);check(s,"create failed");auto*v=(NSView*)s->cocoa_view;auto r=[v convertRect:[v bounds] toView:rootview];expectRect(r,(-35+20)/scale,(71+40)/scale,((uint64_t(w)*dpi+48)/96)/scale,((uint64_t(h)*dpi+48)/96)/scale);check([v superview]==(parent?oldview:rootview),"wrong native parent");check(data.client_view==(parent?oldview:nullptr)&&![oldview isHidden],"child publication altered top-level view");client_surface_release(&s->client);check(frees==1&&disposals==1,"success cleanup mismatch");});
 test("nested descendant maps to root client",[]{retina_on=true;rawdpi=171;topdpi=128;delta={-135,271};w=401;h=303;[oldview setFrame:NSMakeRect(10,20,1000,800)];auto*s=macdrv_client_surface_create(3);check(s,"nested create failed");auto*v=(NSView*)s->cocoa_view;expectRect([v convertRect:[v bounds] toView:rootview],(-135+20)/2.,(271+40)/2.,714/2.,540/2.);check(data.client_view==oldview&&![oldview isHidden],"nested publication altered root");client_surface_release(&s->client);});
 for(bool retina:{false,true})test("top-level1440 authoritative raw extent retina="+std::to_string(retina),[=]{retina_on=retina;rawdpi=topdpi=128;w=2560;h=1440;data.rects.visible={0,0,3420,2214};data.rects.client={0,145,3420,2069};auto*s=macdrv_client_surface_create(1);check(s,"create failed");double scale=retina?2:1;expectRect([(NSView*)s->cocoa_view frame],0,145/scale,3420/scale,1924/scale);check(data.client_view==s->cocoa_view&&[oldview isHidden],"success not published");client_surface_release(&s->client);check(data.client_view==nullptr&&frees==1&&disposals==1,"success detach mismatch");});
 printf("{\"passed\":%s,\"cases\":%u,\"failures\":%u,\"nativeNSView\":true,\"wineStarted\":false}\n",failures?"false":"true",cases,failures);return failures?1:0;
}}
