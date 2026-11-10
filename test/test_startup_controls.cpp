#include "v4l2_controls.hpp"
#include <array>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstring>
#include <iostream>
#include <linux/videodev2.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Model {
  std::array<int, 3> cached{{50, 681, 100}}, actual{{50, 1000, 200}};
  std::array<int, 3> low{{5, 1, 100}}, high{{60, 65523, 1590}}, step{{1, 1, 1}};
  std::vector<std::pair<unsigned, int>> writes;
  std::vector<unsigned> queries;
  int opens = 0, closes = 0, fail_write = -1, failed = 0;
  bool restore_fail = false, mismatch_once = false, query_fail = false;
} model;
const unsigned ids[] = {0x00981906U, V4L2_CID_EXPOSURE, V4L2_CID_ANALOGUE_GAIN};
const char* names[] = {"frame_rate", "Exposure", "Analogue Gain"};
unsigned index_of(unsigned id) {
  for (unsigned i = 0; i < 3; ++i) if (id == ids[i]) return i;
  throw std::runtime_error("Unexpected control ID in offline model");
}
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
ardustereo::CaptureConfig config(int fps = 20, int exposure = 681, int gain = 100) {
  ardustereo::CaptureConfig c; c.frame_rate = fps; c.exposure = exposure; c.analogue_gain = gain; return c;
}
template<class F> std::string rejects(F f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  throw std::runtime_error("Expected startup rejection");
}
void reset() { model = Model{}; }
}

// Link-time wrappers make this executable incapable of opening a real camera.
extern "C" int __wrap_open(const char* path, int, ...) {
  if (std::string(path) != "/dev/video0") { errno = EACCES; return -1; }
  ++model.opens; return 42;
}
extern "C" int __wrap_close(int fd) {
  if (fd != 42) { errno = EBADF; return -1; }
  ++model.closes; return 0;
}
extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
  if (fd != 42) { errno = EBADF; return -1; }
  va_list args; va_start(args, request); void* p = va_arg(args, void*); va_end(args);
  if (request == VIDIOC_QUERYCTRL) {
    auto& q = *static_cast<v4l2_queryctrl*>(p); const auto i = index_of(q.id); model.queries.push_back(i);
    if (model.query_fail) { errno = EIO; return -1; }
    q.type = V4L2_CTRL_TYPE_INTEGER; std::strncpy(reinterpret_cast<char*>(q.name), names[i], sizeof(q.name)-1);
    q.minimum=model.low[i]; q.maximum=model.high[i]; q.step=model.step[i]; q.flags=0; return 0;
  }
  auto& c = *static_cast<v4l2_control*>(p); const auto i = index_of(c.id);
  if (request == VIDIOC_G_CTRL) {
    c.value = model.cached[i];
    if (model.mismatch_once && i == 2 && model.writes.size() >= 5) { model.mismatch_once=false; c.value=101; }
    return 0;
  }
  if (request == VIDIOC_S_CTRL) {
    model.writes.emplace_back(i, c.value);
    if (model.restore_fail && model.failed && i==0 && c.value==50) { errno=EIO; return -1; }
    // A non-volatile cached control does not call hardware for an equal value.
    if (c.value != model.cached[i]) {
      model.cached[i]=c.value; model.actual[i]=c.value;
      if (i==0) { model.actual[1]=1000; model.actual[2]=200; }
    }
    if (static_cast<int>(model.writes.size()) == model.fail_write) { ++model.failed; errno=EIO; return -1; }
    return 0;
  }
  errno=ENOTTY; return -1;
}

int main() {
  unsigned cases=0;
  try {
    { reset(); const auto result=ardustereo::apply_startup_controls(config());
      check(model.cached==std::array<int,3>{{20,681,100}},"Cached readback differs");
      check(model.actual==model.cached,"REGRESSION: cached success did not apply actual default exposure/gain");
      check(model.writes==std::vector<std::pair<unsigned,int>>{{0,20},{1,682},{1,681},{2,101},{2,100}},"Unexpected startup order");
      check(result.find("exposure=681")!=std::string::npos && model.closes==1,"Missing success description or fd cleanup"); ++cases; }
    { reset(); ardustereo::apply_startup_controls(config(20,65523,1590));
      check(model.writes[1].second==65522 && model.writes[3].second==1589,"Upper boundary must use lower neighbour");
      check(model.actual==std::array<int,3>{{20,65523,1590}},"Upper target not applied"); ++cases; }
    { reset(); model.high[1]=INT_MAX; model.cached[1]=INT_MAX; ardustereo::apply_startup_controls(config(20,INT_MAX,100));
      check(model.actual[1]==INT_MAX && model.writes[1].second==INT_MAX-1,"Overflow in neighbour calculation"); ++cases; }
    { reset(); ardustereo::apply_startup_controls(config(-1,681,-1));
      check(model.queries==std::vector<unsigned>{1} && model.writes==std::vector<std::pair<unsigned,int>>{{1,682},{1,681}},"Unrequested controls touched"); ++cases; }
    { reset(); check(ardustereo::apply_startup_controls(config(-1,-1,-1))=="controls=not_requested" && model.opens==0,"No-request path accessed device"); ++cases; }
    { reset(); rejects([]{ardustereo::apply_startup_controls(config(20,0,100));}); check(model.writes.empty() && model.closes==1,"Invalid target wrote controls"); ++cases; }
    { reset(); model.low[2]=model.high[2]=100; rejects([]{ardustereo::apply_startup_controls(config());}); check(model.writes.empty(),"Singleton range not rejected before writes"); ++cases; }
    { reset(); model.step[1]=2; rejects([]{ardustereo::apply_startup_controls(config(20,682,100));}); check(model.writes.empty(),"Invalid step wrote controls"); ++cases; }
    { reset(); model.query_fail=true; rejects([]{ardustereo::apply_startup_controls(config());}); check(model.writes.empty() && model.closes==1,"Query failure wrote controls or leaked fd"); ++cases; }
    { reset(); const auto before=model.cached; model.fail_write=3; rejects([]{ardustereo::apply_startup_controls(config());});
      check(model.cached==before && model.closes==1,"Partial write did not restore nominal prior readback or close fd"); ++cases; }
    { reset(); const auto before=model.cached; model.mismatch_once=true; rejects([]{ardustereo::apply_startup_controls(config());});
      check(model.cached==before && model.closes==1,"Final mismatch not restored"); ++cases; }
    { reset(); model.fail_write=3; model.restore_fail=true; const auto e=rejects([]{ardustereo::apply_startup_controls(config());});
      check(e.find("startup_rollback_failed_index=0")!=std::string::npos && model.closes==1,"Rollback failure hidden"); ++cases; }
    std::cout << "PASS " << cases << " startup initialization contracts (wrapped ioctl; no device)\n"; return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL after " << cases << " cases: " << e.what() << '\n'; return 1; }
}
