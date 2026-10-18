#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
using HWND=int;using UINT=unsigned;using LONG=int32_t;
struct RECT{LONG left,top,right,bottom;};struct POINT{LONG x,y;};
struct CGRect{struct{double x,y;}origin;struct{double width,height;}size;};
CGRect CGRectMake(double x,double y,double w,double h){return {{x,y},{w,h}};}
static bool retina_on=false;
// RECT_CONVERSION
// RETINA_CONVERSION
struct client_surface{HWND hwnd;};
struct macdrv_client_surface{client_surface client;void *cocoa_view;};
struct macdrv_win_data{struct{RECT client,visible;}rects;void *client_view,*cocoa_window;};
constexpr int GA_ROOT=2,MDT_RAW_DPI=2;
#define TRACE(...) ((void)0)
static macdrv_client_surface surface{{1},reinterpret_cast<void*>(11)};
static macdrv_win_data data;
static HWND ancestor=1;static bool missing=false,locked=false;
static UINT rawdpi=128,topdpi=128,virtual_width=2560,virtual_height=1440;
static POINT map_delta={0,0};
static CGRect frame{},cocoa_frame{};static void *superview,*window;
static unsigned frame_calls=0,super_calls=0,client_queries=0,dpi_queries=0,map_queries=0,get_calls=0,releases=0;
static std::vector<std::string> events;
void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
macdrv_client_surface *impl_from_client_surface(client_surface*c){check(c==&surface.client,"wrong surface");return &surface;}
HWND NtUserGetAncestor(HWND h,int type){check(h==surface.client.hwnd&&type==GA_ROOT,"bad ancestor query");events.push_back("ancestor");return ancestor;}
UINT NtUserGetWinMonitorDpi(HWND h,int type){check(!locked&&type==MDT_RAW_DPI,"DPI query under data lock");dpi_queries++;events.push_back(h==surface.client.hwnd?"dpi-child":"dpi-top");return h==surface.client.hwnd?rawdpi:topdpi;}
LONG rounded_scale(UINT n,UINT dpi){return (uint64_t(n)*dpi+48)/96;}
bool NtUserGetClientRect(HWND h,RECT*r,UINT dpi){check(!locked&&h==surface.client.hwnd,"client query under data lock");client_queries++;events.push_back("client");*r={0,0,rounded_scale(virtual_width,dpi),rounded_scale(virtual_height,dpi)};return true;}
void NtUserMapWindowPoints(HWND from,HWND to,POINT*p,UINT count,UINT dpi){check(!locked&&from==surface.client.hwnd&&to==ancestor&&count==2&&dpi==topdpi,"bad map query or query under lock");map_queries++;events.push_back("map");for(UINT i=0;i<count;i++){p[i].x+=map_delta.x;p[i].y+=map_delta.y;}}
macdrv_win_data *get_win_data(HWND h){check(h==ancestor&&!locked,"bad data lock");events.push_back("get");get_calls++;if(missing)return nullptr;locked=true;return &data;}
void release_win_data(macdrv_win_data*d){check(d==&data&&locked,"bad data release");events.push_back("release");releases++;locked=false;}
bool OffsetRect(RECT*r,LONG x,LONG y){r->left+=x;r->right+=x;r->top+=y;r->bottom+=y;return true;}
void macdrv_set_view_frame(void*v,CGRect rect){check(v==surface.cocoa_view&&locked,"frame outside data lock");events.push_back("frame");frame_calls++;frame=rect;cocoa_frame=cgrect_mac_from_win(rect);}
void macdrv_set_view_superview(void*v,void*parent,void*win,void*,void*){check(v==surface.cocoa_view&&locked,"superview outside data lock");events.push_back("super");super_calls++;superview=parent;window=win;}
// UPDATE
void reset(){surface.client.hwnd=1;ancestor=1;missing=locked=false;retina_on=false;rawdpi=topdpi=128;virtual_width=2560;virtual_height=1440;map_delta={};data={{{0,145,3420,2069},{0,0,3420,2214}},reinterpret_cast<void*>(22),reinterpret_cast<void*>(33)};frame={};cocoa_frame={};superview=window=nullptr;frame_calls=super_calls=client_queries=dpi_queries=map_queries=get_calls=releases=0;events.clear();}
void top_queries(){
#if EXPECT_TOPLEVEL_NOQUERY
 check(!client_queries&&!dpi_queries&&!map_queries,"candidate top-level queried rounded geometry");
#else
 check(client_queries==1&&dpi_queries==2&&map_queries==1,"baseline top-level query behavior changed");
#endif
}
void frame_is(double x,double y,double w,double h){check(frame_calls==1&&super_calls==1&&releases==1&&!locked,"frame/superview/release count wrong");check(frame.origin.x==x&&frame.origin.y==y&&frame.size.width==w&&frame.size.height==h,"raw frame extent/origin wrong");double scale=retina_on?2:1;check(cocoa_frame.origin.x==x/scale&&cocoa_frame.origin.y==y/scale&&cocoa_frame.size.width==w/scale&&cocoa_frame.size.height==h/scale,"Retina conversion lost geometry");check(window==data.cocoa_window,"wrong cocoa window");}
static unsigned cases=0,failures=0;
template<class F>void test(const std::string&name,F fn){try{reset();fn();cases++;}catch(const std::exception&e){failures++;fprintf(stderr,"FAIL %s: %s\n",name.c_str(),e.what());}}
int main(){
 for(bool retina:{false,true}){
 test("1440 raw3420 vs rounded3413 retina="+std::to_string(retina),[=](){retina_on=retina;check(rounded_scale(2560,128)==3413,"negative control rounding not reproduced");macdrv_client_surface_update(&surface.client);printf("{\"observation\":\"1440 raw extent\",\"retina\":%d,\"frame_width\":%.0f,\"frame_height\":%.0f,\"cocoa_width\":%.1f}\n",retina,frame.size.width,frame.size.height,cocoa_frame.size.width);frame_is(0,145,3420,1924);top_queries();check(superview==nullptr,"top-level gained parent");});
 test("1080 unchanged aspect-fit extent retina="+std::to_string(retina),[=](){retina_on=retina;virtual_width=1920;virtual_height=1080;rawdpi=topdpi=171;macdrv_client_surface_update(&surface.client);printf("{\"observation\":\"1080 aspect-fit extent\",\"retina\":%d,\"x\":%.0f,\"y\":%.0f,\"width\":%.0f,\"height\":%.0f}\n",retina,frame.origin.x,frame.origin.y,frame.size.width,frame.size.height);frame_is(0,145,3420,1924);top_queries();});
 test("normal bordered window retina="+std::to_string(retina),[=](){retina_on=retina;virtual_width=1280;virtual_height=720;rawdpi=topdpi=96;data.rects.visible={100,200,1400,960};data.rects.client={110,230,1390,950};macdrv_client_surface_update(&surface.client);frame_is(10,30,1280,720);top_queries();});
 test("negative screen origins retina="+std::to_string(retina),[=](){retina_on=retina;data.rects.visible={-3500,-250,-80,1964};data.rects.client={-3480,-220,-100,1954};macdrv_client_surface_update(&surface.client);frame_is(20,30,3380,2174);});
 test("nonempty present_rect authoritative raw extent retina="+std::to_string(retina),[=](){retina_on=retina;data.rects.visible={100,200,3520,2414};data.rects.client={140,260,2140,1460};macdrv_client_surface_update(&surface.client);check(frame.origin.x==40&&frame.origin.y==60,"present_rect changed old top-level origin");printf("{\"observation\":\"present_rect origin\",\"retina\":%d,\"x\":%.0f,\"y\":%.0f,\"width\":%.0f,\"height\":%.0f}\n",retina,frame.origin.x,frame.origin.y,frame.size.width,frame.size.height);frame_is(40,60,2000,1200);top_queries();});
 test("child parent-relative coordinates retina="+std::to_string(retina),[=](){retina_on=retina;surface.client.hwnd=2;ancestor=1;virtual_width=400;virtual_height=300;rawdpi=topdpi=144;map_delta={-35,70};data.rects.visible={-1500,100,1920,2314};data.rects.client={-1480,140,1900,2300};macdrv_client_surface_update(&surface.client);
#if EXPECT_CHILD_PARENT_RELATIVE
 frame_is(-35,70,600,450);
#else
 frame_is(-15,110,600,450);
#endif
 check(client_queries==1&&dpi_queries==2&&map_queries==1,"child query behavior changed");check(superview==data.client_view,"child parent changed");check(events==std::vector<std::string>{"ancestor","dpi-child","client","dpi-top","map","get","frame","super","release"},"child query/mapping not before data lock");});
 test("child without parent offset retained retina="+std::to_string(retina),[=](){retina_on=retina;surface.client.hwnd=2;ancestor=1;virtual_width=400;virtual_height=300;rawdpi=topdpi=144;map_delta={-35,70};data.rects.visible={-1500,100,1920,2314};data.rects.client={-1480,140,1900,2300};data.client_view=nullptr;macdrv_client_surface_update(&surface.client);frame_is(-15,110,600,450);check(client_queries==1&&dpi_queries==2&&map_queries==1,"child no-parent query behavior changed");check(superview==nullptr,"child acquired unexpected parent");});
 }
 for(bool child:{false,true})test("missing data does not publish child="+std::to_string(child),[=](){if(child)surface.client.hwnd=2;missing=true;macdrv_client_surface_update(&surface.client);check(get_calls==1&&!frame_calls&&!super_calls&&!releases&&!locked,"missing data published frame or lock leaked");if(child)check(client_queries==1&&map_queries==1,"child missing-data query order changed");else top_queries();});
 test("empty authoritative rect conversion",[]{data.rects.visible={10,20,100,100};data.rects.client={20,30,20,80};macdrv_client_surface_update(&surface.client);frame_is(10,10,0,0);});
 printf("{\"passed\":%s,\"cases\":%u,\"failures\":%u,\"wineStarted\":false}\n",failures?"false":"true",cases,failures);return failures?1:0;
}
