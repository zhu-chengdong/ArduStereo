#include "ardustereo/controls.hpp"
#include "ardustereo/diagnostics.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ardustereo;
namespace {
void check(bool ok) { if (!ok) throw std::runtime_error("Control contract failed"); }
class Fake final : public ControlTransport {
 public:
  std::array<int, 3> values{{20, 680, 100}};
  int fail = -1, failed = 0; bool mismatch = false, bad_query = false, restore_fail = false, coupled = false;
  std::vector<unsigned> writes;
  ControlRange query(unsigned i) override { if (bad_query) throw std::runtime_error("query denied"); return i == 1 ? ControlRange{0, 1000, 2} : ControlRange{1, 120, 1}; }
  int read(unsigned i) override { return values.at(i); }
  void write(unsigned i, int v) override {
    writes.push_back(i);
    if (restore_fail && failed && v == 20 && i == 0) throw std::runtime_error("restore denied");
    values.at(i) = v;
    if (coupled && i == 0) values[2] = 99; // A driver may change another control as a side effect.
    if (static_cast<int>(i) == fail && failed++ == 0) throw std::runtime_error("write failed after mutation");
    if (mismatch && i == 2 && v == 101) values[i] = 100;
  }
};
template<class F> std::string rejects(F f) { try { f(); } catch (const std::exception& e) { return e.what(); } throw std::runtime_error("Expected rejection"); }
}
int main() {
  try {
  unsigned cases = 0;
  { Fake f; check(read_controls(f).values == f.values && f.writes.empty()); ++cases; }
  { Fake f; auto r = change_controls(f, {{25, 682, 101}}); check(r.values[0] == 25 && f.writes.size() == 3); ++cases; }
  { Fake f; change_controls(f, f.values); check(f.writes.empty()); ++cases; }
  { Fake f; rejects([&]{change_controls(f, {{25, 681, 101}});}); check(f.writes.empty()); ++cases; }
  { Fake f; f.bad_query = true; rejects([&]{change_controls(f, {{25, 682, 101}});}); check(f.writes.empty()); ++cases; }
  { Fake f; const auto before = f.values; f.fail = 1; rejects([&]{change_controls(f, {{25, 682, 101}});}); check(f.values == before); check(f.writes == std::vector<unsigned>({0,1,0,1,2})); ++cases; }
  { Fake f; const auto before = f.values; f.mismatch = true; rejects([&]{change_controls(f, {{25, 682, 101}});}); check(f.values == before); ++cases; }
  { Fake f; f.fail = 1; f.restore_fail = true; auto error = rejects([&]{change_controls(f, {{25, 682, 101}});}); check(error.find("rollback_failed_index=0") != std::string::npos); ++cases; }
  { Fake f; f.coupled = true; const auto before = f.values; rejects([&]{change_controls(f, {{25, 680, 100}});}); check(f.values == before); ++cases; }
  { std::uint64_t now = 1; DiagnosticConfig c; c.expected_fps = 20; RuntimeDiagnostics d(c, [&]{return now;});
    d.ready(); d.read_started(); d.frame_received(); d.pair_published(true); now += 5000000000ULL;
    d.reset_rate_reference(10); auto s = d.snapshot(); check(s.expected_fps_reference == 10 && s.publication_window_seconds == 0 && s.published_pairs == 1);
    rejects([&]{d.reset_rate_reference(-1);}); ++cases; }
  std::cout << "PASS " << cases << " runtime control/fault contracts (no device)\n";
  return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
