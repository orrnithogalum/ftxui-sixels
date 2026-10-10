// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_BITMAP_HPP
#define FTXUI_SCREEN_BITMAP_HPP

#include <cstddef>  // for size_t
#include <cstdint>  // for uint8_t, uint64_t
#include <memory>   // for shared_ptr
#include <string>   // for string

#include "ftxui/util/export.hpp"  // for FTXUI_EXPORT

namespace ftxui {

class SixelPalette;

/// @brief How the pixels of a Bitmap are resampled when it is scaled.
/// @ingroup screen
enum class BitmapFilter : uint8_t {
  /// Area averaging when scaling down, bilinear interpolation when scaling up.
  Smooth,
  /// Nearest neighbor. Keeps hard edges. This is what you want for pixel art.
  Nearest,
};

/// @brief A raster image: a rectangular grid of RGBA pixels.
///
/// A Bitmap is a cheap handle. Copying it shares the pixels. The pixels are
/// 8 bits per channel, in RGBA order, row by row, alpha not premultiplied.
///
/// It is the pixel source of the ftxui::image element, and what is drawn on a
/// ftxui::Screen via Screen::DrawGraphic.
///
/// @note Not to be confused with ftxui::Image, the deprecated name of
/// ftxui::Surface, which is a grid of text cells.
///
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) Bitmap {
 public:
  /// @brief An empty bitmap.
  Bitmap();

  /// @brief A fully transparent bitmap of the given size.
  Bitmap(int width, int height);

  /// @brief Copy `width * height * 4` bytes of RGBA pixels.
  static Bitmap FromRGBA(int width, int height, const uint8_t* rgba);

  /// @brief Copy `width * height * 3` bytes of RGB pixels.
  static Bitmap FromRGB(int width, int height, const uint8_t* rgb);

  /// @brief Decode an image file: PNG, JPEG, GIF (first frame), BMP, TGA, PSD
  /// or PNM.
  /// @param path The file to read.
  /// @param error If non-null, receives the reason of a failure.
  /// @return The decoded bitmap. It is empty() on failure.
  static Bitmap Load(const std::string& path, std::string* error = nullptr);

  /// @brief Decode an image file held in memory.
  /// @see Bitmap::Load
  static Bitmap LoadFromMemory(const uint8_t* data,
                               size_t size,
                               std::string* error = nullptr);

  bool empty() const;
  int width() const;
  int height() const;

  /// @brief Whether every pixel is fully opaque.
  bool opaque() const;

  /// @brief The pixels: `4 * width() * height()` bytes. Null when empty().
  const uint8_t* data() const;

  /// @brief Write access to the pixels.
  ///
  /// The pixels are detached from every other copy of this Bitmap, and the
  /// id() changes, so that anything cached about the previous content is
  /// dropped. Call it once per modification, not once per pixel.
  uint8_t* MutableData();

  /// @brief A number identifying the content. Two bitmaps sharing an id hold
  /// the same pixels. This is what the caches are keyed with.
  uint64_t id() const;

  /// @brief Scale the whole bitmap to a new size.
  Bitmap Resized(int width,
                 int height,
                 BitmapFilter filter = BitmapFilter::Smooth) const;

  /// @brief Scale a sub-rectangle of the bitmap to a new size.
  ///
  /// The source rectangle is expressed in pixels, and may have fractional
  /// coordinates. It supports scaling both down and up.
  Bitmap Resampled(double source_x,
                   double source_y,
                   double source_width,
                   double source_height,
                   int width,
                   int height,
                   BitmapFilter filter = BitmapFilter::Smooth) const;

 private:
  friend class SixelPalette;
  struct Storage;
  std::shared_ptr<Storage> storage_;
};

}  // namespace ftxui

#endif  // FTXUI_SCREEN_BITMAP_HPP
