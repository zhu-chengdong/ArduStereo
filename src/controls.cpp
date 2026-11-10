#include "ardustereo/controls.hpp"
#include <stdexcept>
#include <vector>

namespace ardustereo {
namespace {
void valid(const ControlRange& r, int value) {
  if (r.minimum > r.maximum || r.step <= 0 || value < r.minimum || value > r.maximum ||
      (static_cast<long long>(value) - r.minimum) % r.step)
    throw std::invalid_argument("Raw control outside driver range/step");
}
}
ControlState read_controls(ControlTransport& transport) {
  ControlState out;
  for (unsigned i = 0; i < 3; ++i) {
    out.ranges[i] = transport.query(i);
    out.values[i] = transport.read(i);
    valid(out.ranges[i], out.values[i]);
  }
  return out;
}
ControlState change_controls(ControlTransport& t, const std::array<int, 3>& values) {
  const auto before = read_controls(t);
  for (unsigned i = 0; i < 3; ++i) valid(before.ranges[i], values[i]);
  std::vector<unsigned> attempted;
  try {
    for (unsigned i = 0; i < 3; ++i) {
      if (values[i] == before.values[i]) continue;
      attempted.push_back(i); // A failing ioctl may already have modified the device.
      t.write(i, values[i]);
    }
    auto after = read_controls(t);
    if (after.values != values) throw std::runtime_error("Control readback differs from request");
    return after;
  } catch (const std::exception& error) {
    std::string failures;
    // Restore the whole original triplet: changing frame rate can also alter a
    // control we did not explicitly write. Restore frame rate first, then the
    // exposure/gain under that original mode, and verify all three afterwards.
    if (!attempted.empty()) {
      for (unsigned i = 0; i < 3; ++i) {
        try { t.write(i, before.values[i]); }
        catch (...) { failures += " rollback_failed_index=" + std::to_string(i); }
      }
      for (unsigned i = 0; i < 3; ++i) {
        try { if (t.read(i) != before.values[i]) throw std::runtime_error("restore mismatch"); }
        catch (...) { failures += " rollback_verify_failed_index=" + std::to_string(i); }
      }
    }
    throw std::runtime_error(std::string(error.what()) + failures);
  }
}
} // namespace ardustereo
