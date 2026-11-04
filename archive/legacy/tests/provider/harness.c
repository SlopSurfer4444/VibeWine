#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
/* ACTUAL_PROTOCOL */
/* ACTUAL_CONSTANTS */
typedef int BOOL;
typedef int32_t LONG;
typedef int32_t HRESULT;
typedef uint64_t ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t HWND;
typedef uint16_t WCHAR;
typedef WCHAR *BSTR;
typedef void IUnknown;
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define S_FALSE 1
#define E_FAIL ((HRESULT)0x80004005)
#define SUCCEEDED(hr) ((HRESULT)(hr)>=0)
#define FAILED(hr) ((HRESULT)(hr)<0)
#define VT_EMPTY 0
#define VT_I4 3
#define CHILDID_SELF 0
#define OBJID_CLIENT -4
#define WM_GETOBJECT 0x3d
#define SMTO_ABORTIFHUNG 2
#define SMTO_BLOCK 1
#define UlongToHandle(value) ((HWND)(value))
#define HandleToUlong(value) ((uint32_t)(value))
#define TRACE(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define WARN(...) do { if (0) printf(__VA_ARGS__); } while (0)
typedef uintptr_t WPARAM;
typedef struct {uint16_t vt; LONG lval;} VARIANT;
#define V_VT(v) ((v)->vt)
#define V_I4(v) ((v)->lval)
static const int ax_iid_accessible=1;
static WCHAR ax_cookie_property[64];
static BOOL ax_provider_abandoned;
typedef struct {LONG role,state; uint16_t role_vt,state_vt; HRESULT role_hr,state_hr,default_hr,action_hr; BOOL default_nonempty; unsigned int defaults,presses,puts; BOOL expire_after_default; unsigned int default_mutation;} IAccessible;
static uint64_t now=1000;
static BOOL lease=TRUE,owned=TRUE,owned_child=TRUE,child_owned=TRUE,window_ready=TRUE,send_ok=TRUE,allocation_ok=TRUE;
static DWORD_PTR provider_result;
static ULONG_PTR property_cookie=12;
static unsigned int lease_checks,free_calls,clears;
struct ax_binding {IAccessible *object; IUnknown *identity; VARIANT child; uint32_t role,cookie; HWND standard_hwnd; uint64_t token;};
struct ax_cache {uint32_t hwnd,count;uint64_t generation,revision;ULONGLONG timestamp;struct ax_binding bindings[MACDRV_AX_MAX_NODES];};
static struct ax_cache ax_caches[MACDRV_AX_MAX_WINDOWS];
static HRESULT mock_role(IAccessible *o,VARIANT child,VARIANT *v){v->vt=o->role_vt;v->lval=o->role;return o->role_hr;}
static HRESULT mock_state(IAccessible *o,VARIANT child,VARIANT *v){v->vt=o->state_vt;v->lval=o->state;return o->state_hr;}
static HRESULT mock_default(IAccessible *o,VARIANT child,BSTR *s){static WCHAR full[]={ 'x',0 },empty[]={0};o->defaults++;*s=o->default_nonempty?full:empty;if(o->expire_after_default)lease=FALSE;switch(o->default_mutation){case 1:property_cookie=0;break;case 2:property_cookie=13;break;case 3:owned_child=FALSE;break;case 4:child_owned=FALSE;break;}return o->default_hr;}
static HRESULT mock_press(IAccessible *o,VARIANT child){o->presses++;return o->action_hr;}
static HRESULT mock_put(IAccessible *o,VARIANT child,BSTR s){o->puts++;return o->action_hr;}
#define IAccessible_get_accRole mock_role
#define IAccessible_get_accState mock_state
#define IAccessible_get_accDefaultAction mock_default
#define IAccessible_accDoDefaultAction mock_press
#define IAccessible_put_accValue mock_put
static HRESULT ax_clear_variant(VARIANT *v){clears++;v->vt=VT_EMPTY;return S_OK;}
static void ax_free_string(BSTR s){free_calls++;}
static BSTR ax_alloc_string(const WCHAR *s){static WCHAR copy[MACDRV_AX_TEXT];if(!allocation_ok)return NULL;memcpy(copy,s,sizeof(copy));return copy;}
static BOOL ax_window_owned(uint32_t hwnd){return owned && (hwnd != 88 || owned_child);}
static BOOL IsChild(HWND parent,HWND child){return child_owned;}
static HWND GetPropW(HWND hwnd,const WCHAR *name){return property_cookie;}
static uint64_t GetTickCount64(void){return now;}
static BOOL ax_lease_valid(const struct macdrv_ax_request *r){lease_checks++;return lease && !ax_provider_abandoned;}
static BOOL ax_window_ready(const struct macdrv_ax_request *r,HWND hwnd){return window_ready;}
static BOOL SendMessageTimeoutW(HWND hwnd,unsigned int msg,WPARAM w,LONG l,unsigned int flags,unsigned int timeout,DWORD_PTR *result){*result=provider_result;return send_ok;}
static unsigned long GetLastError(void){return 0;}
static HRESULT ax_object_from_result(DWORD_PTR result,const int *iid,unsigned int w,void **replacement){*replacement=NULL;return S_OK;}
static void IAccessible_Release(IAccessible *o){}
/* ACTUAL_FUNCTIONS */
static unsigned int checks;
#define CHECK(expr) do { checks++; if(!(expr)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr);abort();}} while(0)
static IAccessible object;
static struct macdrv_ax_request request;
static void reset(uint32_t role,LONG msaa_role)
{
    memset(ax_caches,0,sizeof(ax_caches)); memset(&object,0,sizeof(object));memset(&request,0,sizeof(request));
    object.role=msaa_role;object.role_vt=object.state_vt=VT_I4;object.default_hr=object.role_hr=object.state_hr=object.action_hr=S_OK;object.default_nonempty=TRUE;
    request.hwnd=99;request.generation=5;request.revision=6;request.token=8;request.node_id=1;request.operation=MACDRV_AX_PRESS;
    ax_caches[0].hwnd=99;ax_caches[0].generation=5;ax_caches[0].revision=6;ax_caches[0].timestamp=1000;ax_caches[0].count=1;
    ax_caches[0].bindings[0].object=&object;ax_caches[0].bindings[0].role=role;ax_caches[0].bindings[0].token=8;ax_caches[0].bindings[0].child.vt=VT_I4;
    now=1000;lease=owned=owned_child=child_owned=window_ready=send_ok=allocation_ok=TRUE;ax_provider_abandoned=FALSE;property_cookie=12;provider_result=0;lease_checks=free_calls=clears=0;
}
int main(void)
{
    static const struct {LONG msaa;uint32_t bridge;} roles[]={
      {ROLE_SYSTEM_PUSHBUTTON,MACDRV_AX_ROLE_BUTTON},{ROLE_SYSTEM_TEXT,MACDRV_AX_ROLE_EDIT},
      {ROLE_SYSTEM_CHECKBUTTON,MACDRV_AX_ROLE_CHECKBOX},{ROLE_SYSTEM_RADIOBUTTON,MACDRV_AX_ROLE_RADIO},
      {ROLE_SYSTEM_GROUPING,MACDRV_AX_ROLE_GROUP},{ROLE_SYSTEM_LIST,0},{ROLE_SYSTEM_LISTITEM,0},{ROLE_SYSTEM_MENUITEM,0}};
    VARIANT self={VT_I4,0};uint32_t role,flags;LONG state;unsigned int i;
    for(i=0;i<sizeof(roles)/sizeof(roles[0]);i++) {reset(roles[i].bridge,roles[i].msaa);CHECK(ax_role_state(&object,self,&role,&state));CHECK(role==roles[i].bridge);CHECK(clears==2);}
    reset(MACDRV_AX_ROLE_CHECKBOX,ROLE_SYSTEM_CHECKBUTTON);object.role_hr=E_FAIL;CHECK(ax_role_state(&object,self,&role,&state)&&role==0);
    object.role_hr=S_FALSE;CHECK(ax_role_state(&object,self,&role,&state)&&role==0);
    object.role_hr=S_OK;object.role_vt=VT_EMPTY;CHECK(ax_role_state(&object,self,&role,&state)&&role==0);
    object.role_vt=VT_I4;object.state_hr=E_FAIL;CHECK(!ax_role_state(&object,self,&role,&state));object.state_hr=S_FALSE;CHECK(!ax_role_state(&object,self,&role,&state));object.state_hr=S_OK;object.state_vt=VT_EMPTY;CHECK(!ax_role_state(&object,self,&role,&state));
    for(i=0;i<sizeof(roles)/sizeof(roles[0]);i++) {reset(roles[i].bridge,roles[i].msaa);flags=ax_node_flags(&object,self,roles[i].bridge,STATE_SYSTEM_CHECKED|STATE_SYSTEM_MIXED);CHECK(!!(flags&MACDRV_AX_PRESSABLE)==!!ax_press_role(roles[i].bridge));CHECK(!!(flags&MACDRV_AX_MIXED)==(roles[i].bridge==4||roles[i].bridge==5));CHECK(!(flags&MACDRV_AX_CHECKED));CHECK(!!(flags&MACDRV_AX_SETTABLE)==(roles[i].bridge==3));}
    for(i=MACDRV_AX_ROLE_CHECKBOX;i<=MACDRV_AX_ROLE_RADIO;i++) {
      reset(i,i==4?ROLE_SYSTEM_CHECKBUTTON:ROLE_SYSTEM_RADIOBUTTON);flags=ax_node_flags(&object,self,i,STATE_SYSTEM_CHECKED);CHECK((flags&(MACDRV_AX_CHECKED|MACDRV_AX_PRESSABLE))==(MACDRV_AX_CHECKED|MACDRV_AX_PRESSABLE));
      flags=ax_node_flags(&object,self,i,STATE_SYSTEM_UNAVAILABLE|STATE_SYSTEM_CHECKED);CHECK(!(flags&(MACDRV_AX_ENABLED|MACDRV_AX_PRESSABLE))&&(flags&MACDRV_AX_CHECKED));
      flags=ax_node_flags(&object,self,i,STATE_SYSTEM_READONLY|STATE_SYSTEM_PROTECTED|STATE_SYSTEM_FOCUSED);CHECK(!(flags&MACDRV_AX_PRESSABLE)&&(flags&MACDRV_AX_PASSWORD)&&(flags&MACDRV_AX_FOCUSED));
      object.default_hr=S_FALSE;CHECK(!(ax_node_flags(&object,self,i,0)&MACDRV_AX_PRESSABLE));object.default_hr=E_FAIL;CHECK(!(ax_node_flags(&object,self,i,0)&MACDRV_AX_PRESSABLE));object.default_hr=S_OK;object.default_nonempty=FALSE;CHECK(!(ax_node_flags(&object,self,i,0)&MACDRV_AX_PRESSABLE));
      reset(i,i==4?ROLE_SYSTEM_CHECKBUTTON:ROLE_SYSTEM_RADIOBUTTON);CHECK(ax_action(&request)==MACDRV_AX_OK&&object.presses==1&&object.defaults==1&&lease_checks==2);
    }
    reset(1,ROLE_SYSTEM_GROUPING);CHECK(ax_action(&request)==MACDRV_AX_DENIED&&object.presses==0&&object.defaults==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);object.role_hr=S_FALSE;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);object.state_hr=S_FALSE;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_RADIOBUTTON);CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    { static const LONG denied[]={STATE_SYSTEM_UNAVAILABLE,STATE_SYSTEM_READONLY,STATE_SYSTEM_PROTECTED,STATE_SYSTEM_INVISIBLE,STATE_SYSTEM_OFFSCREEN};for(i=0;i<sizeof(denied)/sizeof(denied[0]);i++) {reset(4,ROLE_SYSTEM_CHECKBUTTON);object.state=denied[i];CHECK(ax_action(&request)==MACDRV_AX_DENIED&&object.presses==0&&object.defaults==0);} }
    reset(4,ROLE_SYSTEM_CHECKBUTTON);object.default_hr=S_FALSE;CHECK(ax_action(&request)==MACDRV_AX_DENIED&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);object.default_nonempty=FALSE;CHECK(ax_action(&request)==MACDRV_AX_DENIED&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);object.expire_after_default=TRUE;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);object.action_hr=S_FALSE;CHECK(ax_action(&request)==MACDRV_AX_UNAVAILABLE&&object.presses==1);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);object.action_hr=E_FAIL;CHECK(ax_action(&request)==MACDRV_AX_UNAVAILABLE&&object.presses==1);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);request.operation=MACDRV_AX_SET_VALUE;CHECK(ax_action(&request)==MACDRV_AX_DENIED&&object.puts==0);
    reset(3,ROLE_SYSTEM_TEXT);request.operation=MACDRV_AX_SET_VALUE;CHECK(ax_action(&request)==MACDRV_AX_OK&&object.puts==1&&object.defaults==0);
    reset(3,ROLE_SYSTEM_TEXT);request.operation=MACDRV_AX_SET_VALUE;request.value[MACDRV_AX_TEXT-1]=1;CHECK(ax_action(&request)==MACDRV_AX_DENIED&&object.puts==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);request.token++;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);now+=MACDRV_AX_CACHE_MS+1;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);request.generation++;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);request.revision++;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);request.node_id=0;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);lease=FALSE;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);ax_caches[0].bindings[0].standard_hwnd=88;ax_caches[0].bindings[0].cookie=12;CHECK(ax_action(&request)==MACDRV_AX_OK&&object.presses==1);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);ax_caches[0].bindings[0].standard_hwnd=88;ax_caches[0].bindings[0].cookie=11;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);ax_caches[0].bindings[0].standard_hwnd=88;ax_caches[0].bindings[0].cookie=12;send_ok=FALSE;CHECK(ax_action(&request)==MACDRV_AX_STALE&&ax_provider_abandoned&&object.presses==0);
    reset(4,ROLE_SYSTEM_CHECKBUTTON);ax_caches[0].bindings[0].standard_hwnd=88;ax_caches[0].bindings[0].cookie=12;provider_result=5;CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.presses==0);
    /* A COM getter is an external boundary: parent/root lease stays valid while
     * the child disappears, is replaced, changes process, or is reparented. */
    for(i=1;i<=4;i++) {
      reset(MACDRV_AX_ROLE_CHECKBOX,ROLE_SYSTEM_CHECKBUTTON);
      ax_caches[0].bindings[0].standard_hwnd=88;ax_caches[0].bindings[0].cookie=12;
      object.default_mutation=i;
      CHECK(ax_action(&request)==MACDRV_AX_STALE&&object.defaults==1&&object.presses==0&&object.puts==0&&lease);
      CHECK(lease_checks==1);
    }
    printf("PASS %u checks; actual extracted provider functions; MSAA/Win32 mocks; ASan+UBSan\n",checks);
    return 0;
}
