#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ardustereo {

enum class PixelFormat { Mono8, Mono16LE, Mono16BE };

// data points to the first pixel of this view, not the parent allocation.
// bytes is the accessible byte extent from data; stride may include padding.
struct ImageView {
  const std::uint8_t* data = nullptr;
  std::size_t bytes = 0;
  int width = 0;
  int height = 0;
  std::size_t stride = 0;
  PixelFormat format = PixelFormat::Mono8;
};

struct MonoImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> data;
};

struct StereoImages {
  MonoImage left;
  MonoImage right;
};

std::size_t checked_product(std::size_t a, std::size_t b);
void validate_image(const MonoImage& image);
// 16-bit input is rejected unless a bit shift in [0,8] is explicit.
// Values which would overflow mono8 are rejected, never wrapped or rescaled.
MonoImage normalize_mono8(const ImageView& view, int mono16_shift = -1);
StereoImages split_stereo(const MonoImage& image, int split_column);

}  // namespace ardustereo
