// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include "ftxui/screen/sixel.hpp"

#include <algorithm>  // for sort, min, max, clamp
#include <array>      // for array
#include <atomic>     // for atomic
#include <cmath>      // for cbrt, sqrt, lround
#include <cstddef>    // for size_t
#include <cstdint>    // for uint8_t, uint16_t, uint32_t, uint64_t, int16_t
#include <memory>     // for shared_ptr, unique_ptr, make_unique
#include <mutex>      // for mutex, lock_guard
#include <string>     // for string
#include <utility>    // for move
#include <vector>     // for vector

#include "ftxui/screen/bitmap_internal.hpp"  // for Bitmap::Storage

namespace ftxui {

namespace {

constexpr int g_min_colors = 2;
constexpr int g_max_colors = 256;

// Below this alpha, a pixel is not drawn.
constexpr int g_alpha_threshold = 128;

// The palette lookup table has one entry per color expressed with 5 bits of
// red, 6 bits of green, and 5 bits of blue.
constexpr int g_lut_red_bits = 5;
constexpr int g_lut_green_bits = 6;
constexpr int g_lut_blue_bits = 5;
constexpr size_t g_lut_size =
    size_t{1} << (g_lut_red_bits + g_lut_green_bits + g_lut_blue_bits);
constexpr uint16_t g_lut_empty = 0xFFFF;

// The size of the hash table of the colors of an exact palette.
constexpr size_t g_exact_size = 1024;  // Must be > 2 * g_max_colors.
constexpr uint32_t g_exact_empty = 0xFFFFFFFF;

size_t ExactSlot(uint32_t color) {
  return (color * 2654435761U) >> 22;  // 10 bits.
}

// Sixel expresses a color channel as a percentage.
int ToPercent(int v) {
  return (v * 100 + 127) / 255;
}
int FromPercent(int v) {
  return (v * 255 + 50) / 100;
}

int Distance(int r1, int g1, int b1, int r2, int g2, int b2) {
  const int dr = r1 - r2;
  const int dg = g1 - g2;
  const int db = b1 - b2;
  return 3 * dr * dr + 4 * dg * dg + 2 * db * db;
}

// Turn a pixel into the color it is displayed with.
struct Blend {
  explicit Blend(const Color& background)
      : enabled(background.GetAlpha() != 0),
        red(background.GetRed()),
        green(background.GetGreen()),
        blue(background.GetBlue()) {}

  // Returns whether the pixel is displayed.
  bool operator()(int alpha, int* r, int* g, int* b) const {
    if (!enabled) {
      return alpha >= g_alpha_threshold;
    }
    *r = (*r * alpha + red * (255 - alpha) + 127) / 255;
    *g = (*g * alpha + green * (255 - alpha) + 127) / 255;
    *b = (*b * alpha + blue * (255 - alpha) + 127) / 255;
    return true;
  }

  bool enabled;
  int red;
  int green;
  int blue;
};

void AppendInt(std::string& out, int value) {
  std::array<char, 12> buffer{};
  size_t i = buffer.size();
  if (value <= 0) {
    out += '0';
    return;
  }
  while (value > 0) {
    buffer[--i] = static_cast<char>('0' + value % 10);  // NOLINT
    value /= 10;
  }
  out.append(buffer.data() + i, buffer.size() - i);  // NOLINT
}

// A group of colors of the histogram, to be represented by a single color of
// the palette.
struct Bin {
  int red = 0;
  int green = 0;
  int blue = 0;
  uint32_t count = 0;

