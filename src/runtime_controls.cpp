#include "ardustereo/controls.hpp"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#ifdef __linux__
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace ardustereo {
namespace {
#ifdef __linux__
class LinuxControls final : public ControlTransport {
 public:
  explicit LinuxControls(const std::string& device) {
    fd_ = ::open(device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) throw std::runtime_error("Cannot open control interface: " + std::string(std::strerror(errno)));
  }
  ~LinuxControls() override { ::close(fd_); }
  ControlRange query(unsigned i) override {
    v4l2_queryctrl q{}; q.id = ids_.at(i); call(VIDIOC_QUERYCTRL, &q);
    const std::string name(reinterpret_cast<const char*>(q.name), strnlen(reinterpret_cast<const char*>(q.name), sizeof(q.name)));
    if (name != names_.at(i) || q.type != V4L2_CTRL_TYPE_INTEGER ||
        (q.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE | V4L2_CTRL_FLAG_GRABBED)))
      throw std::runtime_error("Driver control unavailable/unwritable: " + names_.at(i));
    return {q.minimum, q.maximum, q.step};
  }
  int read(unsigned i) override {
    v4l2_control v{}; v.id = ids_.at(i); call(VIDIOC_G_CTRL, &v); return v.value;
  }
  void write(unsigned i, int value) override {
    v4l2_control v{}; v.id = ids_.at(i); v.value = value; call(VIDIOC_S_CTRL, &v);
  }
 private:
  void call(unsigned long request, void* value) {
    int result;
    do { result = ::ioctl(fd_, request, value); } while (result < 0 && errno == EINTR);
    if (result < 0) throw std::runtime_error("V4L2 control ioctl: " + std::string(std::strerror(errno)));
  }
  int fd_ = -1;
  const std::array<unsigned, 3> ids_{{0x00981906U, V4L2_CID_EXPOSURE, V4L2_CID_ANALOGUE_GAIN}};
  const std::array<std::string, 3> names_{{"frame_rate", "Exposure", "Analogue Gain"}};
};
#endif
class SimulatedControls final : public ControlTransport {
 public:
  ControlRange query(unsigned i) override { return ranges_.at(i); }
  int read(unsigned i) override { return values_.at(i); }
  void write(unsigned i, int v) override { values_.at(i) = v; }
 private:
  std::array<int, 3> values_{{20, 681, 100}};
  // Artificial software-test limits; never presented as queried device limits.
  std::array<ControlRange, 3> ranges_{{{1, 120, 1}, {0, 10000, 1}, {0, 255, 1}}};
};
}
std::unique_ptr<ControlTransport> make_linux_controls(const std::string& device) {
#ifdef __linux__
  return std::unique_ptr<ControlTransport>(new LinuxControls(device));
#else
  (void)device; throw std::runtime_error("Runtime camera controls require Linux V4L2");
#endif
}
std::unique_ptr<ControlTransport> make_simulated_controls() {
  return std::unique_ptr<ControlTransport>(new SimulatedControls);
}
} // namespace ardustereo
