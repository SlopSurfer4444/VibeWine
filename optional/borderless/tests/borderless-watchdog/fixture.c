#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
typedef uint64_t ULONGLONG;
typedef unsigned long DWORD;
typedef uintptr_t ULONG_PTR;
typedef intptr_t LONG_PTR;
#define GWL_STYLE -16
#define GWL_EXSTYLE -20
#define SWP_FRAMECHANGED 32
#define SWP_NOACTIVATE 16
#define SWP_ASYNCWINDOWPOS 16384
typedef struct { long left,top,right,bottom; } RECT;
static DWORD last_error;
static void SetLastError(DWORD value) { last_error=value; }
static DWORD GetLastError(void) { return last_error; }


typedef void *HWND;
static LONG_PTR SetWindowLongPtrW(HWND,int,LONG_PTR);
static int SetWindowPos(HWND,HWND,int,int,int,int,unsigned);
typedef pthread_mutex_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
#define WINAPI
#define INFINITE 0xffffffffUL
static ULONGLONG ticks,ready_at;
static int absent,selection_calls,exit_code,stopped;
static jmp_buf escape;
static ULONGLONG GetTickCount64(void) { return ticks; }
static void AcquireSRWLockExclusive(SRWLOCK *lock) { if (pthread_mutex_lock(lock)) abort(); }
static void ReleaseSRWLockExclusive(SRWLOCK *lock) { if (pthread_mutex_unlock(lock)) abort(); }
static void Sleep(DWORD ms) { if(ms==INFINITE) { stopped=1;longjmp(escape,1); } ticks+=ms; }
static void ExitProcess(unsigned code) { exit_code=code;longjmp(escape,1); }
/* WATCH_CODE */
typedef struct { DWORD pid; HWND window; } Target;
/* WRITE_METHODS */
static int writes,bad_write_context,expire_write;
static LONG_PTR SetWindowLongPtrW(HWND h,int index,LONG_PTR value) {
 (void)h;(void)index;(void)value;writes++;
 WatchState state=watch_snapshot(),expired;
 if(!state.mutation_started) bad_write_context++;
 if(expire_write) { ticks+=5000;watch_claim_timeout(state.generation,&expired); }
 return 1;
}
static int SetWindowPos(HWND h,HWND after,int x,int y,int w,int height,unsigned flags) {
 (void)h;(void)after;(void)x;(void)y;(void)w;(void)height;(void)flags;writes++;
 if(!watch_snapshot().mutation_started || strcmp(watch_snapshot().phase,"position_window")) bad_write_context++;
 return 1;
}
static int select_target(Target *target) { (void)target;selection_calls++;return absent || ticks<ready_at ? 2:0; }
/* WAIT_TARGET */
static unsigned assertions;
#define CHECK(x) do { assertions++;if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1; } } while(0)
static void reset(void) { memset(&watch_state,0,sizeof(watch_state));ticks=100;ready_at=0;absent=selection_calls=exit_code=stopped=0; }
int main(void) {
 WatchState stale,expired;Target target;
 reset();arm("first");stale=watch_snapshot();ticks+=5000;disarm();
 CHECK(!watch_claim_timeout(stale.generation,&expired));
 CHECK(!watch_snapshot().expired);
 reset();arm("first");stale=watch_snapshot();ticks+=5000;disarm();arm("next");
 CHECK(!watch_claim_timeout(stale.generation,&expired));
 CHECK(!strcmp(watch_snapshot().phase,"next") && !watch_snapshot().expired);
 reset();arm("first");stale=watch_snapshot();disarm();arm("same_tick");ticks+=5000;
 CHECK(!watch_claim_timeout(stale.generation,&expired)); /* same deadline, different generation */
 CHECK(watch_claim_timeout(watch_snapshot().generation,&expired));
 CHECK(!strcmp(expired.phase,"same_tick"));
 CHECK(!watch_claim_timeout(expired.generation,&expired)); /* claim once */
 reset();arm("process_snapshot");ticks+=4999;
 CHECK(!watch_claim_timeout(watch_snapshot().generation,&expired));
 ticks++;CHECK(watch_claim_timeout(watch_snapshot().generation,&expired));
 CHECK(!expired.mutation_started && !expired.pid);
 reset();arm("apply");watch_phase("write_style",123,(HWND)(uintptr_t)456,1);ticks+=5000;
 CHECK(watch_claim_timeout(watch_snapshot().generation,&expired));
 CHECK(expired.mutation_started && expired.pid==123 && (uintptr_t)expired.window==456);
 CHECK(!strcmp(expired.phase,"write_style"));
 if(!setjmp(escape)) watch_phase("late_write",123,NULL,1);
 CHECK(stopped && !strcmp(watch_snapshot().phase,"write_style"));
 stopped=0;if(!setjmp(escape)) disarm();CHECK(stopped);
 stopped=0;if(!setjmp(escape)) arm("late_restart");CHECK(stopped);
 reset();arm("apply");watch_phase("write_style",123,(HWND)(uintptr_t)456,1);disarm();arm("follow");
 CHECK(watch_snapshot().mutation_started); /* conservative session sticky */
 reset();arm("selection");watch_target(123,(HWND)(uintptr_t)456);watch_target(0,NULL);
 CHECK(!watch_snapshot().pid && !watch_snapshot().window);
 watch_target(789,NULL);CHECK(watch_snapshot().pid==789 && !watch_snapshot().window);
 disarm();
 reset();ready_at=30100;
 CHECK(wait_target(&target,1,1,900)==0 && selection_calls==121 && ticks==30100);
 CHECK(!watch_snapshot().deadline && !watch_snapshot().expired && !watch_snapshot().mutation_started);
 reset();absent=1;CHECK(wait_target(&target,1,1,2)==2 && ticks==2100 && selection_calls==9);
 CHECK(!watch_snapshot().deadline);
 reset();absent=1;CHECK(wait_target(&target,0,0,900)==2 && selection_calls==1 && ticks==100);
 reset();writes=bad_write_context=expire_write=0;arm("apply");
 CHECK(write_long((HWND)(uintptr_t)456,GWL_STYLE,123));
 CHECK(writes==1 && !bad_write_context && !strcmp(watch_snapshot().phase,"write_style"));
 Target bound={123,(HWND)(uintptr_t)456};RECT rect={0,0,1920,1080};
 CHECK(position_window(&bound,NULL,&rect));
 CHECK(writes==2 && !bad_write_context && watch_snapshot().pid==123);
 reset();writes=0;expire_write=1;arm("apply");
 CHECK(write_long((HWND)(uintptr_t)456,GWL_STYLE,123));
 if(!setjmp(escape)) write_long((HWND)(uintptr_t)456,GWL_EXSTYLE,0);
 CHECK(stopped && writes==1 && watch_snapshot().mutation_started);
 reset();arm("write_extended_style");watch_phase("write_extended_style",123,(HWND)(uintptr_t)456,1);ticks+=5000;
 if(!setjmp(escape)) watchdog(NULL);
 CHECK(exit_code==4 && watch_snapshot().expired);
 printf("{\"assertions\":%u,\"failed\":0,\"wineStarted\":false}\n",assertions);
 return 0;
}
