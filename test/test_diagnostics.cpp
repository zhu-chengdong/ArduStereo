#include "ardustereo/diagnostics.hpp"

#include <atomic>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace ardustereo;
namespace {
void require(bool ok) { if (!ok) throw std::runtime_error("Diagnostic contract failed"); }
void near(double a, double b) { require(std::abs(a-b) < 1e-8); }
template <typename F> void rejects(F f) {
  bool caught=false; try { f(); } catch (const std::exception&) { caught=true; } require(caught);
}
struct Fake {
  std::atomic<std::uint64_t> time{0};
  int mode=0, reads=0, closes=0;
  std::uint64_t system_stamp=1000000001;
  std::promise<void>* entered=nullptr;
  std::shared_future<void> release;
};
class Source : public CaptureSource {
 public:
  explicit Source(std::shared_ptr<Fake> state): state_(std::move(state)) {}
  void open(const CaptureConfig& config) override {
    if (state_->mode==1) throw std::runtime_error("open failure");
    c_=config;
  }
  bool read(CapturedFrame& f) override {
    ++state_->reads;
    if (state_->entered) { state_->entered->set_value(); state_->release.wait(); }
    if (state_->mode==2) return false;
    if (state_->mode==3) throw std::runtime_error("read exception");
    f.image.width=c_.width; f.image.height=c_.height;
    f.image.data.assign(static_cast<std::size_t>(c_.width)*c_.height,42);
    f.received_system_ns=state_->mode==5 ? 0 : state_->system_stamp;
    if (state_->mode==4) f.image.data.pop_back();
    return true;
  }
  void close() noexcept override { ++state_->closes; }
  std::string description() const override { return "injected diagnostic test source"; }
 private:
  std::shared_ptr<Fake> state_; CaptureConfig c_;
};
struct Fixture {
  std::shared_ptr<Fake> fake=std::make_shared<Fake>();
  RuntimeDiagnostics monitor;
  MonitoredCapture capture;
  CaptureConfig config;
  CapturedFrame frame;
  explicit Fixture(DiagnosticConfig c = {}, unsigned maximum=3)
      : monitor(c,[this] {return fake->time.load();}),
        capture(std::unique_ptr<CaptureSource>(new Source(fake)),monitor,maximum) {
    config.width=8; config.height=3; config.split_column=4; config.warmup_frames=0;
  }
  void open() { capture.open(config); }
  void tick(double seconds) { fake->time=static_cast<std::uint64_t>(seconds*1e9); }
  void publish() { require(capture.read(frame)); monitor.pair_published(true); }
};
DiagnosticConfig short_window() {
  DiagnosticConfig c; c.window_seconds=1; c.stale_after_seconds=0.3; return c;
}
}
int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests={
    {"configuration rejects nonfinite and invalid monitoring references", [] {
      auto c=short_window(); validate_diagnostic_config(c);
      c.window_seconds=0; rejects([&]{validate_diagnostic_config(c);});
      c=short_window(); c.expected_fps=-1; rejects([&]{validate_diagnostic_config(c);});
      c=short_window(); c.stale_after_seconds=std::numeric_limits<double>::quiet_NaN();
      rejects([&]{validate_diagnostic_config(c);});
      c=short_window(); c.minimum_fps_ratio=1.1; rejects([&]{validate_diagnostic_config(c);});
      rejects([]{RuntimeDiagnostics m({},{});});
    }},
    {"startup and waiting states do not imply zero device drops", [] {
      Fixture f; require(f.monitor.snapshot().state=="starting"); f.open();
      auto s=f.monitor.snapshot(); require(s.state=="waiting_for_frame" && s.last_frame_age_seconds==-1);
      near(s.capture_fps,0); require(s.read_attempts==0);
    }},
    {"20 FPS uses elapsed monotonic time and excludes startup boundary", [] {
      Fixture f(short_window()); f.open();
      for(int i=0;i<=20;++i){f.tick(i/20.0);f.publish();}
      auto s=f.monitor.snapshot(); near(s.capture_fps,20);near(s.publication_fps,20);
      require(s.state=="running" && s.captured_frames==21 && s.published_pairs==21);
    }},
    {"host timestamp jumps cannot change diagnostic FPS", [] {
      Fixture f(short_window());f.open();
      for(int i=0;i<=20;++i){f.tick(i/20.0);f.fake->system_stamp=(i%2)?10:900000000000ULL;f.publish();}
      near(f.monitor.snapshot().capture_fps,20);
    }},
    {"stale rate decays to zero and a later frame restores running", [] {
      Fixture f(short_window());f.open();f.tick(0.1);f.publish();f.tick(2);
      auto s=f.monitor.snapshot();require(s.state=="stale" && s.level==3);
      near(s.capture_fps,0);near(s.publication_fps,0);near(s.last_frame_age_seconds,1.9);
      f.publish();require(f.monitor.snapshot().state=="running");
    }},
    {"empty reads retain counts but reset consecutive failures on recovery", [] {
      Fixture f;f.open();f.publish();f.fake->mode=2;f.tick(0.1);
      require(!f.capture.read(f.frame) && f.frame.image.data.empty());
      auto s=f.monitor.snapshot();require(s.state=="degraded" && s.empty_reads==1 && s.published_pairs==1);
      f.fake->mode=0;f.tick(0.2);f.publish();s=f.monitor.snapshot();
      require(s.state=="running" && s.empty_reads==1 && s.consecutive_empty_reads==0 && s.read_attempts==3);
    }},
    {"consecutive empty limit is terminal and cleanup cannot hide it", [] {
      Fixture f;f.open();f.fake->mode=2;
      require(!f.capture.read(f.frame));require(!f.capture.read(f.frame));
      rejects([&]{f.capture.read(f.frame);});f.capture.close();f.capture.close();
      auto s=f.monitor.snapshot();require(s.state=="error" && s.level==2 && s.error_stage=="empty_read_limit");
      require(s.empty_reads==3 && s.published_pairs==0 && f.fake->closes==1);
      rejects([&]{f.capture.read(f.frame);});
    }},
    {"bad frame counted as rejection with no stale publication", [] {
      Fixture f;f.open();f.publish();f.fake->mode=4;rejects([&]{f.capture.read(f.frame);});
      auto s=f.monitor.snapshot();require(s.error_stage=="frame_validation" && s.frame_rejections==1);
      require(s.read_exceptions==0 && s.captured_frames==1 && s.published_pairs==1 && f.frame.image.data.empty());
    }},
    {"missing host timestamp is a frame validation error", [] {
      Fixture f;f.open();f.fake->mode=5;rejects([&]{f.capture.read(f.frame);});
      auto s=f.monitor.snapshot();require(s.frame_rejections==1 && s.captured_frames==0);
    }},
    {"source exception is distinct from empty read and rejection", [] {
      Fixture f;f.open();f.fake->mode=3;rejects([&]{f.capture.read(f.frame);});
      auto s=f.monitor.snapshot();require(s.error_stage=="source_read" && s.read_exceptions==1);
      require(s.empty_reads==0 && s.frame_rejections==0 && !s.read_in_progress);
    }},
    {"failed open reports startup and closes partially acquired source", [] {
      Fixture f;f.fake->mode=1;rejects([&]{f.open();});
      auto s=f.monitor.snapshot();require(s.error_stage=="startup" && s.read_attempts==0 && f.fake->closes==1);
    }},
    {"warmup reads are excluded from runtime counters", [] {
      Fixture f;f.config.warmup_frames=5;f.open();require(f.fake->reads==5);
      require(f.monitor.snapshot().read_attempts==0);f.publish();
      require(f.fake->reads==6 && f.monitor.snapshot().captured_frames==1);
    }},
    {"warmup failure remains startup failure", [] {
      Fixture f;f.config.warmup_frames=2;f.fake->mode=2;rejects([&]{f.open();});
      auto s=f.monitor.snapshot();require(s.error_stage=="startup" && s.empty_reads==0 && f.fake->closes==1);
    }},
    {"snapshot remains available while injected read is blocked", [] {
      Fixture f(short_window());f.open();std::promise<void> entered,release;
      auto entered_future=entered.get_future();f.fake->entered=&entered;f.fake->release=release.get_future().share();
      auto read=std::async(std::launch::async,[&]{return f.capture.read(f.frame);});
      const bool reached=entered_future.wait_for(std::chrono::seconds(2))==std::future_status::ready;
      if (!reached) {release.set_value();read.wait();require(false);}
      f.tick(2);auto s=f.monitor.snapshot();release.set_value();require(read.get());
      require(s.state=="stale" && s.read_in_progress && s.read_attempts==1 && s.captured_frames==0);
      require(f.monitor.snapshot().state=="running");
    }},
    {"monitoring reference warns only after full observation window", [] {
      auto c=short_window();c.expected_fps=20;c.stale_after_seconds=2;Fixture f(c);f.open();
      f.tick(0.1);f.publish();require(f.monitor.snapshot().state=="running");
      for(int i=2;i<=10;++i){f.tick(i/10.0);f.publish();}
      auto s=f.monitor.snapshot();near(s.publication_fps,10);require(s.state=="degraded");
    }},
    {"zero reference disables rate warnings without disabling stale detection", [] {
      Fixture f(short_window());f.open();f.tick(1);f.publish();require(f.monitor.snapshot().state=="running");
      f.tick(2);require(f.monitor.snapshot().state=="stale");
    }},
    {"bounded window reports reduced observation span under extreme input", [] {
      auto c=short_window();c.window_seconds=10;Fixture f(c);f.open();
      for(int i=1;i<=5000;++i){f.tick(i/1000.0);f.publish();}
      auto s=f.monitor.snapshot();require(s.window_limited);near(s.capture_fps,1000);
      require(s.capture_window_seconds<10 && s.capture_window_seconds>4);
    }},
    {"publication counts distinguish optional composite and accepted frames", [] {
      Fixture f;f.open();require(f.capture.read(f.frame));f.monitor.pair_published(false);
      auto s=f.monitor.snapshot();require(s.published_pairs==1 && s.composite_publications==0);
      rejects([&]{f.monitor.pair_published(false);});
    }},
    {"normal stop freezes uptime and rejects further acquisition", [] {
      Fixture f;f.open();f.tick(1);f.publish();f.tick(2);f.capture.close();f.tick(20);
      auto s=f.monitor.snapshot();require(s.state=="stopped" && s.level==0);near(s.uptime_seconds,2);
      rejects([&]{f.capture.read(f.frame);});
    }},
    {"backwards injected clock is rejected before modifying counters", [] {
      Fixture f;f.tick(2);f.open();f.tick(1);rejects([&]{f.capture.read(f.frame);});
      f.tick(3);require(f.monitor.snapshot().read_attempts==0);
    }}
  };
  unsigned failed=0;
  for(const auto& test:tests){try{test.second();std::cout<<"PASS "<<test.first<<'\n';}
    catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}}
  std::cout<<"RESULT "<<tests.size()-failed<<'/'<<tests.size()<<" passed\n";
  return failed==0?0:1;
}