  int channel(int i) const { return i == 0 ? red : i == 1 ? green : blue; }
};

// A range of `Bin` to be split by the median cut algorithm.
struct Box {
  size_t begin = 0;
  size_t end = 0;
  double score = 0.0;  // The squared error along the worst axis.
  int axis = 0;
  double mean = 0.0;  // Along the worst axis.
};

void ComputeBox(const std::vector<Bin>& bins, Box& box) {
  double count = 0.0;
  std::array<double, 3> sum = {0.0, 0.0, 0.0};
  std::array<double, 3> sum2 = {0.0, 0.0, 0.0};
  for (size_t i = box.begin; i < box.end; ++i) {
    const Bin& bin = bins[i];
    count += bin.count;
    for (size_t c = 0; c < 3; ++c) {
      const double v = bin.channel(static_cast<int>(c));
      sum[c] += v * bin.count;
      sum2[c] += v * v * bin.count;
    }
  }
  // The eye is more sensitive to green, then red, then blue.
  const std::array<double, 3> weight = {3.0, 4.0, 2.0};
  box.score = -1.0;
  for (size_t c = 0; c < 3; ++c) {
    const double mean = sum[c] / std::max(count, 1.0);
    const double error = (sum2[c] - mean * sum[c]) * weight[c];
    if (error > box.score) {
      box.score = error;
      box.axis = static_cast<int>(c);
      box.mean = mean;
    }
  }
  if (box.end - box.begin < 2) {
    box.score = 0.0;
  }
}

// Reduce the colors of `bins` to at most `colors` representative ones.
std::vector<SixelPalette::RGB> MedianCut(std::vector<Bin> bins, int colors) {
  std::vector<Box> boxes;
  boxes.push_back(Box{0, bins.size(), 0.0, 0, 0.0});
  ComputeBox(bins, boxes.back());

  while (static_cast<int>(boxes.size()) < colors) {
    // Split the box with the largest error.
    size_t worst = 0;
    for (size_t i = 1; i < boxes.size(); ++i) {
      if (boxes[i].score > boxes[worst].score) {
        worst = i;
      }
    }
    Box box = boxes[worst];
    if (box.score <= 0.0) {
      break;
    }

    const int axis = box.axis;
    const auto first = bins.begin() + static_cast<std::ptrdiff_t>(box.begin);
    const auto last = bins.begin() + static_cast<std::ptrdiff_t>(box.end);
    const auto middle = std::partition(first, last, [&](const Bin& bin) {
      return bin.channel(axis) <= box.mean;
    });
    auto split = static_cast<size_t>(middle - bins.begin());
    // Both halves must be non empty.
    split = std::clamp(split, box.begin + 1, box.end - 1);

    Box left{box.begin, split, 0.0, 0, 0.0};
    Box right{split, box.end, 0.0, 0, 0.0};
    ComputeBox(bins, left);
    ComputeBox(bins, right);
    boxes[worst] = left;
    boxes.push_back(right);
  }

  std::vector<SixelPalette::RGB> out;
  out.reserve(boxes.size());
  for (const Box& box : boxes) {
    double count = 0.0;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    for (size_t i = box.begin; i < box.end; ++i) {
      count += bins[i].count;
      red += double(bins[i].red) * bins[i].count;
      green += double(bins[i].green) * bins[i].count;
      blue += double(bins[i].blue) * bins[i].count;
    }
    if (count <= 0.0) {
      continue;
    }
    out.push_back({
        static_cast<uint8_t>(std::lround(red / count)),
        static_cast<uint8_t>(std::lround(green / count)),
        static_cast<uint8_t>(std::lround(blue / count)),
    });
  }
  return out;
}

// Move each color of the palette toward the mean of the colors it represents.
void Refine(const std::vector<Bin>& bins,
            std::vector<SixelPalette::RGB>& palette) {
  struct Sum {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double count = 0.0;
  };
  std::vector<Sum> sums(palette.size());
  for (const Bin& bin : bins) {
    size_t best = 0;
    int best_distance = Distance(bin.red, bin.green, bin.blue, palette[0].red,
                                 palette[0].green, palette[0].blue);
    for (size_t i = 1; i < palette.size(); ++i) {
      const int distance =
          Distance(bin.red, bin.green, bin.blue, palette[i].red,
                   palette[i].green, palette[i].blue);
      if (distance < best_distance) {
        best_distance = distance;
        best = i;
      }
    }
    sums[best].red += double(bin.red) * bin.count;
    sums[best].green += double(bin.green) * bin.count;
    sums[best].blue += double(bin.blue) * bin.count;
    sums[best].count += bin.count;
  }
  for (size_t i = 0; i < palette.size(); ++i) {
    if (sums[i].count <= 0.0) {
      continue;
    }
    palette[i] = {
        static_cast<uint8_t>(std::lround(sums[i].red / sums[i].count)),
        static_cast<uint8_t>(std::lround(sums[i].green / sums[i].count)),
        static_cast<uint8_t>(std::lround(sums[i].blue / sums[i].count)),
    };
  }
}

// The 8x8 Bayer matrix.
constexpr std::array<uint8_t, 64> g_bayer = {
    0,  32, 8,  40, 2,  34, 10, 42,  //
    48, 16, 56, 24, 50, 18, 58, 26,  //
    12, 44, 4,  36, 14, 46, 6,  38,  //
    60, 28, 52, 20, 62, 30, 54, 22,  //
    3,  35, 11, 43, 1,  33, 9,  41,  //
    51, 19, 59, 27, 49, 17, 57, 25,  //
    15, 47, 7,  39, 13, 45, 5,  37,  //
    63, 31, 55, 23, 61, 29, 53, 21,  //
};

}  // namespace

struct SixelPalette::Impl {
  std::vector<RGB> colors;
  uint64_t id = 0;

