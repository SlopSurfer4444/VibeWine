// Exact production compute admission and constructor, with the real scheduler.
#define main scheduler_regressions_main
#include "../demand-scheduler/fixture.cpp"
#undef main
#define STDMETHODCALLTYPE
#define D3D11_ASSERT assert
using HRESULT = int;
using UINT = unsigned;
constexpr HRESULT E_FAIL = -1, S_OK = 0;
struct ID3D11ComputeShader {};
namespace dxmt {
using ShaderVariantDefault = unsigned;
struct MTLD3D11Device {};
class ComputeShader : public CachedLookupFixture {
  MTL_SHADER_REFLECTION reflection_{};
public:
  using CachedLookupFixture::CachedLookupFixture;
  MTL_SHADER_REFLECTION &reflection() { return reflection_; }
};
using ManagedShader = ComputeShader *;
struct MTL_COMPUTE_PIPELINE_DESC { ManagedShader ComputeShader; };
inline bool early_compile = true;
namespace cs2lab { bool earlyCompile() { return early_compile; } }
inline unsigned interface_count = 0;
template <typename T> struct TShaderBase : T {
  TShaderBase(MTLD3D11Device *, ManagedShader) { ++interface_count; }
};
template <typename T> T *ref(T *value) { return value; }

class MTLCompiledComputePipelineImpl : public Task {
  MTLD3D11Device *device_;
  CompiledShader *ComputeShader;
  bool tgsize_is_multiple_of_sgwidth;
public:
#include "compute_constructor.inc"
  void initialize_run() {
    run = [this]() -> Task * { return ComputeShader->done ? this : ComputeShader; };
  }
  Task *dependency() { return ComputeShader; }
};
using MTLCompiledComputePipeline = MTLCompiledComputePipelineImpl;
inline std::function<void()> pipeline_factory_hook;
#include "compute_factory_decl.inc"
#include "compute_factory.inc"
// Factory only installs the fixture's dependency/terminal seam. Construction
// above and the cache's complete public/private admission methods are verbatim.
#ifndef FIXTURE_BASELINE_FACTORY
std::unique_ptr<MTLCompiledComputePipeline> fixture_compute_pipeline(
    MTLD3D11Device *device, ManagedShader shader, CompiledShader *compiled) {
  auto result = CreateComputePipeline(device, shader, compiled);
  result->initialize_run();
  if (pipeline_factory_hook) pipeline_factory_hook();
  return result;
}
#endif
// The baseline constructor still resolves its shader internally; retaining this
// overload lets the unchanged pre-fix source execute the same acceptance test.
std::unique_ptr<MTLCompiledComputePipeline> fixture_compute_pipeline(
    MTLD3D11Device *device, ManagedShader shader) {
#ifdef FIXTURE_BASELINE_FACTORY
  auto result = CreateComputePipeline(device, shader);
#else
  auto result = CreateComputePipeline(device, shader, shader->get_shader(ShaderVariantDefault{}));
#endif
  result->initialize_run();
  if (pipeline_factory_hook) pipeline_factory_hook();
  return result;
}
struct CacheInterface {
  virtual HRESULT AddComputeShader(const void *, UINT, ID3D11ComputeShader **) = 0;
  virtual void GetComputePipeline(MTL_COMPUTE_PIPELINE_DESC *, MTLCompiledComputePipeline **) = 0;
};
class ComputeCache : public CacheInterface {
  using CachedSM50Shader = ComputeShader;
  MTLD3D11Device *device = nullptr;
  std::unordered_map<ManagedShader, std::unique_ptr<MTLCompiledComputePipeline>> pipelines_cs_;
  dxmt::mutex mutex_cs_;
  Scheduler &scheduler_;
  ComputeShader *CreateShader(const void *identity, UINT) {
    return const_cast<ComputeShader *>(static_cast<const ComputeShader *>(identity));
  }
public:
  explicit ComputeCache(Scheduler &scheduler) : scheduler_(scheduler) {}
#define CreateComputePipeline fixture_compute_pipeline
#include "compute_add.inc"
#include "compute_lookup.inc"
#undef CreateComputePipeline
};
}

