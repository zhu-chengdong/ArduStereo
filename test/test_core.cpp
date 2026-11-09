#include "ardustereo/capture.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace ardustereo;
namespace {
void require(bool value, const std::string& message = "Contract failed") {
  if (!value) throw std::runtime_error(message);
}
template <typename F> void rejects(F f) {
  bool thrown = false;
  try { f(); } catch (const std::exception&) { thrown = true; }
  require(thrown, "Invalid input was not rejected");
}
CaptureConfig small_config() {
  CaptureConfig c;
  c.width = 8; c.height = 3; c.split_column = 4; c.warmup_frames = 0;
  return c;
}
Calibration calibrated() {
  Calibration c;
  c.width = 4; c.height = 3;
  c.distortion_model = "plumb_bob";
  c.d = {0.1, -0.1, 0.0, 0.0, 0.01};
  c.k = {{5, 0, 1.5, 0, 5, 1, 0, 0, 1}};
  c.r = {{1, 0, 0, 0, 1, 0, 0, 0, 1}};
  c.p = {{5, 0, 1.5, 0, 0, 5, 1, 0, 0, 0, 1, 0}};
  return c;
}
struct SourceState { int opens = 0, closes = 0, reads = 0; int mode = 0; };
class TestSource final : public CaptureSource {
 public:
  explicit TestSource(std::shared_ptr<SourceState> state) : state_(std::move(state)) {}
  void open(const CaptureConfig& c) override {
    ++state_->opens;
    if (state_->mode == 1) throw std::runtime_error("injected open failure");
    c_ = c;
  }
  bool read(CapturedFrame& f) override {
    ++state_->reads;
    if (state_->mode == 2) return false;
    if (state_->mode == 5) throw std::runtime_error("injected read failure");
    f.image.width = c_.width;
    f.image.height = c_.height;
    f.image.data.assign(static_cast<std::size_t>(c_.width) * c_.height, 42);
    if (state_->mode == 3) f.image.width -= 1;
    f.received_system_ns = state_->mode == 4 ? 0 : 1000000001ULL;
    return true;
  }
  void close() noexcept override { ++state_->closes; }
  std::string description() const override { return "injected unit-test source"; }
 private:
  std::shared_ptr<SourceState> state_;
  CaptureConfig c_;
};
std::unique_ptr<CaptureSource> test_source(std::shared_ptr<SourceState> state) {
  return std::unique_ptr<CaptureSource>(new TestSource(std::move(state)));
}
}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
    {"hardware controls accept raw targets and reject nonpositive non-sentinel values", [] {
      auto c=small_config(); c.frame_rate=20; c.exposure=681; c.analogue_gain=100;
      validate_config(c);
      for (int bad : {0,-2}) {
        c=small_config(); c.frame_rate=bad; rejects([&] { validate_config(c); });
        c=small_config(); c.exposure=bad; rejects([&] { validate_config(c); });
        c=small_config(); c.analogue_gain=bad; rejects([&] { validate_config(c); });
      }
    }},
    {"synthetic source cannot pretend to apply hardware controls", [] {
      auto c=small_config(); c.exposure=681; auto source=make_synthetic_source();
      rejects([&] { source->open(c); });
    }},
    {"mono8 preserves bytes and owns its result", [] {
      std::vector<std::uint8_t> input{0, 1, 2, 253, 254, 255};
      const auto out = normalize_mono8({input.data(), input.size(), 3, 2, 3, PixelFormat::Mono8});
      require(out.data == input);
      input[0] = 99;
      require(out.data[0] == 0);
    }},
    {"padding is excluded, final row need not have padding", [] {
      std::vector<std::uint8_t> in{1,2,3,99,99,4,5,6};
      const auto out = normalize_mono8({in.data(), in.size(), 3, 2, 5, PixelFormat::Mono8});
      require(out.data == std::vector<std::uint8_t>({1,2,3,4,5,6}));
    }},
    {"offset ROI copies its own pixels, not parent allocation", [] {
      std::vector<std::uint8_t> parent(24, 222);
      for (int y=0; y<3; ++y) for (int x=0; x<4; ++x)
        parent[2 + 8*y + x] = static_cast<std::uint8_t>(4*y+x);
      const auto out = normalize_mono8({parent.data()+2, parent.size()-2, 4, 3, 8, PixelFormat::Mono8});
      require(out.data == std::vector<std::uint8_t>({0,1,2,3,4,5,6,7,8,9,10,11}));
    }},
    {"truncated input is rejected before reading", [] {
      const std::uint8_t data[6]{};
      rejects([&] { normalize_mono8({data, 5, 3, 2, 3, PixelFormat::Mono8}); });
    }},
    {"invalid stride, dimensions and null views are rejected", [] {
      const std::uint8_t data[6]{};
      rejects([&] { normalize_mono8({data, 6, 3, 2, 2, PixelFormat::Mono8}); });
      rejects([&] { normalize_mono8({data, 6, 0, 2, 3, PixelFormat::Mono8}); });
      rejects([&] { normalize_mono8({data, 6, -1, 2, 3, PixelFormat::Mono8}); });
      rejects([&] { normalize_mono8({nullptr, 6, 3, 2, 3, PixelFormat::Mono8}); });
    }},
    {"overflowing extents are rejected", [] {
      const std::uint8_t data[1]{};
      rejects([&] { checked_product(std::numeric_limits<std::size_t>::max(), 2); });
      require(checked_product(0, std::numeric_limits<std::size_t>::max()) == 0);
      rejects([&] { normalize_mono8({data, 1, 1, 3,
          std::numeric_limits<std::size_t>::max(), PixelFormat::Mono8}); });
    }},
    {"raw10 right-aligned explicit shift preserves range", [] {
      const std::uint8_t in[]{0,0, 1,0, 4,0, 255,1, 255,3};
      const auto out = normalize_mono8({in, sizeof(in), 5, 1, 10, PixelFormat::Mono16LE}, 2);
      require(out.data == std::vector<std::uint8_t>({0,0,1,127,255}));
    }},
    {"16-bit big endian and padding", [] {
      const std::uint8_t in[]{0,0,255,255,99,99, 128,0,1,0};
      const auto out = normalize_mono8({in, sizeof(in), 2, 2, 6, PixelFormat::Mono16BE}, 8);
      require(out.data == std::vector<std::uint8_t>({0,255,128,1}));
    }},
    {"16-bit conversion is never guessed", [] {
      const std::uint8_t in[]{0,0};
      rejects([&] { normalize_mono8({in,2,1,1,2,PixelFormat::Mono16LE}); });
      rejects([&] { normalize_mono8({in,2,1,1,2,PixelFormat::Mono16LE},9); });
      rejects([&] { normalize_mono8({in,2,1,1,2,static_cast<PixelFormat>(99)},0); });
    }},
    {"16-bit overflow is rejected instead of wrapped", [] {
      const std::uint8_t in[]{0,4};
      rejects([&] { normalize_mono8({in,2,1,1,2,PixelFormat::Mono16LE},2); });
    }},
    {"unequal stereo halves reconstruct every row", [] {
      MonoImage in; in.width=7; in.height=4;
      for (int i=0; i<28; ++i) in.data.push_back(static_cast<std::uint8_t>(i));
      auto pair = split_stereo(in,3);
      require(pair.left.width==3 && pair.right.width==4 && pair.left.height==4);
      std::vector<std::uint8_t> joined;
      for (int y=0; y<4; ++y) {
        joined.insert(joined.end(),pair.left.data.begin()+3*y,pair.left.data.begin()+3*(y+1));
        joined.insert(joined.end(),pair.right.data.begin()+4*y,pair.right.data.begin()+4*(y+1));
      }
      require(joined==in.data);
      pair.left.data[0]=255;
      require(in.data[0]==0 && pair.right.data[0]==3);
    }},
    {"invalid split positions and malformed images", [] {
      MonoImage in; in.width=4; in.height=1; in.data={1,2,3,4};
      rejects([&] { split_stereo(in,0); });
      rejects([&] { split_stereo(in,4); });
      rejects([&] { split_stereo(in,-1); });
      in.data.pop_back(); rejects([&] { split_stereo(in,2); });
    }},
    {"configuration validates mode combinations", [] {
      auto c=small_config(); validate_config(c);
      c.input_format="mono16"; rejects([&] { validate_config(c); });
      c.mono16_shift=2; validate_config(c);
      c.input_format="mono8"; rejects([&] { validate_config(c); });
      c.mono16_shift=-1; c.input_format="bgr8"; rejects([&] { validate_config(c); });
    }},
    {"configuration rejects invalid geometry and resources", [] {
      auto c=small_config(); c.split_column=8; rejects([&] { validate_config(c); });
      c=small_config(); c.width=0; rejects([&] { validate_config(c); });
      c=small_config(); c.buffer_size=0; rejects([&] { validate_config(c); });
      c=small_config(); c.warmup_frames=-1; rejects([&] { validate_config(c); });
      c=small_config(); c.device=""; rejects([&] { validate_config(c); });
      c=small_config(); c.fourcc="GRE"; rejects([&] { validate_config(c); });
      c.fourcc="GREY"; validate_config(c);
    }},
    {"synthetic source pattern and warmup sequence", [] {
      CaptureSession s(make_synthetic_source()); auto c=small_config(); c.warmup_frames=5;
      s.open(c); CapturedFrame f; require(s.read(f));
      require(f.source_counter==5 && f.received_system_ns>0);
      for(int y=0;y<c.height;++y) for(int x=0;x<c.width;++x)
        require(f.image.data[y*c.width+x]==(x+17*y+5)%256);
      require(s.read(f) && f.source_counter==6);
      s.close(); rejects([&] { s.read(f); });
      s.open(small_config()); require(s.read(f) && f.source_counter==0);
    }},
    {"synthetic source rejects non-mono8 and FourCC overrides", [] {
      CaptureSession s(make_synthetic_source()); auto c=small_config();
      c.fourcc="GREY"; rejects([&] { s.open(c); });
      c.fourcc=""; c.input_format="mono16"; c.mono16_shift=2;
      rejects([&] { s.open(c); });
    }},
    {"session rejects null source and reads before opening", [] {
      rejects([] { CaptureSession s(nullptr); });
      CaptureSession s(make_synthetic_source()); CapturedFrame f;
      rejects([&] { s.read(f); });
    }},
    {"session lifecycle closes exactly once", [] {
      auto state=std::make_shared<SourceState>();
      { CaptureSession s(test_source(state)); s.open(small_config());
        rejects([&] { s.open(small_config()); }); s.close(); s.close(); }
      require(state->opens==1 && state->closes==1);
    }},
    {"failed opening closes partially acquired resources", [] {
      auto state=std::make_shared<SourceState>(); state->mode=1;
      { CaptureSession s(test_source(state)); rejects([&] { s.open(small_config()); }); }
      require(state->opens==1 && state->closes==1);
    }},
    {"failed warmup closes the source", [] {
      auto state=std::make_shared<SourceState>(); state->mode=2;
      { CaptureSession s(test_source(state)); auto c=small_config(); c.warmup_frames=1;
        rejects([&] { s.open(c); }); }
      require(state->closes==1);
    }},
    {"failed read clears a previous frame", [] {
      auto state=std::make_shared<SourceState>(); CaptureSession s(test_source(state));
      s.open(small_config()); CapturedFrame f; require(s.read(f));
      state->mode=2; require(!s.read(f));
      require(f.image.data.empty() && f.received_system_ns==0);
    }},
    {"bad image dimensions never escape a session", [] {
      auto state=std::make_shared<SourceState>(); CaptureSession s(test_source(state));
      s.open(small_config()); state->mode=3; CapturedFrame f;
      rejects([&] { s.read(f); }); require(f.image.data.empty());
    }},
    {"missing source timestamp is rejected", [] {
      auto state=std::make_shared<SourceState>(); CaptureSession s(test_source(state));
      s.open(small_config()); state->mode=4; CapturedFrame f;
      rejects([&] { s.read(f); });
    }},
    {"read exception still releases source at destruction", [] {
      auto state=std::make_shared<SourceState>();
      { CaptureSession s(test_source(state)); s.open(small_config()); state->mode=5;
        CapturedFrame f; rejects([&] { s.read(f); }); }
      require(state->closes==1);
    }},
    {"uncalibrated info is explicit and optional", [] {
      Calibration c; c.width=4; c.height=3;
      validate_calibration(c,4,3,false);
      rejects([&] { validate_calibration(c,4,3,true); });
      c.p[0]=1; rejects([&] { validate_calibration(c,4,3,false); });
    }},
    {"valid raw calibration and right projection", [] {
      auto c=calibrated(); validate_calibration(c,4,3,true);
      c.p[3]=-0.5; validate_calibration(c,4,3,true);
      c.distortion_model="equidistant"; c.d.resize(4); validate_calibration(c,4,3,true);
    }},
    {"calibration resolution mismatch", [] {
      auto c=calibrated(); rejects([&] { validate_calibration(c,8,3,false); });
    }},
    {"calibration nonfinite values", [] {
      auto c=calibrated(); c.d[0]=std::numeric_limits<double>::quiet_NaN();
      rejects([&] { validate_calibration(c,4,3,true); });
      c=calibrated(); c.k[0]=std::numeric_limits<double>::infinity();
      rejects([&] { validate_calibration(c,4,3,true); });
    }},
    {"calibration model and coefficient mismatch", [] {
      auto c=calibrated(); c.distortion_model="radtan";
      rejects([&] { validate_calibration(c,4,3,true); });
      c=calibrated(); c.d.resize(4); rejects([&] { validate_calibration(c,4,3,true); });
    }},
    {"calibration rotation and focal validity", [] {
      auto c=calibrated(); c.r[1]=0.2; rejects([&] { validate_calibration(c,4,3,true); });
      c=calibrated(); c.r[0]=-1; rejects([&] { validate_calibration(c,4,3,true); });
      c=calibrated(); c.k[4]=0; rejects([&] { validate_calibration(c,4,3,true); });
    }}
  };
  int failed=0;
  for (const auto& t : tests) {
    try { t.second(); std::cout << "PASS " << t.first << '\n'; }
    catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << t.first << ": " << e.what() << '\n'; }
  }
  std::cout << "RESULT " << tests.size()-static_cast<std::size_t>(failed) << "/" << tests.size() << " passed\n";
  return failed==0 ? 0 : 1;
}
