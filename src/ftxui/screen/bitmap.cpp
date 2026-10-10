// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include "ftxui/screen/bitmap.hpp"

#include <algorithm>  // for min, max, clamp
#include <atomic>     // for atomic
#include <cmath>      // for floor, ceil
#include <cstring>    // for memcpy
#include <fstream>    // for ifstream
#include <iterator>   // for istreambuf_iterator
#include <limits>     // for numeric_limits
#include <utility>    // for move
#include <vector>     // for vector

#include "ftxui/screen/bitmap_internal.hpp"

// stb_image is vendored, and compiled only here. Its symbols are made static,
// so that an application embedding its own copy of stb_image doesn't clash
// with this one.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wdocumentation"
#pragma GCC diagnostic ignored "-Wcomma"
#endif
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "ftxui/screen/stb_image.h"  // NOLINT
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace ftxui {

namespace {

// The largest image accepted, in pixels. It bounds the memory a malformed or
// hostile file can make us allocate.
constexpr int64_t g_max_pixels = int64_t{1} << 28;     // 268 megapixels, ~1GiB.
constexpr int64_t g_max_file_size = int64_t{1} << 29;  // 512 MiB.

uint64_t NextId() {
  static std::atomic<uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

bool ValidSize(int width, int height) {
  return width > 0 && height > 0 &&
         int64_t{width} * int64_t{height} <= g_max_pixels;
}

bool ComputeOpaque(const std::vector<uint8_t>& pixels) {
  for (size_t i = 3; i < pixels.size(); i += 4) {
    if (pixels[i] != 255) {  // NOLINT
      return false;
    }
  }
  return true;
}

// The contribution of the source pixels to each destination pixel, along one
// axis. The destination pixel `i` is the weighted sum of `count[i]` source
// pixels, starting at `first[i]`. Their weights sum to one.
struct Axis {
  std::vector<int> first;
  std::vector<int> count;
  std::vector<int> offset;  // Into `weights`.
  std::vector<float> weights;
};

Axis BuildAxis(double source_begin,
               double source_length,
               int source_size,
               int size,
               BitmapFilter filter) {
  Axis axis;
  axis.first.resize(static_cast<size_t>(size));
  axis.count.resize(static_cast<size_t>(size));
  axis.offset.resize(static_cast<size_t>(size));
  const double step = source_length / size;
  const int last = source_size - 1;

  for (int i = 0; i < size; ++i) {
    const auto index = static_cast<size_t>(i);
    axis.offset[index] = static_cast<int>(axis.weights.size());

    if (filter == BitmapFilter::Nearest) {
      const double center = source_begin + (i + 0.5) * step;
      axis.first[index] =
          std::clamp(static_cast<int>(std::floor(center)), 0, last);
      axis.count[index] = 1;
      axis.weights.push_back(1.F);
      continue;
    }

    // Scaling up: bilinear interpolation between the two nearest pixels.
    if (step <= 1.0) {
      const double center = source_begin + (i + 0.5) * step - 0.5;
      const double floor = std::floor(center);
      const auto t = static_cast<float>(center - floor);
      const int a = std::clamp(static_cast<int>(floor), 0, last);
      const int b = std::clamp(static_cast<int>(floor) + 1, 0, last);
      axis.first[index] = a;
      if (a == b) {
        axis.count[index] = 1;
        axis.weights.push_back(1.F);
      } else {
        axis.count[index] = 2;
        axis.weights.push_back(1.F - t);
        axis.weights.push_back(t);
      }
      continue;
    }

    // Scaling down: average the source pixels covered by the destination
    // pixel, each weighted by how much of it is covered.
    const double begin =
        std::clamp(source_begin + i * step, 0.0, double(source_size));
    const double end =
        std::clamp(source_begin + (i + 1) * step, 0.0, double(source_size));
    const int a = std::clamp(static_cast<int>(std::floor(begin)), 0, last);
    const int b = std::clamp(static_cast<int>(std::ceil(end)) - 1, a, last);
    double total = 0.0;
    for (int p = a; p <= b; ++p) {
      total +=
          std::max(0.0, std::min(end, p + 1.0) - std::max(begin, double(p)));
    }
    if (total <= 0.0) {
      // The destination pixel lies outside of the source. Repeat the edge.
      axis.first[index] = a;
      axis.count[index] = 1;
      axis.weights.push_back(1.F);
      continue;
    }
    axis.first[index] = a;
    axis.count[index] = b - a + 1;
    for (int p = a; p <= b; ++p) {
      const double w =
          std::max(0.0, std::min(end, p + 1.0) - std::max(begin, double(p)));
      axis.weights.push_back(static_cast<float>(w / total));
    }
  }
  return axis;
}

}  // namespace

Bitmap::Bitmap() = default;

Bitmap::Bitmap(int width, int height) {
  if (!ValidSize(width, height)) {
    return;
  }
  storage_ = std::make_shared<Storage>();
  storage_->width = width;
  storage_->height = height;
  storage_->id = NextId();
  storage_->opaque = false;
  storage_->pixels.assign(
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
}

// static
Bitmap Bitmap::FromRGBA(int width, int height, const uint8_t* rgba) {
  Bitmap bitmap;
  if (!ValidSize(width, height) || rgba == nullptr) {
    return bitmap;
  }
  bitmap.storage_ = std::make_shared<Storage>();
  bitmap.storage_->width = width;
  bitmap.storage_->height = height;
  bitmap.storage_->id = NextId();
  const size_t size =
      static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  bitmap.storage_->pixels.assign(rgba, rgba + size);  // NOLINT
  bitmap.storage_->opaque = ComputeOpaque(bitmap.storage_->pixels);
  return bitmap;
}

// static
Bitmap Bitmap::FromRGB(int width, int height, const uint8_t* rgb) {
  Bitmap bitmap;
  if (!ValidSize(width, height) || rgb == nullptr) {
    return bitmap;
  }
  bitmap.storage_ = std::make_shared<Storage>();
  bitmap.storage_->width = width;
  bitmap.storage_->height = height;
  bitmap.storage_->id = NextId();
  bitmap.storage_->opaque = true;
  const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
  auto& pixels = bitmap.storage_->pixels;
  pixels.resize(count * 4);
  for (size_t i = 0; i < count; ++i) {
    pixels[i * 4 + 0] = rgb[i * 3 + 0];  // NOLINT
    pixels[i * 4 + 1] = rgb[i * 3 + 1];  // NOLINT
    pixels[i * 4 + 2] = rgb[i * 3 + 2];  // NOLINT
    pixels[i * 4 + 3] = 255;             // NOLINT
  }
  return bitmap;
}

// static
Bitmap Bitmap::Load(const std::string& path, std::string* error) {
  auto fail = [&](const char* message) {
    if (error) {
      *error = message;
    }
    return Bitmap();
  };

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return fail("cannot open the file");
  }
  const std::streamoff size = file.tellg();
  if (size <= 0) {
    return fail("the file is empty");
  }
  if (size > g_max_file_size) {
    return fail("the file is too large");
  }
  std::vector<uint8_t> content(static_cast<size_t>(size));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(content.data()),  // NOLINT
            static_cast<std::streamsize>(size));
  if (!file) {
    return fail("cannot read the file");
  }
  return LoadFromMemory(content.data(), content.size(), error);
}

// static
Bitmap Bitmap::LoadFromMemory(const uint8_t* data,
                              size_t size,
                              std::string* error) {
  auto fail = [&](const char* message) {
    if (error) {
      *error = message ? message : "cannot decode the image";
    }
    return Bitmap();
  };

  if (data == nullptr || size == 0 ||
      size > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return fail("invalid image data");
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  if (!stbi_info_from_memory(data, static_cast<int>(size), &width, &height,
                             &channels)) {
    return fail(stbi_failure_reason());
  }
  if (!ValidSize(width, height)) {
    return fail("the image is too large");
  }

  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width,
                                          &height, &channels, 4);
  if (pixels == nullptr) {
    return fail(stbi_failure_reason());
  }
  Bitmap bitmap = FromRGBA(width, height, pixels);
  stbi_image_free(pixels);
  if (bitmap.empty()) {
    return fail("the image is too large");
  }
  return bitmap;
}

bool Bitmap::empty() const {
  return !storage_;
}

int Bitmap::width() const {
  return storage_ ? storage_->width : 0;
}

int Bitmap::height() const {
  return storage_ ? storage_->height : 0;
}

bool Bitmap::opaque() const {
  return storage_ ? storage_->opaque : true;
}

const uint8_t* Bitmap::data() const {
  return storage_ ? storage_->pixels.data() : nullptr;
}

uint8_t* Bitmap::MutableData() {
  if (!storage_) {
    return nullptr;
  }
  // The content is about to change: give it a new identity, and leave the
  // other handles, as well as the caches referring to the old one, untouched.
  auto storage = std::make_shared<Storage>();
  storage->width = storage_->width;
  storage->height = storage_->height;
  storage->id = NextId();
  // The caller may write any alpha value. Assume the worst.
  storage->opaque = false;
  if (storage_.use_count() == 1) {
    storage->pixels = std::move(storage_->pixels);
  } else {
    storage->pixels = storage_->pixels;
  }
  storage_ = std::move(storage);
  return storage_->pixels.data();
}

uint64_t Bitmap::id() const {
  return storage_ ? storage_->id : 0;
}

Bitmap Bitmap::Resized(int width, int height, BitmapFilter filter) const {
  return Resampled(0.0, 0.0, this->width(), this->height(), width, height,
                   filter);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
Bitmap Bitmap::Resampled(double source_x,
                         double source_y,
                         double source_width,
                         double source_height,
                         int width,
                         int height,
                         BitmapFilter filter) const {
  if (empty() || !ValidSize(width, height) || !(source_width > 0.0) ||
      !(source_height > 0.0)) {
    return {};
  }

  const int sw = storage_->width;
  const int sh = storage_->height;
  const uint8_t* source = storage_->pixels.data();

  // Nothing to resample: share the pixels.
  if (source_x == 0.0 && source_y == 0.0 && source_width == sw &&
      source_height == sh && width == sw && height == sh) {
    return *this;
  }

  const Axis ax = BuildAxis(source_x, source_width, sw, width, filter);
  const Axis ay = BuildAxis(source_y, source_height, sh, height, filter);

  Bitmap out;
  out.storage_ = std::make_shared<Storage>();
  out.storage_->width = width;
  out.storage_->height = height;
  out.storage_->id = NextId();
  out.storage_->pixels.resize(static_cast<size_t>(width) *
                              static_cast<size_t>(height) * 4);
  uint8_t* destination = out.storage_->pixels.data();

  // Nearest neighbor: a plain lookup.
  if (filter == BitmapFilter::Nearest) {
    for (int y = 0; y < height; ++y) {
      const uint8_t* row =
          source + static_cast<size_t>(ay.first[static_cast<size_t>(y)]) *
                       static_cast<size_t>(sw) * 4;
      uint8_t* out_row =
          destination + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
      for (int x = 0; x < width; ++x) {
        std::memcpy(
            out_row + static_cast<size_t>(x) * 4,
            row + static_cast<size_t>(ax.first[static_cast<size_t>(x)]) * 4, 4);
      }
    }
    out.storage_->opaque = storage_->opaque;
    return out;
  }

  // Only the source rows contributing to the output are resampled
  // horizontally.
  const int row_begin = ay.first.front();
  const int row_end = ay.first.back() + ay.count.back();  // Exclusive.
  const int rows = row_end - row_begin;

  // Step 1: horizontal pass, into premultiplied floats.
  const bool opaque = storage_->opaque;
  std::vector<float> horizontal(static_cast<size_t>(rows) *
                                static_cast<size_t>(width) * 4);
  for (int y = 0; y < rows; ++y) {
    const uint8_t* row = source + static_cast<size_t>(row_begin + y) *
                                      static_cast<size_t>(sw) * 4;
    float* out_row = horizontal.data() +
                     static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
    for (int x = 0; x < width; ++x) {
      const auto ix = static_cast<size_t>(x);
      const float* weights =
          ax.weights.data() + static_cast<size_t>(ax.offset[ix]);
      const uint8_t* pixel = row + static_cast<size_t>(ax.first[ix]) * 4;
      float r = 0.F;
      float g = 0.F;
      float b = 0.F;
      float a = 0.F;
      const int count = ax.count[ix];
      if (opaque) {
        for (int i = 0; i < count; ++i) {
          const float w = weights[i];  // NOLINT
          r += w * float(pixel[0]);    // NOLINT
          g += w * float(pixel[1]);    // NOLINT
          b += w * float(pixel[2]);    // NOLINT
          pixel += 4;                  // NOLINT
        }
        a = 255.F;
      } else {
        for (int i = 0; i < count; ++i) {
          const float alpha = float(pixel[3]);                 // NOLINT
          const float w = weights[i] * alpha * (1.F / 255.F);  // NOLINT
          r += w * float(pixel[0]);                            // NOLINT
          g += w * float(pixel[1]);                            // NOLINT
          b += w * float(pixel[2]);                            // NOLINT
          a += weights[i] * alpha;                             // NOLINT
          pixel += 4;                                          // NOLINT
        }
      }
      out_row[ix * 4 + 0] = r;  // NOLINT
      out_row[ix * 4 + 1] = g;  // NOLINT
      out_row[ix * 4 + 2] = b;  // NOLINT
      out_row[ix * 4 + 3] = a;  // NOLINT
    }
  }

  // Step 2: vertical pass, back to 8 bits, alpha not premultiplied.
  const size_t stride = static_cast<size_t>(width) * 4;
  std::vector<float> accumulator(stride);
  bool out_opaque = true;
  for (int y = 0; y < height; ++y) {
    const auto iy = static_cast<size_t>(y);
    std::fill(accumulator.begin(), accumulator.end(), 0.F);
    const float* weights =
        ay.weights.data() + static_cast<size_t>(ay.offset[iy]);
    for (int i = 0; i < ay.count[iy]; ++i) {
      const float w = weights[i];  // NOLINT
      const float* row =
          horizontal.data() +
          static_cast<size_t>(ay.first[iy] + i - row_begin) * stride;
      for (size_t k = 0; k < stride; ++k) {
        accumulator[k] += w * row[k];  // NOLINT
      }
    }

    uint8_t* out_row = destination + iy * stride;
    auto to_byte = [](float v) {
      return static_cast<uint8_t>(std::clamp(v + 0.5F, 0.F, 255.F));
    };
    for (int x = 0; x < width; ++x) {
      const size_t k = static_cast<size_t>(x) * 4;
      const float a = accumulator[k + 3];
      if (opaque) {
        out_row[k + 0] = to_byte(accumulator[k + 0]);  // NOLINT
        out_row[k + 1] = to_byte(accumulator[k + 1]);  // NOLINT
        out_row[k + 2] = to_byte(accumulator[k + 2]);  // NOLINT
        out_row[k + 3] = 255;                          // NOLINT
        continue;
      }
      if (a <= 0.5F) {
        out_row[k + 0] = 0;  // NOLINT
        out_row[k + 1] = 0;  // NOLINT
        out_row[k + 2] = 0;  // NOLINT
        out_row[k + 3] = 0;  // NOLINT
        out_opaque = false;
        continue;
      }
      const float unpremultiply = 255.F / a;
      out_row[k + 0] = to_byte(accumulator[k + 0] * unpremultiply);  // NOLINT
      out_row[k + 1] = to_byte(accumulator[k + 1] * unpremultiply);  // NOLINT
      out_row[k + 2] = to_byte(accumulator[k + 2] * unpremultiply);  // NOLINT
      out_row[k + 3] = to_byte(a);                                   // NOLINT
      out_opaque &= (out_row[k + 3] == 255);                         // NOLINT
    }
  }
  out.storage_->opaque = out_opaque;
  return out;
}

}  // namespace ftxui
