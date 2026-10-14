// Executes the retained, byte-exact method include against owned API stubs.
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

struct Metrics {
  int compiled = 0, destroyed = 0, library_calls = 0, function_calls = 0;
  int dumps = 0, updates = 0, errors = 0;
  bool cache_hit = false;
} metrics;
struct Bitcode { std::vector<uint8_t> bytes{1, 2, 3, 4}; };
using sm50_bitcode_t = Bitcode *;
std::unordered_set<sm50_bitcode_t> live;
struct SM50_SHADER_COMMON_DATA {};
struct SM50_COMPILED_BITCODE { const void *Data; size_t Size; };
void SM50GetCompiledBitcode(sm50_bitcode_t code, SM50_COMPILED_BITCODE *out) {
  if (!live.count(code)) throw std::runtime_error("access to dead bitcode");
  *out = {code->bytes.data(), code->bytes.size()};
}
void SM50DestroyBitcode(sm50_bitcode_t code) {
  if (live.erase(code) != 1) throw std::runtime_error("double or null destroy");
  ++metrics.destroyed;
  delete code;
}
namespace cs2lab {
struct CompileTimer {
  const char *kind;
  const char *name;
  bool cache_hit = false;
  ~CompileTimer() { metrics.cache_hit = cache_hit; }
};
}
namespace WMT {
struct Error {};
struct Function {};
struct DispatchData { std::vector<uint8_t> bytes; };
struct Description { const char *getUTF8String() const { return "stub metal error"; } };
template <typename T> struct Reference {
  std::shared_ptr<T> value;
  Reference() = default;
  Reference(std::nullptr_t) {}
  explicit Reference(std::shared_ptr<T> ptr) : value(std::move(ptr)) {}
  Reference &operator=(std::nullptr_t) { value.reset(); return *this; }
  explicit operator bool() const { return bool(value); }
  bool operator==(std::nullptr_t) const { return !value; }
  bool operator!=(std::nullptr_t) const { return bool(value); }
  Description description() const { return {}; }
};
struct Pool {};
Pool MakeAutoreleasePool() { return {}; }
Reference<DispatchData> MakeDispatchData(const void *data, size_t size) {
  auto ptr = std::make_shared<DispatchData>();
  auto bytes = static_cast<const uint8_t *>(data);
  ptr->bytes.assign(bytes, bytes + size);
  return Reference<DispatchData>{ptr};
}
}
#define ERR(...) (++metrics.errors)
enum class Outcome { Success, Error, NullLibrary, NullFunction };
struct Scenario {
  const char *name;
  bool cached, compile_null;
  std::vector<Outcome> libraries;
  int compiled, destroyed, libraries_count, functions, dumps, updates, errors;
  bool cache_hit, function_exists;
};
struct Library {
  bool exists = false, function_exists = false;
  explicit operator bool() const { return exists; }
  WMT::Reference<WMT::Function> newFunction(const char *) {
    if (!exists) throw std::runtime_error("null library method access");
    ++metrics.function_calls;
    return function_exists ? WMT::Reference<WMT::Function>{std::make_shared<WMT::Function>()} : nullptr;
  }
};
struct MetalDevice {
  const Scenario *scenario;
  Library newLibrary(WMT::Reference<WMT::DispatchData> data, WMT::Reference<WMT::Error> &err) {
    if (!data || data.value->bytes != std::vector<uint8_t>({1, 2, 3, 4}))
      throw std::runtime_error("missing or corrupted dispatch data");
    auto index = metrics.library_calls++;
    if (size_t(index) >= scenario->libraries.size()) throw std::runtime_error("unexpected library call");
    auto outcome = scenario->libraries[index];
    err = nullptr;
    if (outcome == Outcome::Error) err = WMT::Reference<WMT::Error>{std::make_shared<WMT::Error>()};
    return {outcome != Outcome::NullLibrary && outcome != Outcome::Error, outcome == Outcome::Success};
  }
};
struct Device {
  MetalDevice metal;
  MetalDevice &GetMTLDevice() { return metal; }
};
struct Shader {
  WMT::Reference<WMT::DispatchData> cached;
  WMT::Reference<WMT::DispatchData> find_cached_variant(int) { return cached; }
  void update_cached_variant(int, WMT::Reference<WMT::DispatchData> data) { ++metrics.updates; cached = data; }
  void dump() { ++metrics.dumps; }
};
struct ThreadpoolWork { virtual ~ThreadpoolWork() = default; };
struct Task : ThreadpoolWork {
  std::string func_name = "fixture_shader";
  Shader *shader_;
  Device *device_;
  int variant_digest_ = 1;
  SM50_SHADER_COMMON_DATA sm50_common;
  std::function<sm50_bitcode_t(const char *, SM50_SHADER_COMMON_DATA *)> proc;
  WMT::Reference<WMT::Function> function_;
#include SELECTED_METHOD
};
int main() {
  const std::vector<Scenario> scenarios = {
    {"fresh_library_error", false, false, {Outcome::Error}, 1, 1, 1, 0, 1, 0, 1, false, false},
    {"fresh_success", false, false, {Outcome::Success}, 1, 1, 1, 1, 0, 1, 0, false, true},
    {"cache_hit", true, false, {Outcome::Success}, 0, 0, 1, 1, 0, 0, 0, true, true},
    {"compile_null", false, true, {}, 1, 0, 0, 0, 0, 0, 0, false, false},
    {"fresh_function_null", false, false, {Outcome::NullFunction}, 1, 1, 1, 1, 1, 1, 1, false, false},
    {"cache_library_error_fallback", true, false, {Outcome::Error, Outcome::Success}, 1, 1, 2, 1, 0, 1, 1, false, true},
    {"cache_null_library_fallback", true, false, {Outcome::NullLibrary, Outcome::Success}, 1, 1, 2, 1, 0, 1, 1, false, true},
    {"cache_function_null_fallback", true, false, {Outcome::NullFunction, Outcome::Success}, 1, 1, 2, 2, 0, 1, 1, false, true},
    {"fallback_library_error", true, false, {Outcome::NullFunction, Outcome::Error}, 1, 1, 2, 1, 1, 0, 2, false, false},
    {"fallback_compile_null", true, true, {Outcome::NullFunction}, 1, 0, 1, 1, 0, 0, 1, false, false},
  };
  int failures = 0;
  for (const auto &s : scenarios) {
    metrics = {};
    Shader shader;
    uint8_t cached_bytes[] = {1, 2, 3, 4};
    if (s.cached) shader.cached = WMT::MakeDispatchData(cached_bytes, 4);
    Device device{{&s}};
    Task task;
    task.shader_ = &shader;
    task.device_ = &device;
    task.proc = [&s](const char *, SM50_SHADER_COMMON_DATA *) {
      ++metrics.compiled;
      if (s.compile_null) return sm50_bitcode_t(nullptr);
      auto code = new Bitcode;
      live.insert(code);
      return code;
    };
    bool passed = false;
    try {
      auto returned = task.RunThreadpoolWork();
      passed = returned == &task && live.empty() && metrics.compiled == s.compiled &&
        metrics.destroyed == s.destroyed && metrics.library_calls == s.libraries_count &&
        metrics.function_calls == s.functions && metrics.dumps == s.dumps &&
        metrics.updates == s.updates && metrics.errors == s.errors &&
        metrics.cache_hit == s.cache_hit && bool(task.function_) == s.function_exists;
    } catch (const std::exception &error) { std::cout << "exception=" << error.what() << '\n'; }
    std::cout << (passed ? "PASS" : "FAIL") << " " << s.name << " live_bitcodes=" << live.size()
              << " compiled=" << metrics.compiled << " destroyed=" << metrics.destroyed
              << " libraries=" << metrics.library_calls << " functions=" << metrics.function_calls
              << " dumps=" << metrics.dumps << " updates=" << metrics.updates
              << " errors=" << metrics.errors << " cache_hit=" << metrics.cache_hit << '\n';
    if (!passed) ++failures;
    // Test harness owns teardown of deliberately leaked original allocations.
    // The live count above remains the test oracle; teardown avoids retaining leaks between cases.
    for (auto ptr : live) delete ptr;
    live.clear();
  }
  std::cout << "scenarios=" << scenarios.size() << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