  // When the palette holds every color of the bitmap, they are found exactly,
  // using this hash table: color => index.
  bool exact = false;
  std::array<uint32_t, g_exact_size> exact_keys = {};
  std::array<uint8_t, g_exact_size> exact_values = {};

  // A cache of the nearest color of the palette, filled on demand. It is made
  // of atomics, because a palette is shared in between every user of a bitmap.
  std::unique_ptr<std::atomic<uint16_t>[]> lut;  // NOLINT

  void Finalize() {
    exact_keys.fill(g_exact_empty);
    if (exact) {
      for (size_t i = 0; i < colors.size(); ++i) {
        const uint32_t color = (uint32_t{colors[i].red} << 16) |
                               (uint32_t{colors[i].green} << 8) |
                               uint32_t{colors[i].blue};
        size_t slot = ExactSlot(color);
        while (exact_keys[slot] != g_exact_empty) {
          slot = (slot + 1) % g_exact_size;
        }
        exact_keys[slot] = color;
        exact_values[slot] = static_cast<uint8_t>(i);
      }
    }
    // NOLINTNEXTLINE
    lut.reset(new std::atomic<uint16_t>[g_lut_size]);
    for (size_t i = 0; i < g_lut_size; ++i) {
      lut[i].store(g_lut_empty, std::memory_order_relaxed);
    }
  }

  int Nearest(int red, int green, int blue) const {
    if (exact) {
      const uint32_t color = (static_cast<uint32_t>(red) << 16) |
                             (static_cast<uint32_t>(green) << 8) |
                             static_cast<uint32_t>(blue);
      size_t slot = ExactSlot(color);
      while (exact_keys[slot] != g_exact_empty) {
        if (exact_keys[slot] == color) {
          return exact_values[slot];
        }
        slot = (slot + 1) % g_exact_size;
      }
    }

    const int r = red >> (8 - g_lut_red_bits);
    const int g = green >> (8 - g_lut_green_bits);
    const int b = blue >> (8 - g_lut_blue_bits);
    const size_t key =
        (static_cast<size_t>(r) << (g_lut_green_bits + g_lut_blue_bits)) |
        (static_cast<size_t>(g) << g_lut_blue_bits) | static_cast<size_t>(b);
    const uint16_t cached = lut[key].load(std::memory_order_relaxed);
    if (cached != g_lut_empty) {
      return cached;
    }
    return NearestSlow(key, r, g, b);
  }

