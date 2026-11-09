#include "v4l2_controls.hpp"

#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef __linux__
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace ardustereo {
std::string apply_startup_controls(const CaptureConfig& config) {
  if (!has_startup_controls(config)) return "controls=not_requested";
#ifndef __linux__
  throw std::runtime_error("Camera controls require Linux V4L2");
#else
  struct Fd {
    int value;
    ~Fd() { if (value >= 0) ::close(value); }
  } fd{::open(config.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC)};
  if (fd.value < 0) throw std::runtime_error("Cannot open camera control interface: " + std::string(std::strerror(errno)));
  struct Control { const char* name; const char* raw_name; unsigned id; int requested; int before; };
  // frame_rate is the vendor CID observed on this B0266 driver, not CAP_PROP_FPS.
  std::vector<Control> controls{
      {"frame_rate", "frame_rate", 0x00981906U, config.frame_rate, 0},
      {"exposure", "Exposure", V4L2_CID_EXPOSURE, config.exposure, 0},
      {"analogue_gain", "Analogue Gain", V4L2_CID_ANALOGUE_GAIN, config.analogue_gain, 0}};
  const auto call = [&](unsigned long request, void* value, const std::string& context) {
    int result;
    do { result = ::ioctl(fd.value, request, value); } while (result < 0 && errno == EINTR);
    if (result < 0) throw std::runtime_error(context + ": " + std::strerror(errno));
  };
  // Check every requested control before writing any of them.
  for (auto& c : controls) {
    if (c.requested == -1) continue;
    v4l2_queryctrl q{};
    q.id = c.id;
    call(VIDIOC_QUERYCTRL, &q, std::string("Query control ") + c.name);
    const std::string actual(reinterpret_cast<const char*>(q.name),
                             strnlen(reinterpret_cast<const char*>(q.name), sizeof(q.name)));
    if (actual != c.raw_name || q.type != V4L2_CTRL_TYPE_INTEGER ||
        (q.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY |
                    V4L2_CTRL_FLAG_INACTIVE | V4L2_CTRL_FLAG_GRABBED)))
      throw std::runtime_error(std::string("Unsupported/unwritable driver control: ") + c.name);
    if (c.requested < q.minimum || c.requested > q.maximum || q.step <= 0 ||
        (static_cast<long long>(c.requested) - q.minimum) % q.step != 0)
      throw std::runtime_error(std::string("Requested control outside driver range/step: ") + c.name);
    v4l2_control value{};
    value.id = c.id;
    call(VIDIOC_G_CTRL, &value, std::string("Read initial control ") + c.name);
    c.before = value.value;
  }
  std::vector<Control*> attempted;
  try {
    for (auto& c : controls) {
      if (c.requested == -1) continue;
      attempted.push_back(&c);
      v4l2_control value{};
      value.id = c.id;
      value.value = c.requested;
      call(VIDIOC_S_CTRL, &value, std::string("Set control ") + c.name);
    }
    std::ostringstream result;
    result << "controls_verified";
    for (const auto& c : controls) {
      if (c.requested == -1) continue;
      v4l2_control value{};
      value.id = c.id;
      call(VIDIOC_G_CTRL, &value, std::string("Read back control ") + c.name);
      if (value.value != c.requested)
        throw std::runtime_error(std::string("Control readback differs from request: ") + c.name);
      result << ' ' << c.name << '=' << value.value;
    }
    return result.str();
  } catch (const std::exception& error) {
    // A failed startup must not silently leave a partially applied control set.
    std::string rollback;
    for (auto* c : attempted) {
      v4l2_control value{};
      value.id = c->id;
      value.value = c->before;
      if (::ioctl(fd.value, VIDIOC_S_CTRL, &value) < 0)
        rollback += std::string(" restore_failed=") + c->name;
    }
    throw std::runtime_error(std::string(error.what()) + rollback);
  }
#endif
}
}  // namespace ardustereo
