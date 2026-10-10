// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_SCREEN_HPP
#define FTXUI_SCREEN_SCREEN_HPP

#include <cstdint>      // for uint8_t
#include <functional>   // for function
#include <memory>       // for shared_ptr
#include <string>       // for string, basic_string, allocator
#include <string_view>  // for string_view
#include <vector>      // for vector

#include "ftxui/screen/bitmap.hpp"    // for Bitmap
#include "ftxui/screen/sixel.hpp"     // for SixelPalette, SixelDither
#include "ftxui/screen/surface.hpp"   // for Surface
#include "ftxui/screen/terminal.hpp"  // for Dimensions
#include "ftxui/util/export.hpp"      // for FTXUI_EXPORT

namespace ftxui {

/// @brief Define how the Screen's dimensions should look like.
/// @ingroup screen
namespace Dimension {
FTXUI_EXPORT(SCREEN) Dimensions Fixed(int);
FTXUI_EXPORT(SCREEN) Dimensions Full();
}  // namespace Dimension

/// @brief A rectangular grid of Cell.
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) Screen : public Surface {
 public:
  // Constructors:
  Screen(int dimx, int dimy);
  static Screen Create(Dimensions dimension);
  static Screen Create(Dimensions width, Dimensions height);

  // Destructor:
  ~Screen() override = default;

  // Copy:
  Screen(const Screen&) = default;
  Screen& operator=(const Screen&) = default;

  std::string ToString() const;
  void ToString(std::string& ss) const;

  // Print the Screen on to the terminal.
  void Print() const;

  // Fill the screen with space and reset any screen state, like hyperlinks, and
  // cursor
  void Clear();

  // Move the terminal cursor n-lines up with n = dimy().
  std::string ResetPosition(bool clear = false) const;
  void ResetPosition(std::string& ss, bool clear = false) const;

  void ApplyShader();

  struct Cursor {
    int x = 0;
    int y = 0;

    enum Shape : uint8_t {
      Hidden = 0,
      BlockBlinking = 1,
      Block = 2,
      UnderlineBlinking = 3,
      Underline = 4,
      BarBlinking = 5,
      Bar = 6,
    };
    Shape shape = Hidden;
  };

  Cursor cursor() const { return cursor_; }
  void SetCursor(Cursor cursor) { cursor_ = cursor; }

  // ABI Reserve:
  void Reserved1() override;
  void Reserved2() override;
  void Reserved3() override;
  void Reserved4() override;
  void Reserved5() override;
  void Reserved6() override;
  void Reserved7() override;
  void Reserved8() override;

  // Store an hyperlink in the screen. Return the id of the hyperlink. The id is
  // used to identify the hyperlink when the user click on it.
  uint8_t RegisterHyperlink(std::string_view link);
  const std::string& Hyperlink(uint8_t id) const;

  using SelectionStyle = std::function<void(Cell&)>;
  const SelectionStyle& GetSelectionStyle() const;
  void SetSelectionStyle(SelectionStyle decorator);

  // --- Graphics -------------------------------------------------------------

  /// @brief A raster image displayed over a rectangle of cells, using the
  /// sixel protocol.
  ///
  /// The rectangle of cells defines a canvas, whose size in pixels is
  /// (`width` * `cell_width`, `height` * `cell_height`). The `bitmap` is
  /// displayed on it at (`offset_x`, `offset_y`), without being scaled. What it
  /// doesn't cover is transparent.
  struct Graphic {
    /// The top-left cell. It may lie outside of the screen.
    int x = 0;
    int y = 0;
    /// The size, in cells.
    int width = 0;
    int height = 0;
    /// The size of one cell, in pixels. See Terminal::CellPixelSize().
    int cell_width = 0;
    int cell_height = 0;

    /// The pixels.
    Bitmap bitmap;
    /// The position of the top-left pixel of the bitmap, relative to the
    /// top-left pixel of the cell (x,y). It may be negative: the bitmap is
    /// then cropped.
    int offset_x = 0;
    int offset_y = 0;

    /// The palette. When null, one is computed from the bitmap.
    std::shared_ptr<const SixelPalette> palette;
    int colors = 256;
    SixelDither dither = SixelDither::FloydSteinberg;
    /// The color partially transparent pixels are blended with. When it is
    /// transparent itself, which is the default, a pixel is either drawn or
    /// not drawn.
    Color background = Color::Default;
  };

  /// @brief Display a graphic.
  ///
  /// The cells it covers, restricted to the `stencil`, become "graphic
  /// cells". The graphic is displayed where they remain so: whatever is drawn
  /// over them afterward punches a hole in it.
  ///
  /// The graphic is part of what ToString() returns.
  void DrawGraphic(Graphic graphic);

  /// @brief The graphics drawn since the last Clear().
  const std::vector<Graphic>& graphics() const { return graphics_; }

  /// @brief Similar to ToString(), for a screen printed again and again at
  /// the same place of the terminal.
  ///
  /// Sending a graphic is expensive. The ones the terminal is already
  /// displaying, as a result of the previous call, are not sent again.
  ///
  /// @param ss The string to append to.
  /// @return Whether a graphic was added, updated, or removed.
  bool ToStringIncremental(std::string& ss);

  /// @brief Forget about what the terminal is displaying. To be called
  /// whenever what was printed might be gone: the terminal was resized,
  /// scrolled, cleared, ...
  void ResetGraphics();

 protected:
  Cursor cursor_;
  std::vector<std::string> hyperlinks_ = {""};

  // The graphics of the current frame.
  std::vector<Graphic> graphics_;
  // What identifies each graphic the terminal is displaying, as a result of
  // the last call to ToStringIncremental().
  std::vector<std::string> displayed_graphics_;

  bool ToStringInternal(std::string& ss,
                        const std::vector<std::string>* previous,
                        std::vector<std::string>* next) const;

  // The current selection style. This is overridden by various dom elements.
  SelectionStyle selection_style_ = [](Cell& cell) { cell.inverted ^= true; };
};

}  // namespace ftxui

#endif  // FTXUI_SCREEN_SCREEN_HPP
