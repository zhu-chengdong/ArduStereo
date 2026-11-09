#include "ardustereo/diagnostics.hpp"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// Numeric event replay only. No CaptureSource/OpenCV/V4L2/ROS instance is used.
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  std::ifstream input(argv[1]);
  std::ofstream output(argv[2]);
  if (!input || !output) return 3;
  std::uint64_t clock = 0;
  ardustereo::DiagnosticConfig config;
  config.expected_fps = 20;
  ardustereo::RuntimeDiagnostics diagnostic(config, [&] { return clock; });
  diagnostic.ready();
  output << "time_ns,publication_fps,capture_fps,level,state,captured,published,window_limited\n";
  char kind;
  std::uint64_t time;
  while (input >> kind >> time) {
    if (time < clock) throw std::runtime_error("Replay clock moved backward");
    clock = time;
    if (kind == 'E') {
      diagnostic.read_started();
      diagnostic.frame_received();
      diagnostic.pair_published(true);
    } else if (kind == 'Q') {
      auto s = diagnostic.snapshot();
      output << time << ',' << std::setprecision(17) << s.publication_fps << ','
             << s.capture_fps << ',' << s.level << ',' << s.state << ','
             << s.captured_frames << ',' << s.published_pairs << ',' << s.window_limited << '\n';
    } else return 4;
  }
  return 0;
}
