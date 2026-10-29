/* Test-only measured device/compiler receivers. No production cache is added.
 * Counterfactual reuse is owned by this fixture and dies before thread return.
 * Compare equal concurrency; unlocked variant is explicitly experimental. */
struct CostGate { pthread_mutex_t lock; pthread_cond_t condition; unsigned ready,active,peak; bool go; };
@interface CostCompiler : NSObject { @public id real; struct CostGate *gate; }
@end
@implementation CostCompiler
- (id)newRenderPipelineStateWithDescriptor:(id)d compilerTaskOptions:(id)o error:(NSError**)e {
  if(@available(macOS 26.0,*)) {
    pthread_mutex_lock(&gate->lock); ++gate->active; if(gate->active>gate->peak) gate->peak=gate->active; pthread_mutex_unlock(&gate->lock);
    uint64_t start=cost_now(); id p=[real newRenderPipelineStateWithDescriptor:d compilerTaskOptions:o error:e];
    cost_current.pipeline_ns+=cost_now()-start; cost_current.pipeline_calls++;
    pthread_mutex_lock(&gate->lock); --gate->active; pthread_mutex_unlock(&gate->lock); return p;
  }
  return nil;
}
- (void)dealloc { [real release]; [super dealloc]; }
@end
@interface CostDevice : NSObject { @public id<MTLDevice> real; CostCompiler *cached; bool reuse; struct CostGate *gate; }
@end
@implementation CostDevice
- (BOOL)supportsFamily:(MTLGPUFamily)family { return [real supportsFamily:family]; }
- (BOOL)supportsTextureSampleCount:(NSUInteger)n { return [real supportsTextureSampleCount:n]; }
- (id)newCompilerWithDescriptor:(id)d error:(NSError**)e {
  if(cached) return [cached retain];
  if(@available(macOS 26.0,*)) {
    uint64_t start=cost_now(); id<MTL4Compiler> c=[real newCompilerWithDescriptor:d error:e];
    cost_current.setup_ns+=cost_now()-start; cost_current.setup_calls++;
    if(!c || *e) { [c release]; return nil; }
    CostCompiler *wrapper=[CostCompiler new]; wrapper->real=c; wrapper->gate=gate;
    if(reuse) cached=[wrapper retain]; return wrapper;
  }
  return nil;
}
- (void)dealloc { [cached release]; [real release]; [super dealloc]; }
@end
struct CostWork { id<MTLDevice> device; id<MTLFunction> vertex,fragment; bool reuse,unlocked;
  struct CostRecord samples[12]; uint64_t total_ns; struct CostGate *gate; };
static void *cost_worker(void *opaque) { @autoreleasepool {
  struct CostWork *work=opaque; CostDevice *proxy=[CostDevice new]; proxy->real=[work->device retain]; proxy->reuse=work->reuse; proxy->gate=work->gate;
  pthread_mutex_lock(&work->gate->lock);++work->gate->ready;pthread_cond_broadcast(&work->gate->condition);
  while(!work->gate->go) pthread_cond_wait(&work->gate->condition,&work->gate->lock);
  pthread_mutex_unlock(&work->gate->lock);
  cost_unlocked=work->unlocked; uint64_t start=cost_now();
  for(unsigned n=0;n<12;n++) {
    @autoreleasepool {
      cost_current=(struct CostRecord){0}; uint64_t tick=cost_now();
      struct WMTRenderPipelineInfo info=render_info(work->vertex,work->fragment,1,false,false);
      info.colors[1]=info.colors[0]; id<MTLRenderPipelineState> p=native_render((id<MTLDevice>)proxy,&info);
      [p release]; cost_current.total_ns=cost_now()-tick; work->samples[n]=cost_current;
    }
  }
  work->total_ns=cost_now()-start; cost_unlocked=false; [proxy release]; return NULL;
} }
static int cost_all(const char *directory) {
  id<MTLDevice> dev=MTLCreateSystemDefaultDevice(); if(!dev || !wmt_metal4_capable(dev)) { [dev release]; return 77; }
  id<MTLLibrary> vl=translated_library(dev,directory,"mrt-vs"),pl=translated_library(dev,directory,"mrt-ps");
  id<MTLFunction> v=ordinary(vl,"shader_main"),f=ordinary(pl,"shader_main");
  /* Warm the exact pipeline before all timed modes. Driver caches are never
   * cleared. Rotate/reverse modes to expose order effects, not a cold claim. */
  struct WMTRenderPipelineInfo warm=render_info(v,f,1,false,false);warm.colors[1]=warm.colors[0];
  id<MTLRenderPipelineState> warmed=native_render(dev,&warm);[warmed release];
  const unsigned orders[3][5]={{0,1,2,3,4},{4,3,2,1,0},{2,0,4,1,3}};
  for(unsigned round=0;round<3;round++) for(unsigned order=0;order<5;order++) {
    unsigned mode=orders[round][order];
    unsigned count=mode<2 ? 1:2; bool reuse=mode==1 || mode>=3, unlocked=mode==4;
    struct CostGate gate={0};require(!pthread_mutex_init(&gate.lock,NULL),"cost gate mutex init");
    require(!pthread_cond_init(&gate.condition,NULL),"cost gate condition init");
    struct CostWork work[2]={0}; pthread_t threads[2];
    for(unsigned t=0;t<count;t++) { work[t].device=dev;work[t].vertex=v;work[t].fragment=f;work[t].reuse=reuse;work[t].unlocked=unlocked;work[t].gate=&gate;
      require(!pthread_create(&threads[t],NULL,cost_worker,&work[t]),"cost worker create"); }
    pthread_mutex_lock(&gate.lock);while(gate.ready<count) pthread_cond_wait(&gate.condition,&gate.lock);
    uint64_t begin=cost_now();gate.go=true;pthread_cond_broadcast(&gate.condition);pthread_mutex_unlock(&gate.lock);
    for(unsigned t=0;t<count;t++) require(!pthread_join(threads[t],NULL),"cost worker join");
    uint64_t wall=cost_now()-begin;
    require(gate.active==0 && gate.peak>=1 && gate.peak<=count,"cost active compiler closure");
    if(!unlocked) require(gate.peak==1,"production compile serialization observed");
    for(unsigned t=0;t<count;t++) for(unsigned n=0;n<12;n++) {
      struct CostRecord s=work[t].samples[n];
      printf("COST {\"round\":%u,\"order\":%u,\"threads\":%u,\"reuse\":%u,\"unlocked\":%u,\"peak_active_compiles\":%u,\"thread\":%u,\"sample\":%u,\"first_compiler_use\":%u,\"setup_ns\":%llu,\"setup_calls\":%u,\"pipeline_ns\":%llu,\"pipeline_calls\":%u,\"lock_wait_ns\":%llu,\"total_ns\":%llu,\"burst_wall_ns\":%llu}\n",
          round,order,count,reuse,unlocked,gate.peak,t,n,n==0,(unsigned long long)s.setup_ns,s.setup_calls,(unsigned long long)s.pipeline_ns,s.pipeline_calls,
          (unsigned long long)s.wait_ns,(unsigned long long)s.total_ns,(unsigned long long)wall);
      require(s.pipeline_calls==1,"cost production compiler receiver connected");
      require(s.setup_calls==(reuse && n ? 0u:1u),"cost setup reuse counterfactual connected");
    }
    require(!pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.lock),"cost gate destruction");
  }
  [v release];[f release];[vl release];[pl release];[dev release];
  puts("PASS cost exact production helper with measured receiver; controlled cache is test-only; no cold/game claim");return 0;
}