  int NearestSlow(size_t key, int r, int g, int b) const {
    // The color at the center of the lookup table entry.
    const int red = (r << (8 - g_lut_red_bits)) + (1 << (7 - g_lut_red_bits));
    const int green =
        (g << (8 - g_lut_green_bits)) + (1 << (7 - g_lut_green_bits));
    const int blue =
        (b << (8 - g_lut_blue_bits)) + (1 << (7 - g_lut_blue_bits));
    size_t best = 0;
    int best_distance = Distance(red, green, blue, colors[0].red,
                                 colors[0].green, colors[0].blue);
    for (size_t i = 1; i < colors.size(); ++i) {
      const int distance = Distance(red, green, blue, colors[i].red,
                                    colors[i].green, colors[i].blue);
      if (distance < best_distance) {
        best_distance = distance;
        best = i;
      }
    }
    lut[key].store(static_cast<uint16_t>(best), std::memory_order_relaxed);
    return static_cast<int>(best);
  }
};

SixelPalette::SixelPalette() : impl_(std::make_unique<Impl>()) {
  static std::atomic<uint64_t> next{1};
  impl_->id = next.fetch_add(1, std::memory_order_relaxed);
}

SixelPalette::~SixelPalette() = default;

// static
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::shared_ptr<const SixelPalette> SixelPalette::Build(const Bitmap& bitmap,
                                                        int colors,
                                                        Color background) {
  colors = std::clamp(colors, g_min_colors, g_max_colors);
  std::shared_ptr<SixelPalette> palette(new SixelPalette());
  auto& out = palette->impl_->colors;

  const int width = bitmap.width();
  const int height = bitmap.height();
  const uint8_t* data = bitmap.data();
  const size_t pixel_count =
      static_cast<size_t>(width) * static_cast<size_t>(height);

  // The color of a pixel, as it is going to be displayed. Returns false when
  // it is not displayed.
  const Blend blend(background);
  auto displayed = [&](const uint8_t* pixel, int* red, int* green, int* blue) {
    *red = pixel[0];                           // NOLINT
    *green = pixel[1];                         // NOLINT
    *blue = pixel[2];                          // NOLINT
    return blend(pixel[3], red, green, blue);  // NOLINT
  };

  // Step 1: An image made of a few colors keeps them all, untouched. This is
  // what keeps logos, diagrams and pixel art crisp.
  bool exact = true;
  {
    // A small open addressing hash set of the colors seen so far.
    constexpr uint32_t empty = g_exact_empty;
    constexpr size_t table_size = g_exact_size;
    std::array<uint32_t, table_size> table = {};
    table.fill(empty);
    std::vector<uint32_t> seen;
    uint32_t previous = empty;
    for (size_t i = 0; i < pixel_count && exact; ++i) {
      int red = 0;
      int green = 0;
      int blue = 0;
      if (!displayed(data + i * 4, &red, &green, &blue)) {  // NOLINT
        continue;
      }
      const uint32_t color = (static_cast<uint32_t>(red) << 16) |
                             (static_cast<uint32_t>(green) << 8) |
                             static_cast<uint32_t>(blue);
      if (color == previous) {
        continue;
      }
      previous = color;
      size_t slot = ExactSlot(color);
      while (table[slot] != empty && table[slot] != color) {
        slot = (slot + 1) % table_size;
      }
      if (table[slot] == color) {
        continue;
      }
      table[slot] = color;
      seen.push_back(color);
      exact = (static_cast<int>(seen.size()) <= colors);
    }
    if (exact) {
      for (const uint32_t color : seen) {
        out.push_back({
            static_cast<uint8_t>(color >> 16),
            static_cast<uint8_t>(color >> 8),
            static_cast<uint8_t>(color),
        });
      }
    }
  }

  // Step 2: Otherwise, reduce the colors with the median cut algorithm,
  // applied to an histogram of a sample of the pixels.
  if (!exact) {
    constexpr int bits = 5;
    constexpr int shift = 8 - bits;
    constexpr size_t histogram_size = size_t{1} << (3 * bits);
    struct Accumulator {
      uint32_t count = 0;
      uint32_t red = 0;
      uint32_t green = 0;
      uint32_t blue = 0;
    };
    std::vector<Accumulator> histogram(histogram_size);

    constexpr double max_samples = 1 << 19;
    const int step = std::max(
        1, static_cast<int>(std::sqrt(double(pixel_count) / max_samples)));
    for (int y = 0; y < height; y += step) {
      const uint8_t* row =
          data + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
      for (int x = 0; x < width; x += step) {
        int red = 0;
        int green = 0;
        int blue = 0;
        // NOLINTNEXTLINE
        if (!displayed(row + static_cast<size_t>(x) * 4, &red, &green, &blue)) {
          continue;
        }
        const size_t index = (static_cast<size_t>(red >> shift) << (2 * bits)) |
                             (static_cast<size_t>(green >> shift) << bits) |
                             static_cast<size_t>(blue >> shift);
        Accumulator& accumulator = histogram[index];
        accumulator.count++;
        accumulator.red += static_cast<uint32_t>(red);
        accumulator.green += static_cast<uint32_t>(green);
        accumulator.blue += static_cast<uint32_t>(blue);
      }
    }

    std::vector<Bin> bins;
    for (const Accumulator& accumulator : histogram) {
      if (accumulator.count == 0) {
        continue;
      }
      Bin bin;
      bin.count = accumulator.count;
      bin.red = static_cast<int>(accumulator.red / accumulator.count);
      bin.green = static_cast<int>(accumulator.green / accumulator.count);
      bin.blue = static_cast<int>(accumulator.blue / accumulator.count);
      bins.push_back(bin);
    }

    if (!bins.empty()) {
      out = MedianCut(bins, colors);
      Refine(bins, out);
      Refine(bins, out);
    }
  }

  // A fully transparent image has no color. Provide one, so that the palette
  // is never empty.
  if (out.empty()) {
    out.push_back({0, 0, 0});
  }

  // Step 3: A terminal only knows about the colors sixel can express. Snap the
  // palette to them, so that dithering compensates for the difference.
  std::vector<RGB> snapped;
  snapped.reserve(out.size());
  for (const RGB& color : out) {
    const RGB candidate = {
        static_cast<uint8_t>(FromPercent(ToPercent(color.red))),
        static_cast<uint8_t>(FromPercent(ToPercent(color.green))),
        static_cast<uint8_t>(FromPercent(ToPercent(color.blue))),
    };
    const bool duplicate =
        std::any_of(snapped.begin(), snapped.end(), [&](const RGB& other) {
          return other.red == candidate.red && other.green == candidate.green &&
                 other.blue == candidate.blue;
        });
    if (!duplicate) {
      snapped.push_back(candidate);
    }
  }
  // The exact colors are still looked up by their original value. This only
  // works if none of them was merged with another one.
  const bool keep_exact = exact && snapped.size() == out.size();
  std::vector<RGB> original = std::move(out);
  out = std::move(snapped);
  palette->impl_->exact = keep_exact;
  if (keep_exact) {
    // Index the original colors, then display the snapped ones.
    std::vector<RGB> snapped_colors = out;
    out = std::move(original);
    palette->impl_->Finalize();
    out = std::move(snapped_colors);
  } else {
    palette->impl_->Finalize();
  }
  return palette;
}

// static
std::shared_ptr<const SixelPalette> SixelPalette::For(const Bitmap& bitmap,
                                                      int colors,
                                                      Color background) {
  colors = std::clamp(colors, g_min_colors, g_max_colors);
  if (bitmap.empty()) {
    return Build(bitmap, colors, background);
  }

  // The background only matters when there is something to blend.
  const Blend blend(background);
  auto key = static_cast<uint64_t>(colors);
  if (blend.enabled && !bitmap.opaque()) {
    key |= uint64_t{1} << 16;
    key |= static_cast<uint64_t>(blend.red) << 24;
    key |= static_cast<uint64_t>(blend.green) << 32;
    key |= static_cast<uint64_t>(blend.blue) << 40;
  }

  Bitmap::Storage& storage = *bitmap.storage_;
  {
    const std::lock_guard<std::mutex> lock(storage.palettes_mutex);
    auto it = storage.palettes.find(key);
    if (it != storage.palettes.end()) {
      return it->second;
    }
  }
  // Computed without holding the lock. At worst, two threads compute the same
  // palette, and the first one to finish wins.
  auto palette = Build(bitmap, colors, background);
  const std::lock_guard<std::mutex> lock(storage.palettes_mutex);
  auto [it, inserted] = storage.palettes.emplace(key, palette);
  return it->second;
}

int SixelPalette::size() const {
  return static_cast<int>(impl_->colors.size());
}

SixelPalette::RGB SixelPalette::color(int index) const {
  return impl_->colors[static_cast<size_t>(index)];
}

uint64_t SixelPalette::id() const {
  return impl_->id;
}

int SixelPalette::Nearest(int red, int green, int blue) const {
  return impl_->Nearest(std::clamp(red, 0, 255), std::clamp(green, 0, 255),
                        std::clamp(blue, 0, 255));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::string EncodeSixel(const SixelImage& image) {
  const Bitmap& bitmap = image.bitmap;
  if (bitmap.empty()) {
    return "";
  }
  const int width = image.width > 0 ? image.width : bitmap.width();
  const int height = image.height > 0 ? image.height : bitmap.height();
  if (width <= 0 || height <= 0) {
    return "";
  }

  const std::shared_ptr<const SixelPalette> palette =
      image.palette ? image.palette
                    : SixelPalette::For(bitmap, image.colors, image.background);
  const Blend blend(image.background);
  const int palette_size = palette->size();

  // The part of the canvas covered by the bitmap.
  const int x_begin = std::max(0, image.offset_x);
  const int y_begin = std::max(0, image.offset_y);
  const int x_end = std::min(width, image.offset_x + bitmap.width());
  const int y_end = std::min(height, image.offset_y + bitmap.height());
  if (x_begin >= x_end || y_begin >= y_end) {
    return "";
  }

  // ---------------------------------------------------------------------------
  // Step 1: Turn the pixels into indices of the palette. -1 is transparent.
  // ---------------------------------------------------------------------------
  const auto canvas_width = static_cast<size_t>(width);
  std::vector<int16_t> indices(canvas_width * static_cast<size_t>(height), -1);

  const bool has_mask = !image.mask.empty() && image.mask_columns > 0 &&
                        image.mask_cell_width > 0 && image.mask_cell_height > 0;
  const int mask_rows =
      has_mask ? static_cast<int>(image.mask.size()) / image.mask_columns : 0;
  auto masked = [&](int x, int y) {
    const int column = x / image.mask_cell_width;
    const int row = y / image.mask_cell_height;
    if (column >= image.mask_columns || row >= mask_rows) {
      return true;
    }
    return image.mask[static_cast<size_t>(row) *
                          static_cast<size_t>(image.mask_columns) +
                      static_cast<size_t>(column)] == 0;
  };

  const bool diffuse = (image.dither == SixelDither::FloydSteinberg);
  const bool ordered = (image.dither == SixelDither::Ordered);
  // The amplitude of the ordered dithering pattern: about the distance in
  // between two colors of the palette.
  const int ordered_amplitude =
      static_cast<int>(192.0 / std::cbrt(double(palette_size)));

  // The error of the current and of the next row, for each channel. They are
  // expressed in 1/16 of a color level.
  const size_t span = static_cast<size_t>(x_end - x_begin) + 2;
  std::vector<int> error_current(span * 3, 0);
  std::vector<int> error_next(span * 3, 0);

  std::array<bool, g_max_colors> used = {};
  bool any_pixel = false;

  const uint8_t* data = bitmap.data();
  const auto bitmap_width = static_cast<size_t>(bitmap.width());
  for (int y = y_begin; y < y_end; ++y) {
    const uint8_t* row =
        data + static_cast<size_t>(y - image.offset_y) * bitmap_width * 4;
    int16_t* out_row = indices.data() + static_cast<size_t>(y) * canvas_width;
    std::fill(error_next.begin(), error_next.end(), 0);

    for (int x = x_begin; x < x_end; ++x) {
      const uint8_t* pixel =
          row + static_cast<size_t>(x - image.offset_x) * 4;  // NOLINT
      const size_t e = (static_cast<size_t>(x - x_begin) + 1) * 3;

      int red = pixel[0];                           // NOLINT
      int green = pixel[1];                         // NOLINT
      int blue = pixel[2];                          // NOLINT
      if (!blend(pixel[3], &red, &green, &blue)) {  // NOLINT
        continue;
      }

      if (diffuse) {
        red = std::clamp(red + error_current[e + 0] / 16, 0, 255);
        green = std::clamp(green + error_current[e + 1] / 16, 0, 255);
        blue = std::clamp(blue + error_current[e + 2] / 16, 0, 255);
      } else if (ordered) {
        const int threshold = (int(g_bayer[static_cast<size_t>(y & 7) * 8 +
                                           static_cast<size_t>(x & 7)]) -
                               32) *
                              ordered_amplitude / 64;
        red = std::clamp(red + threshold, 0, 255);
        green = std::clamp(green + threshold, 0, 255);
        blue = std::clamp(blue + threshold, 0, 255);
      }

      const int index = palette->Nearest(red, green, blue);

      if (diffuse) {
        const SixelPalette::RGB color = palette->color(index);
        const std::array<int, 3> error = {
            red - color.red,
            green - color.green,
            blue - color.blue,
        };
        for (size_t c = 0; c < 3; ++c) {
          error_current[e + 3 + c] += error[c] * 7;
          error_next[e - 3 + c] += error[c] * 3;
          error_next[e + c] += error[c] * 5;
          error_next[e + 3 + c] += error[c] * 1;
        }
      }

      // The dithering is computed as if there was no mask, so that punching a
      // hole doesn't change the look of what remains around.
      if (has_mask && masked(x, y)) {
        continue;
      }
      out_row[x] = static_cast<int16_t>(index);  // NOLINT
      used[static_cast<size_t>(index)] = true;
      any_pixel = true;
    }
    std::swap(error_current, error_next);
  }

  if (!any_pixel) {
    return "";
  }

  // ---------------------------------------------------------------------------
  // Step 2: The header.
  // ---------------------------------------------------------------------------
  std::string out;
  out.reserve(canvas_width * static_cast<size_t>(height) / 2 + 4096);

  // DCS P1 ; P2 ; P3 q
  // - P1 = 0: the pixel aspect ratio is given by the raster attributes.
  // - P2 = 1: the pixels which are not drawn are left untouched.
  // - P3 = 0: default horizontal grid size.
  out += "\x1BP0;1;0q";
  // Raster attributes: 1:1 pixels, and the size of the image.
  out += "\"1;1;";
  AppendInt(out, width);
  out += ';';
  AppendInt(out, height);

  // The colors, limited to the ones in use.
  std::array<int, g_max_colors> color_register = {};
  int register_count = 0;
  for (int i = 0; i < palette_size; ++i) {
    if (!used[static_cast<size_t>(i)]) {
      continue;
    }
    color_register[static_cast<size_t>(i)] = register_count;
    const SixelPalette::RGB color = palette->color(i);
    out += '#';
    AppendInt(out, register_count);
    out += ";2;";
    AppendInt(out, ToPercent(color.red));
    out += ';';
    AppendInt(out, ToPercent(color.green));
    out += ';';
    AppendInt(out, ToPercent(color.blue));
    register_count++;
  }

  // ---------------------------------------------------------------------------
  // Step 3: The pixels. They are sent by bands of 6 rows. For each band, and
  // for each color in the band, a row of characters encodes which of the 6
  // pixels of each column have this color.
  // ---------------------------------------------------------------------------
  // The pixels of one color in a band: for each column having some, which of
  // its 6 pixels. A column is encoded as (x << 8) | bits. They are ordered.
  struct Plane {
    std::vector<uint32_t> columns;
    int color = 0;
  };
  std::vector<Plane> planes;
  std::array<int, g_max_colors> plane_of_color = {};
  plane_of_color.fill(-1);

  auto emit = [&](int count, char character) {
    if (count > 3) {
      out += '!';
      AppendInt(out, count);
      out += character;
      return;
    }
    out.append(static_cast<size_t>(count), character);
  };

  const int bands = (height + 5) / 6;
  for (int band = 0; band < bands; ++band) {
    const int band_y = band * 6;
    const int band_height = std::min(6, height - band_y);
    size_t plane_count = 0;

    // Distribute the pixels of the band to the planes, column by column.
    const int16_t* band_begin =
        indices.data() + static_cast<size_t>(band_y) * canvas_width;
    for (int x = x_begin; x < x_end; ++x) {
      const int16_t* pixel = band_begin + x;  // NOLINT
      for (int dy = 0; dy < band_height;
           ++dy, pixel += canvas_width) {  // NOLINT
        const int16_t index = *pixel;
        if (index < 0) {
          continue;
        }
        int& plane_index = plane_of_color[static_cast<size_t>(index)];
        if (plane_index < 0) {
          plane_index = static_cast<int>(plane_count++);
          if (planes.size() < plane_count) {
            planes.emplace_back();
          }
          Plane& plane = planes[static_cast<size_t>(plane_index)];
          plane.color = index;
          plane.columns.clear();
        }
        Plane& plane = planes[static_cast<size_t>(plane_index)];
        const uint32_t column = static_cast<uint32_t>(x) << 8;
        const uint32_t bit = uint32_t{1} << dy;
        if (!plane.columns.empty() &&
            (plane.columns.back() >> 8) == static_cast<uint32_t>(x)) {
          plane.columns.back() |= bit;
        } else {
          plane.columns.push_back(column | bit);
        }
      }
    }

    // Send the planes. Consecutive identical columns are run-length encoded.
    for (size_t i = 0; i < plane_count; ++i) {
      const Plane& plane = planes[i];
      if (i != 0) {
        out += '$';  // Graphics carriage return: back to the left.
      }
      out += '#';
      AppendInt(out, color_register[static_cast<size_t>(plane.color)]);

      int next_x = 0;  // The column the next character is drawn at.
      int run = 0;
      uint32_t run_bits = 0;
      for (const uint32_t column : plane.columns) {
        const int x = static_cast<int>(column >> 8);
        const uint32_t bits = column & 0xFF;
        if (x == next_x + run && bits == run_bits) {
          run++;
          continue;
        }
        // Flush the run, then skip the columns without any pixel.
        if (run != 0) {
          emit(run, static_cast<char>('?' + run_bits));
          next_x += run;
        }
        if (x > next_x) {
          emit(x - next_x, '?');
          next_x = x;
        }
        run = 1;
        run_bits = bits;
      }
      if (run != 0) {
        emit(run, static_cast<char>('?' + run_bits));
      }

      plane_of_color[static_cast<size_t>(plane.color)] = -1;
    }

    // Graphics new line: next band. There is none after the last one, so that
    // a terminal doesn't scroll when the image ends on its last row.
    if (band + 1 != bands) {
      out += '-';
    }
  }

  // String terminator.
  out += "\x1B\\";
  return out;
}

std::string EncodeSixel(const Bitmap& bitmap, int colors, SixelDither dither) {
  SixelImage image;
  image.bitmap = bitmap;
  image.colors = colors;
  image.dither = dither;
  return EncodeSixel(image);
}

}  // namespace ftxui
