// Appended to the precise-bindings production extraction and CPU adapters.
namespace dxmt {
template <PipelineStage Stage, class Context>
void set_range(Context &ctx, ID3D11Buffer *buffer, const UINT *first, const UINT *count, bool plain = false) {
#define SET(stage, prefix) if constexpr (Stage == PipelineStage::stage) { \
  if (plain) ctx.prefix##SetConstantBuffers(0, 1, &buffer); \
  else ctx.prefix##SetConstantBuffers1(0, 1, &buffer, first, count); }
  SET(Vertex, VS) SET(Pixel, PS) SET(Hull, HS) SET(Domain, DS) SET(Geometry, GS) SET(Compute, CS)
#undef SET
}
template <PipelineStage Stage, class Context>
void get_range(Context &ctx, ID3D11Buffer *expected, UINT first, UINT count) {
  ID3D11Buffer *buffer = nullptr;
  UINT observed_first = 999, observed_count = 999;
#define GET(stage, prefix) if constexpr (Stage == PipelineStage::stage) { \
  ctx.prefix##GetConstantBuffers1(0, 1, &buffer, &observed_first, &observed_count); }
  GET(Vertex, VS) GET(Pixel, PS) GET(Hull, HS) GET(Domain, DS) GET(Geometry, GS) GET(Compute, CS)
#undef GET
  require(buffer == expected, "getter resource identity");
  if (buffer) {
    buffer->decRef();
    require(observed_first == first && observed_count == count, "same-buffer whole-range reset metadata");
  }
  buffer = nullptr;
#define GET(stage, prefix) if constexpr (Stage == PipelineStage::stage) { ctx.prefix##GetConstantBuffers(0, 1, &buffer); }
  GET(Vertex, VS) GET(Pixel, PS) GET(Hull, HS) GET(Domain, DS) GET(Geometry, GS) GET(Compute, CS)
#undef GET
  require(buffer == expected, "plain getter identity");
  if (buffer) buffer->decRef();
}
template <class Context> void fill_range(Context &ctx, const Rc<D3D11ResourceCommon> &resource, uint64_t token) {
  D3D11_MAPPED_SUBRESOURCE mapped{};
  require(ctx.map(resource.ptr(), D3D11_MAP_WRITE_DISCARD, &mapped) == S_OK, "range fixture map");
  require(mapped.RowPitch == 512 && mapped.DepthPitch == 512, "range fixture extent");
  for (UINT i = 0; i < 32; ++i) {
    uint64_t value = token + i;
    std::memcpy(static_cast<uint8_t *>(mapped.pData) + i * 16, &value, sizeof(value));
  }
}
template <PipelineStage Stage, class Context>
void observe(Context &ctx, ID3D11Buffer *expected, UINT first, UINT count, uint64_t value, UINT tables) {
  get_range<Stage>(ctx, expected, first, count);
  ctx.template draw<Stage>({value}, tables);
  ctx.EmitST([first, bound = expected != nullptr](ArgumentEncodingContext &enc) {
    auto &entry = enc.cbuf_[14 * unsigned(Stage)];
    require(bool(entry.buffer) == bound, "encoded CB bound identity");
    require(entry.offset == first * 16, "encoded CB offset reset");
  });
  ctx.replay();
}
template <PipelineStage Stage, class Context>
void reset_scenario(bool plain, bool restore) {
  ++scenarios;
  Context ctx;
  Rc<D3D11ResourceCommon> a = new D3D11ResourceCommon, b = new D3D11ResourceCommon;
  fill_range(ctx, a, 100);
  fill_range(ctx, b, 200);
  for (auto &s : ctx.state_.ShaderStages) s.ConstantBuffers.clear_dirty();
  // Production managed-shader lifetime is outside the adapter. Keep the shader
  // alive across D3D state resets while old deferred commands still reference it.
  Rc<Shader> shader = new Shader({0});
  ctx.state_.ShaderStages[Stage].Shader = shader;
  auto &bindings = ctx.state_.ShaderStages[Stage].ConstantBuffers;
  UINT first = 16, count = 16;
  set_range<Stage>(ctx, a.ptr(), &first, &count);
  observe<Stage>(ctx, a.ptr(), 16, 16, 116, 1);
  auto before = ctx.recorded.size();
  set_range<Stage>(ctx, a.ptr(), nullptr, nullptr, plain);
  // Readback first gives the immutable baseline a specific behavioral failure.
  get_range<Stage>(ctx, a.ptr(), 0, 32);
  require(bindings.test_dirty(0) && ctx.recorded.size() == before + 1, "reset dirties slot and emits offset once");
  observe<Stage>(ctx, a.ptr(), 0, 32, 100, 2);
  ctx.replay(); // Deferred captures must retain their resources on repeated replay.
  before = ctx.recorded.size();
  bindings.set_dirty(9);
  set_range<Stage>(ctx, a.ptr(), nullptr, nullptr, plain);
  require(!bindings.test_dirty(0) && bindings.test_dirty(9) && ctx.recorded.size() == before, "whole-range exact repeat preserves dirty state");
  observe<Stage>(ctx, a.ptr(), 0, 32, 100, 2);
  first = 0;
  before = ctx.recorded.size();
  set_range<Stage>(ctx, a.ptr(), &first, &count);
  require(bindings.test_dirty(0) && ctx.recorded.size() == before, "count-only change dirties without offset emission");
  observe<Stage>(ctx, a.ptr(), 0, 16, 100, 3);
  before = ctx.recorded.size();
  set_range<Stage>(ctx, a.ptr(), &first, &count);
  require(!bindings.test_dirty(0) && ctx.recorded.size() == before, "explicit-range exact repeat");
  set_range<Stage>(ctx, a.ptr(), nullptr, nullptr);
  require(bindings.test_dirty(0) && ctx.recorded.size() == before, "count-only whole-range reset");
  observe<Stage>(ctx, a.ptr(), 0, 32, 100, 4);
  before = ctx.recorded.size();
  set_range<Stage>(ctx, b.ptr(), nullptr, nullptr, plain);
  require(bindings.test_dirty(0) && ctx.recorded.size() == before + 1, "resource replacement full bind");
  observe<Stage>(ctx, b.ptr(), 0, 32, 200, 5);
  set_range<Stage>(ctx, nullptr, nullptr, nullptr, plain);
  require(!bindings.test_bound(0) && bindings.test_dirty(0), "null unbind dirty");
  observe<Stage>(ctx, nullptr, 0, 0, 0, 6);
  before = ctx.recorded.size();
  set_range<Stage>(ctx, nullptr, nullptr, nullptr, plain);
  require(!bindings.test_dirty(0) && ctx.recorded.size() == before, "null exact repeat");
  first = 16;
  set_range<Stage>(ctx, a.ptr(), &first, &count);
  observe<Stage>(ctx, a.ptr(), 16, 16, 116, 7);
  set_range<Stage>(ctx, a.ptr(), nullptr, nullptr, plain);
  observe<Stage>(ctx, a.ptr(), 0, 32, 100, 8);
  // Exercise both actual branch snippets and the actual CB restore loop. The
  // rest of ExecuteCommandList/FinishCommandList is outside this CPU boundary.
  for (bool finish : {false, true}) {
    ctx.EmitST([](ArgumentEncodingContext &enc) { for (auto &cb : enc.cbuf_) cb = {}; });
    if (finish) ctx.finish_boundary(restore); else ctx.execute_boundary(restore);
    require(bindings.test_bound(0) == restore, "restore flag D3D binding state");
    get_range<Stage>(ctx, restore ? a.ptr() : nullptr, 0, 32);
    ctx.EmitST([restore](ArgumentEncodingContext &enc) {
      auto &entry = enc.cbuf_[14 * unsigned(Stage)];
      require(bool(entry.buffer) == restore && entry.offset == 0, "restore flag encoded CB state");
    });
    ctx.replay();
    ctx.replay();
    if (!restore) {
      ctx.state_.ShaderStages[Stage].Shader = shader;
      set_range<Stage>(ctx, a.ptr(), nullptr, nullptr);
      ctx.replay();
    }
  }
}
template <class Context> void reset_all_stages() {
  for (bool plain : {false, true}) for (bool restore : {false, true}) {
    reset_scenario<PipelineStage::Vertex, Context>(plain, restore);
    reset_scenario<PipelineStage::Pixel, Context>(plain, restore);
    reset_scenario<PipelineStage::Hull, Context>(plain, restore);
    reset_scenario<PipelineStage::Domain, Context>(plain, restore);
    reset_scenario<PipelineStage::Geometry, Context>(plain, restore);
    reset_scenario<PipelineStage::Compute, Context>(plain, restore);
  }
}
void count_only_control() {
  ++scenarios;
  Immediate ctx;
  Rc<D3D11ResourceCommon> a = new D3D11ResourceCommon;
  fill_range(ctx, a, 300);
  ctx.state_.ShaderStages[PipelineStage::Vertex].Shader = new Shader({0});
  set_range<PipelineStage::Vertex>(ctx, a.ptr(), nullptr, nullptr);
  observe<PipelineStage::Vertex>(ctx, a.ptr(), 0, 32, 300, 1);
  UINT first = 0, count = 16;
  auto before = ctx.recorded.size();
  set_range<PipelineStage::Vertex>(ctx, a.ptr(), &first, &count);
  auto &bindings = ctx.state_.ShaderStages[PipelineStage::Vertex].ConstantBuffers;
  require(bindings.at(0).NumConstants == 16, "count-only stored count");
  require(bindings.test_dirty(0), "count-only update dirty bit");
  require(ctx.recorded.size() == before, "count-only no offset command");
  observe<PipelineStage::Vertex>(ctx, a.ptr(), 0, 16, 300, 2);
}
} // namespace dxmt
int main(int argc, char **) {
  try {
    if (argc > 1) dxmt::count_only_control();
    else {
      dxmt::reset_all_stages<dxmt::Immediate>();
      dxmt::reset_all_stages<dxmt::Deferred>();
      dxmt::count_only_control();
    }
    std::cout << "PASS " << scenarios << " CB range scenarios, " << checks << " assertions\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CONTRACT: " << error.what() << '\n';
    return 1;
  }
}
