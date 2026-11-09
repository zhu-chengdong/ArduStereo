#include "ardustereo/frame_ops.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace ardustereo {

std::size_t checked_product(std::size_t a, std::size_t b) {
  if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a)
    throw std::invalid_argument("Image byte count overflows size_t");
  return a * b;
}

void validate_image(const MonoImage& image) {
  if (image.width <= 0 || image.height <= 0)
    throw std::invalid_argument("Image dimensions must be positive");
  const auto expected = checked_product(static_cast<std::size_t>(image.width),
                                        static_cast<std::size_t>(image.height));
  if (image.data.size() != expected)
    throw std::invalid_argument("Packed mono8 data size does not match dimensions");
}

MonoImage normalize_mono8(const ImageView& view, int shift) {
  if (!view.data || view.width <= 0 || view.height <= 0)
    throw std::invalid_argument("Invalid or empty image view");
  const bool is8 = view.format == PixelFormat::Mono8;
  if (!is8 && view.format != PixelFormat::Mono16LE && view.format != PixelFormat::Mono16BE)
    throw std::invalid_argument("Unsupported pixel format");
  if (!is8 && (shift < 0 || shift > 8))
    throw std::invalid_argument("mono16 input requires an explicit shift in [0,8]");
  const auto width = static_cast<std::size_t>(view.width);
  const auto height = static_cast<std::size_t>(view.height);
  const auto row_bytes = checked_product(width, is8 ? 1U : 2U);
  if (view.stride < row_bytes)
    throw std::invalid_argument("Image stride is smaller than its pixel row");
  const auto prefix = checked_product(height - 1, view.stride);
  if (prefix > std::numeric_limits<std::size_t>::max() - row_bytes ||
      view.bytes < prefix + row_bytes)
    throw std::invalid_argument("Image view is truncated or its extent overflows");
  MonoImage out;
  out.width = view.width;
  out.height = view.height;
  out.data.resize(checked_product(width, height));
  for (std::size_t y = 0; y < height; ++y) {
    const auto* src = view.data + y * view.stride;
    auto* dst = out.data.data() + y * width;
    if (is8) {
      std::memcpy(dst, src, width);
    } else {
      for (std::size_t x = 0; x < width; ++x) {
        const auto a = static_cast<std::uint16_t>(src[2 * x]);
        const auto b = static_cast<std::uint16_t>(src[2 * x + 1]);
        const std::uint16_t value = view.format == PixelFormat::Mono16LE
            ? static_cast<std::uint16_t>(a | (b << 8))
            : static_cast<std::uint16_t>((a << 8) | b);
        const auto shifted = static_cast<std::uint16_t>(value >> shift);
        if (shifted > 255)
          throw std::invalid_argument("mono16 sample exceeds mono8 range after requested shift");
        dst[x] = static_cast<std::uint8_t>(shifted);
      }
    }
  }
  return out;
}

StereoImages split_stereo(const MonoImage& image, int split) {
  validate_image(image);
  if (split <= 0 || split >= image.width)
    throw std::invalid_argument("split_column must be inside the image");
  StereoImages out;
  out.left.width = split;
  out.right.width = image.width - split;
  out.left.height = out.right.height = image.height;
  const auto h = static_cast<std::size_t>(image.height);
  const auto l = static_cast<std::size_t>(split);
  const auto r = static_cast<std::size_t>(image.width - split);
  out.left.data.resize(checked_product(l, h));
  out.right.data.resize(checked_product(r, h));
  for (std::size_t y = 0; y < h; ++y) {
    const auto* row = image.data.data() + y * static_cast<std::size_t>(image.width);
    std::memcpy(out.left.data.data() + y * l, row, l);
    std::memcpy(out.right.data.data() + y * r, row + l, r);
  }
  return out;
}

}  // namespace ardustereo