void compute_routing(bool backlog, bool demand, bool early, bool promote) {
  Saturated pool(backlog, demand);
  dxmt::LookupCache shader_owner{*pool.scheduler};
  dxmt::ComputeShader prewarm{&shader_owner}, consumer{&shader_owner};
  dxmt::ComputeCache cache{*pool.scheduler};
  std::array<unsigned, 2> order{};
  std::atomic<unsigned> entered{0}, released{0};
  dxmt::lookup_run = [&](Task *task) {
    unsigned n = entered.load();
    order[n] = task == prewarm.get_shader(0u, Urgency::speculative) ? 0u : 1u;
    entered = n + 1;
    until([&] { return released > n; });
    return task;
  };
  dxmt::early_compile = early;
  ID3D11ComputeShader *handle = nullptr;
  assert(cache.AddComputeShader(&prewarm, 0, &handle) == S_OK && handle);
  delete handle;
  dxmt::MTL_COMPUTE_PIPELINE_DESC a{&prewarm}, b{&consumer};
  dxmt::MTLCompiledComputePipeline *a_pipeline = nullptr, *b_pipeline = nullptr;
  cache.GetComputePipeline(&b, &b_pipeline);
  if (promote || !early) cache.GetComputePipeline(&a, &a_pipeline);
  // Repeated creation of one bytecode owner must not convert an existing
  // speculative pipeline to demand or submit another shader/PSO node.
  if (early && !promote) {
    assert(cache.AddComputeShader(&prewarm, 0, &handle) == S_OK);
    delete handle;
  }
  pool.release[0] = true;
  const unsigned expected_first = early && !demand ? 0 : 1;
  until([&] { return entered == 1; });
  assert(order[0] == expected_first && "compute prewarm must remain below actual shader demand");
  released = 1;
  until([&] { return entered == 2; });
  assert(order[1] == 1 - expected_first);
  released = 2;
  auto *a_shader = prewarm.get_shader(0u, Urgency::speculative);
  auto *b_shader = consumer.get_shader(0u, Urgency::speculative);
  until([&] { return a_shader->done && b_shader->done && b_pipeline->done; });
  // Retrieving a speculative PSO after its shader finished must promote/return
  // the same identity. The queued and dependency-blocked cases are separate.
  dxmt::MTLCompiledComputePipeline *same = nullptr;
  cache.GetComputePipeline(&a, &same);
  if (a_pipeline) assert(same == a_pipeline);
  until([&] { return same->done.load(); });
  for (unsigned n = 0; n < 64; ++n) {
    cache.GetComputePipeline(&a, &a_pipeline);
    assert(a_pipeline == same);
  }
  assert(a_shader->runs == 1 && b_shader->runs == 1);
  assert(a_shader->trace_initializations == 1 && b_shader->trace_initializations == 1);
  assert(same->trace_initializations == 1 && b_pipeline->trace_initializations == 1);
  pool.finish();
  dxmt::lookup_run = {};
}


// A factory hook admits unrelated work between this shader and its PSO. It is
// outside the exact production method and only fixes a reproducible queue order.
void compute_queued_promotion(bool backlog, bool demand, bool fail_shader) {
  Saturated pool(backlog, demand);
  dxmt::LookupCache shader_owner{*pool.scheduler};
  dxmt::ComputeShader prewarm{&shader_owner};
  dxmt::ComputeCache cache{*pool.scheduler};
  Task filler;
  std::atomic<bool> shader_entered{false}, release_shader{false}, release_filler{false};
  std::atomic<int> first{-1};
  filler.run = [&] {
    int empty = -1; first.compare_exchange_strong(empty, 1);
    until([&] { return release_filler.load(); }); return &filler;
  };
  dxmt::lookup_run = [&](Task *task) {
    shader_entered = true;
    until([&] { return release_shader.load(); });
    task->compile_failed = fail_shader;
    return task;
  };
  dxmt::pipeline_factory_hook = [&] { pool.scheduler->submit(&filler, Urgency::speculative); };
  dxmt::early_compile = true;
  ID3D11ComputeShader *handle = nullptr;
  cache.AddComputeShader(&prewarm, 0, &handle);
  delete handle;
  dxmt::pipeline_factory_hook = {};
  pool.release[0] = true;
  until([&] { return shader_entered.load(); });
  dxmt::MTL_COMPUTE_PIPELINE_DESC desc{&prewarm};
  dxmt::MTLCompiledComputePipeline *pipeline = nullptr;
  cache.GetComputePipeline(&desc, &pipeline);
  pipeline->on_ready = [&] { int empty = -1; first.compare_exchange_strong(empty, 0); };
  // Early-compile repetition must never downgrade the consumer's promotion.
  cache.AddComputeShader(&prewarm, 0, &handle);
  delete handle;
  release_shader = true;
  until([&] { return first >= 0; });
  assert(first == (demand ? 0 : 1) && "actual lookup must promote its existing queued compute PSO");
  release_filler = true;
  until([&] { return pipeline->done && filler.done; });
  auto *shader = prewarm.get_shader(0u, Urgency::speculative);
  assert(shader->compile_failed == fail_shader && shader->runs == 1 && pipeline->runs == 1);
  assert(shader->trace_initializations == 1 && pipeline->trace_initializations == 1);
  pool.finish();
  dxmt::lookup_run = {};
}

