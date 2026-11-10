#pragma once
#include <array>
#include <memory>
#include <string>

namespace ardustereo {
struct ControlRange { int minimum = 0, maximum = 0, step = 1; };
struct ControlState {
  std::array<int, 3> values{{20, 681, 100}}; // frame_rate, exposure, analogue_gain; raw integers.
  std::array<ControlRange, 3> ranges;
};
class ControlTransport {
 public:
  virtual ~ControlTransport() = default;
  virtual ControlRange query(unsigned index) = 0;
  virtual int read(unsigned index) = 0;
  virtual void write(unsigned index, int value) = 0;
};
ControlState read_controls(ControlTransport& transport);
// Validate all values before any write. Restore the original triplet on failure and
// report failed restoration explicitly; V4L2 does not provide a transaction here.
ControlState change_controls(ControlTransport& transport, const std::array<int, 3>& values);
std::unique_ptr<ControlTransport> make_linux_controls(const std::string& device);
std::unique_ptr<ControlTransport> make_simulated_controls();
} // namespace ardustereo
