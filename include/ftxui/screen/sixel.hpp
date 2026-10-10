// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_SIXEL_HPP
#define FTXUI_SCREEN_SIXEL_HPP

#include <cstddef>  // for size_t
#include <cstdint>  // for uint8_t, uint64_t
#include <memory>   // for shared_ptr, unique_ptr
#include <string>   // for string
#include <vector>   // for vector

#include "ftxui/screen/bitmap.hpp"  // for Bitmap
#include "ftxui/screen/color.hpp"   // for Color
#include "ftxui/util/export.hpp"    // for FTXUI_EXPORT

namespace ftxui {

/// @brief How the colors of an image are approximated with the limited
/// palette of a sixel image.
/// @ingroup screen
enum class SixelDither : uint8_t {
  /// Use the nearest color. Smallest output. Gradients show bands.
  None,
  /// A fixed 8x8 threshold pattern. Stable from one frame to the next.
  Ordered,
  /// Floyd-Steinberg error diffusion. Best looking, largest output.
  FloydSteinberg,
};

/// @brief The colors a sixel image is made of.
///
/// Sixel is a palette based format: a terminal provides a few color
/// registers, typically 256. The palette is computed from the pixels of a
/// Bitmap, and is shared by every scaled version of it, so that an image keeps
/// the same colors while it is resized.
///
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) SixelPalette {
 public:
  struct RGB {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  /// @brief Compute a palette of at most `colors` colors for a bitmap.
  /// @param bitmap The bitmap to take the colors from.
  /// @param colors The maximal number of colors, in [2, 256].
  /// @param background The color partially transparent pixels are blended
  /// with. When it is transparent itself, which is the default, they are not
  /// blended: the pixels which are mostly transparent are ignored.
  static std::shared_ptr<const SixelPalette>
  Build(const Bitmap& bitmap, int colors, Color background = Color::Default);

  /// @brief Same as Build(), but the result is computed once per bitmap, and
  /// then remembered alongside its pixels.
  static std::shared_ptr<const SixelPalette>
  For(const Bitmap& bitmap, int colors, Color background = Color::Default);

  ~SixelPalette();
  SixelPalette(const SixelPalette&) = delete;
  SixelPalette& operator=(const SixelPalette&) = delete;

  /// @brief The number of colors.
  int size() const;
  /// @brief The color at `index`, in [0, size()).
  RGB color(int index) const;
  /// @brief The index of the color the closest to the given one.
  int Nearest(int red, int green, int blue) const;
  /// @brief A number identifying this palette.
  uint64_t id() const;

 private:
  SixelPalette();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// @brief What EncodeSixel() draws.
///
/// The output is a "canvas" of `width` x `height` pixels. The bitmap is
/// placed on it at (`offset_x`, `offset_y`). Whatever the bitmap doesn't
/// cover is transparent: the terminal keeps what was displayed there.
///
/// @ingroup screen
struct FTXUI_EXPORT(SCREEN) SixelImage {
  Bitmap bitmap;

  /// The size of the canvas, in pixels. Zero means the size of the bitmap.
  int width = 0;
  int height = 0;

  /// The position of the top-left pixel of the bitmap on the canvas. It can
  /// be negative, in which case the bitmap is cropped.
  int offset_x = 0;
  int offset_y = 0;

  /// The palette. When null, one is computed from the bitmap.
  std::shared_ptr<const SixelPalette> palette;
  /// The maximal number of colors of the computed palette, in [2, 256].
  int colors = 256;
  SixelDither dither = SixelDither::FloydSteinberg;

  /// The color partially transparent pixels are blended with. When it is
  /// transparent itself, which is the default, a pixel is either fully drawn
  /// or not drawn at all, depending on which one its alpha is the closest to.
  Color background = Color::Default;

  /// An optional mask, to punch holes in the canvas. The canvas is divided
  /// into cells of `mask_cell_width` x `mask_cell_height` pixels. The cell
  /// (x,y) is drawn when `mask[y * mask_columns + x]` is non zero, and left
  /// transparent otherwise. An empty mask draws everything.
  std::vector<uint8_t> mask;
  int mask_columns = 0;
  int mask_cell_width = 0;
  int mask_cell_height = 0;
};

/// @brief Encode an image as a sixel escape sequence: a Device Control String
/// that draws the image at the cursor position of a terminal supporting
/// sixel.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) std::string EncodeSixel(const SixelImage& image);

/// @brief Encode a bitmap as a sixel escape sequence.
/// @ingroup screen
FTXUI_EXPORT(SCREEN)
std::string EncodeSixel(const Bitmap& bitmap,
                        int colors = 256,
                        SixelDither dither = SixelDither::FloydSteinberg);

/// @brief Set how much memory is used by ftxui::Screen to remember the
/// graphics it has encoded. The default is 64 MiB.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) void SetSixelCacheSize(size_t max_bytes);

}  // namespace ftxui

#endif  // FTXUI_SCREEN_SIXEL_HPP
