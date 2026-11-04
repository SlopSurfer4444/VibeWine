// Test-only pass-through timing seam. Production ApiScope is checked separately
// by test_api.py; this seam preserves return/exception behavior for WSI tests.
namespace cs2trace {
struct ApiScope {
  template<class... T> explicit ApiScope(T...) {}
  template<class F> auto invoke(F &&fn) -> decltype(fn()) { return fn(); }
};
}
