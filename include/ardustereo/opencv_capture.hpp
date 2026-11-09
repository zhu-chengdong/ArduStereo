#pragma once
#include "ardustereo/capture.hpp"

namespace ardustereo {
std::unique_ptr<CaptureSource> make_opencv_v4l2_source();
}  // namespace ardustereo