void compute_dependency_promotion(bool backlog, bool demand) {
  Saturated pool(backlog, demand);
  dxmt::LookupCache shader_owner{*pool.scheduler};
  dxmt::ComputeShader prewarm{&shader_owner};
  dxmt::ComputeCache cache{*pool.scheduler};
  Task filler;
  std::atomic<int> first{-1};
  std::atomic<bool> release{false};
  filler.run = [&] {
    int empty = -1; first.compare_exchange_strong(empty, 1);
    until([&] { return release.load(); }); return &filler;
  };
  dxmt::lookup_run = [&](Task *task) {
    int empty = -1; first.compare_exchange_strong(empty, 0);
    until([&] { return release.load(); }); return task;
  };
  pool.scheduler->submit(&filler, Urgency::speculative);
  dxmt::early_compile = true;
  ID3D11ComputeShader *handle = nullptr;
  cache.AddComputeShader(&prewarm, 0, &handle);
  delete handle;
  dxmt::MTL_COMPUTE_PIPELINE_DESC desc{&prewarm};
  dxmt::MTLCompiledComputePipeline *pipeline = nullptr, *same = nullptr;
  cache.GetComputePipeline(&desc, &pipeline);
  for (unsigned n = 0; n < 64; ++n) {
    cache.GetComputePipeline(&desc, &same);
    assert(pipeline == same);
  }
  pool.release[0] = true;
  until([&] { return first >= 0; });
  assert(first == (demand ? 0 : 1) && "promoted compute PSO must promote its shader dependency");
  release = true;
  until([&] { return pipeline->done && filler.done; });
  auto *shader = prewarm.get_shader(0u, Urgency::speculative);
  assert(shader->runs == 1 && pipeline->runs == (demand ? 2u : 1u));
  assert(shader->trace_initializations == 1 && pipeline->trace_initializations == 1);
  pool.finish();
  dxmt::lookup_run = {};
}

void variant_factory_retry() {
  Scheduler scheduler(false, true, 1);
  dxmt::LookupCache owner{scheduler};
  dxmt::CachedLookupFixture shader{&owner};
  dxmt::lookup_run = [](Task *task) { return task; };
  dxmt::lookup_factory_fail_once = true;
  bool failed = false;
  try { shader.get_shader(9u, Urgency::speculative); } catch (const std::bad_alloc &) { failed = true; }
  assert(failed);
  auto *result = shader.get_shader(9u);
  assert(result && "a failed variant factory must leave its key retryable");
  until([&] { return result->done.load(); });
  assert(result == shader.get_shader(9u) && result->runs == 1 && result->trace_initializations == 1);
}

int main(int argc, char **argv) {
  const std::string selected = argc > 1 ? argv[1] : "all";
  if (selected == "all" || selected == "scheduler") scheduler_regressions_main();
  if (selected == "all" || selected == "compute") {
    for (bool backlog : {false, true})
      for (bool demand : {false, true}) {
        compute_routing(backlog, demand, false, false);
        compute_routing(backlog, demand, true, false);
        compute_routing(backlog, demand, true, true);
      }
    std::cout << "PASS production compute prewarm: early off/on, scheduler off/on, backlog off/on, dependency identity, repeated prewarm, consumer lookup\n";
  }
  if (selected == "all" || selected == "promotion") {
    for (bool backlog : {false, true})
      for (bool demand : {false, true}) {
        compute_queued_promotion(backlog, demand, false);
        compute_queued_promotion(backlog, demand, true);
        compute_dependency_promotion(backlog, demand);
      }
    std::cout << "PASS production compute cache-hit promotion: queued PSO order, blocked shader inheritance, failed shader readiness, identity and admission counts\n";
  }
  if (selected == "all" || selected == "retry") {
    variant_factory_retry();
    std::cout << "PASS production variant factory exception: same-key retry and one task admission\n";
  }
}
