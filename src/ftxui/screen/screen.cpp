// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <algorithm>  // for find, max, min
#include <array>      // for array
#include <cstddef>    // for size_t
#include <cstdint>
#include <cstring>  // for memcpy, strcmp
#include <iostream>  // for cout, flush
#include <limits>
#include <list>    // for list
#include <map>     // for _Rb_tree_const_iterator, map, operator!=, operator==
#include <mutex>   // for mutex, lock_guard
#include <string>  // for string
#include <string_view>    // for string_view
#include <type_traits>    // for is_trivially_copyable
#include <unordered_map>  // for unordered_map
#include <utility>        // for pair
#include <vector>         // for vector

#include "ftxui/screen/cell.hpp"  // for Cell
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/sixel.hpp"     // for EncodeSixel, SixelImage
#include "ftxui/screen/string.hpp"    // for string_width
#include "ftxui/screen/surface.hpp"   // for Surface
#include "ftxui/screen/terminal.hpp"  // for Dimensions, Size

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Macro for hinting that an expression is likely to be false.
#if !defined(FTXUI_UNLIKELY)
#if defined(COMPILER_GCC) || defined(__clang__)
#define FTXUI_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define FTXUI_UNLIKELY(x) (x)
#endif  // defined(COMPILER_GCC)
#endif  // !defined(FTXUI_UNLIKELY)

#if !defined(FTXUI_LIKELY)
#if defined(COMPILER_GCC) || defined(__clang__)
#define FTXUI_LIKELY(x) __builtin_expect(!!(x), 1)
#else
#define FTXUI_LIKELY(x) (x)
#endif  // defined(COMPILER_GCC)
#endif  // !defined(FTXUI_LIKELY)

namespace ftxui {

namespace {

#if defined(_WIN32)
void WindowsEmulateVT100Terminal() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;

  // Enable VT processing on stdout and stdin
  auto stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

  DWORD out_mode = 0;
  if (!GetConsoleMode(stdout_handle, &out_mode)) {
    // The output is not a console (e.g. redirected to a file or a pipe). Keep
    // the detected color support and let the consumer of the stream interpret
    // the escape sequences.
    return;
  }

  // https://docs.microsoft.com/en-us/windows/console/setconsolemode
  const int enable_virtual_terminal_processing = 0x0004;
  const int disable_newline_auto_return = 0x0008;
  out_mode |= enable_virtual_terminal_processing;
  out_mode |= disable_newline_auto_return;

  SetConsoleMode(stdout_handle, out_mode);
}
#endif

// Position of a color sequence inside the output string. It refers to the
// output instead of copying it, so tracking it costs no allocation.
struct ColorSequence {
  size_t begin = 0;
  size_t size = 0;
};

// Append to `ss` the escape sequences switching the style from `prev` to
// `next`.
// - `color_support`: the terminal color support, read once per frame.
// - `last_colors`: the last color sequence written to `ss`.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void UpdateCellStyle(const Screen* screen,
                     std::string& ss,
                     Terminal::Color color_support,
                     ColorSequence& last_colors,
                     const Cell& prev,
                     const Cell& next) {
  // See https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
  if (FTXUI_UNLIKELY(next.hyperlink != prev.hyperlink)) {
    ss += "\x1B]8;;";
    ss += screen->Hyperlink(next.hyperlink);
    ss += "\x1B\\";
  }

  // Bold
  if (FTXUI_UNLIKELY((next.bold ^ prev.bold) | (next.dim ^ prev.dim))) {
    // BOLD_AND_DIM_RESET:
    if ((prev.bold && !next.bold) || (prev.dim && !next.dim)) {
      ss += "\x1B[22m";
    }
    if (next.bold) {
      ss += "\x1B[1m";  // BOLD_SET
    }
    if (next.dim) {
      ss += "\x1B[2m";  // DIM_SET
    }
  }

  // Underline
  if (FTXUI_UNLIKELY(next.underlined != prev.underlined ||
                     next.underlined_double != prev.underlined_double)) {
    ss += (next.underlined          ? "\x1B[4m"     // UNDERLINE
           : next.underlined_double ? "\x1B[21m"    // UNDERLINE_DOUBLE
                                    : "\x1B[24m");  // UNDERLINE_RESET
  }

  // Blink
  if (FTXUI_UNLIKELY(next.blink != prev.blink)) {
    ss += (next.blink ? "\x1B[5m"     // BLINK_SET
                      : "\x1B[25m");  // BLINK_RESET
  }

  // Inverted
  if (FTXUI_UNLIKELY(next.inverted != prev.inverted)) {
    ss += (next.inverted ? "\x1B[7m"     // INVERTED_SET
                         : "\x1B[27m");  // INVERTED_RESET
  }

  // Italics
  if (FTXUI_UNLIKELY(next.italic != prev.italic)) {
    ss += (next.italic ? "\x1B[3m"     // ITALIC_SET
                       : "\x1B[23m");  // ITALIC_RESET
  }

  // StrikeThrough
  if (FTXUI_UNLIKELY(next.strikethrough != prev.strikethrough)) {
    ss += (next.strikethrough ? "\x1B[9m"     // CROSSED_OUT
                              : "\x1B[29m");  // CROSSED_OUT_RESET
  }

  if (FTXUI_UNLIKELY(next.foreground_color != prev.foreground_color ||
                     next.background_color != prev.background_color)) {
    const size_t begin = ss.size();
    ss += "\x1B[";
    next.foreground_color.PrintTo(ss, false, color_support);
    ss += 'm';
    ss += "\x1B[";
    next.background_color.PrintTo(ss, true, color_support);
    ss += 'm';

    // Colors are degraded to the terminal color support when printed, so
    // distinct colors can print the same sequence. e.g. RGB(1,2,3) and
    // RGB(2,3,4) both print "38;5;16" on a 256-color terminal. Drop the new
    // sequence when it repeats the last one. This can't happen with
    // TrueColor, so skip the comparison there.
    if (color_support != Terminal::Color::TrueColor) {
      const size_t size = ss.size() - begin;
      if (size == last_colors.size &&
          ss.compare(begin, size, ss, last_colors.begin, size) == 0) {
        ss.resize(begin);
      } else {
        last_colors.begin = begin;
        last_colors.size = size;
      }
    }
  }
}

// --- Graphics ---------------------------------------------------------------

// Encoding a graphic is expensive: the result is kept. This is useful when a
// graphic comes back after being hidden, or when a screen is printed several
// times.
class SixelCache {
 public:
  // Never destroyed: it must remain usable until the very end of the program.
  static SixelCache& Get() {
    static auto* cache = new SixelCache();  // NOLINT
    return *cache;
  }

  // Returns whether `key` was found. If so, `out` receives the value.
  bool Find(const std::string& key, std::string* out) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) {
      return false;
    }
    entries_.splice(entries_.begin(), entries_, it->second);
    *out = it->second->second;
    return true;
  }

  void Insert(const std::string& key, const std::string& value) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (value.size() > max_bytes_ || index_.count(key) != 0) {
      return;
    }
    entries_.emplace_front(key, value);
    index_[key] = entries_.begin();
    bytes_ += value.size();
    while (bytes_ > max_bytes_ && !entries_.empty()) {
      bytes_ -= entries_.back().second.size();
      index_.erase(entries_.back().first);
      entries_.pop_back();
    }
  }

  void SetMaxBytes(size_t max_bytes) {
    const std::lock_guard<std::mutex> lock(mutex_);
    max_bytes_ = max_bytes;
    while (bytes_ > max_bytes_ && !entries_.empty()) {
      bytes_ -= entries_.back().second.size();
      index_.erase(entries_.back().first);
      entries_.pop_back();
    }
  }

 private:
  using Entry = std::pair<std::string, std::string>;
  std::mutex mutex_;
  std::list<Entry> entries_;  // Most recently used first.
  std::unordered_map<std::string, std::list<Entry>::iterator> index_;
  size_t bytes_ = 0;
  size_t max_bytes_ = size_t{64} << 20;  // 64 MiB.
};

template <class T>
void AppendBytes(std::string& out, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  char buffer[sizeof(T)];  // NOLINT
  std::memcpy(buffer, &value, sizeof(T));
  out.append(buffer, sizeof(T));  // NOLINT
}

bool IsGraphicCell(const Cell& cell, size_t id) {
  return cell.graphic == id &&
         std::strcmp(cell.character.c_str(), Cell::GraphicCharacter()) == 0;
}

// What to do about a graphic, when printing the screen.
struct GraphicPlan {
  // Whether the graphic has at least one cell left.
  bool visible = false;
  // Whether the terminal is already displaying it.
  bool retained = false;
  // The cells to draw, and where.
  Box box;
  SixelImage image;
  // What identifies the result of encoding `image`.
  std::string key;
};

struct TileEncoding {
  std::uint8_t left : 2;
  std::uint8_t top : 2;
  std::uint8_t right : 2;
  std::uint8_t down : 2;
  std::uint8_t round : 1;

  // clang-format off
  bool operator<(const TileEncoding& other) const {
    if (left < other.left)   { return  true;  }
    if (left > other.left)   { return  false; }
    if (top < other.top)     { return  true;  }
    if (top > other.top)     { return  false; }
    if (right < other.right) { return  true;  }
    if (right > other.right) { return  false; }
    if (down < other.down)   { return  true;  }
    if (down > other.down)   { return  false; }
    if (round < other.round) { return  true;  }
    if (round > other.round) { return  false; }
      return false;
  }
  // clang-format on
};

// clang-format off
const std::map<std::string, TileEncoding> tile_encoding = { // NOLINT
    {"─", {1, 0, 1, 0, 0}},
    {"━", {2, 0, 2, 0, 0}},
    {"╍", {2, 0, 2, 0, 0}},

    {"│", {0, 1, 0, 1, 0}},
    {"┃", {0, 2, 0, 2, 0}},
    {"╏", {0, 2, 0, 2, 0}},

    {"┌", {0, 0, 1, 1, 0}},
    {"┍", {0, 0, 2, 1, 0}},
    {"┎", {0, 0, 1, 2, 0}},
    {"┏", {0, 0, 2, 2, 0}},

    {"┐", {1, 0, 0, 1, 0}},
    {"┑", {2, 0, 0, 1, 0}},
    {"┒", {1, 0, 0, 2, 0}},
    {"┓", {2, 0, 0, 2, 0}},

    {"└", {0, 1, 1, 0, 0}},
    {"┕", {0, 1, 2, 0, 0}},
    {"┖", {0, 2, 1, 0, 0}},
    {"┗", {0, 2, 2, 0, 0}},

    {"┘", {1, 1, 0, 0, 0}},
    {"┙", {2, 1, 0, 0, 0}},
    {"┚", {1, 2, 0, 0, 0}},
    {"┛", {2, 2, 0, 0, 0}},

    {"├", {0, 1, 1, 1, 0}},
    {"┝", {0, 1, 2, 1, 0}},
    {"┞", {0, 2, 1, 1, 0}},
    {"┟", {0, 1, 1, 2, 0}},
    {"┠", {0, 2, 1, 2, 0}},
    {"┡", {0, 2, 2, 1, 0}},
    {"┢", {0, 1, 2, 2, 0}},
    {"┣", {0, 2, 2, 2, 0}},

    {"┤", {1, 1, 0, 1, 0}},
    {"┥", {2, 1, 0, 1, 0}},
    {"┦", {1, 2, 0, 1, 0}},
    {"┧", {1, 1, 0, 2, 0}},
    {"┨", {1, 2, 0, 2, 0}},
    {"┩", {2, 2, 0, 1, 0}},
    {"┪", {2, 1, 0, 2, 0}},
    {"┫", {2, 2, 0, 2, 0}},

    {"┬", {1, 0, 1, 1, 0}},
    {"┭", {2, 0, 1, 1, 0}},
    {"┮", {1, 0, 2, 1, 0}},
    {"┯", {2, 0, 2, 1, 0}},
    {"┰", {1, 0, 1, 2, 0}},
    {"┱", {2, 0, 1, 2, 0}},
    {"┲", {1, 0, 2, 2, 0}},
    {"┳", {2, 0, 2, 2, 0}},

    {"┴", {1, 1, 1, 0, 0}},
    {"┵", {2, 1, 1, 0, 0}},
    {"┶", {1, 1, 2, 0, 0}},
    {"┷", {2, 1, 2, 0, 0}},
    {"┸", {1, 2, 1, 0, 0}},
    {"┹", {2, 2, 1, 0, 0}},
    {"┺", {1, 2, 2, 0, 0}},
    {"┻", {2, 2, 2, 0, 0}},

    {"┼", {1, 1, 1, 1, 0}},
    {"┽", {2, 1, 1, 1, 0}},
    {"┾", {1, 1, 2, 1, 0}},
    {"┿", {2, 1, 2, 1, 0}},
    {"╀", {1, 2, 1, 1, 0}},
    {"╁", {1, 1, 1, 2, 0}},
    {"╂", {1, 2, 1, 2, 0}},
    {"╃", {2, 2, 1, 1, 0}},
    {"╄", {1, 2, 2, 1, 0}},
    {"╅", {2, 1, 1, 2, 0}},
    {"╆", {1, 1, 2, 2, 0}},
    {"╇", {2, 2, 2, 1, 0}},
    {"╈", {2, 1, 2, 2, 0}},
    {"╉", {2, 2, 1, 2, 0}},
    {"╊", {1, 2, 2, 2, 0}},
    {"╋", {2, 2, 2, 2, 0}},

    {"═", {3, 0, 3, 0, 0}},
    {"║", {0, 3, 0, 3, 0}},

    {"╒", {0, 0, 3, 1, 0}},
    {"╓", {0, 0, 1, 3, 0}},
    {"╔", {0, 0, 3, 3, 0}},

    {"╕", {3, 0, 0, 1, 0}},
    {"╖", {1, 0, 0, 3, 0}},
    {"╗", {3, 0, 0, 3, 0}},

    {"╘", {0, 1, 3, 0, 0}},
    {"╙", {0, 3, 1, 0, 0}},
    {"╚", {0, 3, 3, 0, 0}},

    {"╛", {3, 1, 0, 0, 0}},
    {"╜", {1, 3, 0, 0, 0}},
    {"╝", {3, 3, 0, 0, 0}},

    {"╞", {0, 1, 3, 1, 0}},
    {"╟", {0, 3, 1, 3, 0}},
    {"╠", {0, 3, 3, 3, 0}},

    {"╡", {3, 1, 0, 1, 0}},
    {"╢", {1, 3, 0, 3, 0}},
    {"╣", {3, 3, 0, 3, 0}},

    {"╤", {3, 0, 3, 1, 0}},
    {"╥", {1, 0, 1, 3, 0}},
    {"╦", {3, 0, 3, 3, 0}},

    {"╧", {3, 1, 3, 0, 0}},
    {"╨", {1, 3, 1, 0, 0}},
    {"╩", {3, 3, 3, 0, 0}},

    {"╪", {3, 1, 3, 1, 0}},
    {"╫", {1, 3, 1, 3, 0}},
    {"╬", {3, 3, 3, 3, 0}},

    {"╭", {0, 0, 1, 1, 1}},
    {"╮", {1, 0, 0, 1, 1}},
    {"╯", {1, 1, 0, 0, 1}},
    {"╰", {0, 1, 1, 0, 1}},

    {"╴", {1, 0, 0, 0, 0}},
    {"╵", {0, 1, 0, 0, 0}},
    {"╶", {0, 0, 1, 0, 0}},
    {"╷", {0, 0, 0, 1, 0}},

    {"╸", {2, 0, 0, 0, 0}},
    {"╹", {0, 2, 0, 0, 0}},
    {"╺", {0, 0, 2, 0, 0}},
    {"╻", {0, 0, 0, 2, 0}},

    {"╼", {1, 0, 2, 0, 0}},
    {"╽", {0, 1, 0, 2, 0}},
    {"╾", {2, 0, 1, 0, 0}},
    {"╿", {0, 2, 0, 1, 0}},
};
// clang-format on

template <class A, class B>
std::map<B, A> InvertMap(const std::map<A, B>& input) {
  std::map<B, A> output;
  for (const auto& it : input) {
    output[it.second] = it.first;
  }
  return output;
}

const std::map<TileEncoding, std::string> tile_encoding_inverse =  // NOLINT
    InvertMap(tile_encoding);

void UpgradeLeftRight(std::string& left, std::string& right) {
  const auto it_left = tile_encoding.find(left);
  if (it_left == tile_encoding.end()) {
    return;
  }
  const auto it_right = tile_encoding.find(right);
  if (it_right == tile_encoding.end()) {
    return;
  }

  if (it_left->second.right == 0 && it_right->second.left != 0) {
    TileEncoding encoding_left = it_left->second;
    encoding_left.right = it_right->second.left;
    const auto it_left_upgrade = tile_encoding_inverse.find(encoding_left);
    if (it_left_upgrade != tile_encoding_inverse.end()) {
      left = it_left_upgrade->second;
    }
  }

  if (it_right->second.left == 0 && it_left->second.right != 0) {
    TileEncoding encoding_right = it_right->second;
    encoding_right.left = it_left->second.right;
    const auto it_right_upgrade = tile_encoding_inverse.find(encoding_right);
    if (it_right_upgrade != tile_encoding_inverse.end()) {
      right = it_right_upgrade->second;
    }
  }
}

void UpgradeTopDown(std::string& top, std::string& down) {
  const auto it_top = tile_encoding.find(top);
  if (it_top == tile_encoding.end()) {
    return;
  }
  const auto it_down = tile_encoding.find(down);
  if (it_down == tile_encoding.end()) {
    return;
  }

  if (it_top->second.down == 0 && it_down->second.top != 0) {
    TileEncoding encoding_top = it_top->second;
    encoding_top.down = it_down->second.top;
    const auto it_top_down = tile_encoding_inverse.find(encoding_top);
    if (it_top_down != tile_encoding_inverse.end()) {
      top = it_top_down->second;
    }
  }

  if (it_down->second.top == 0 && it_top->second.down != 0) {
    TileEncoding encoding_down = it_down->second;
    encoding_down.top = it_top->second.down;
    const auto it_down_top = tile_encoding_inverse.find(encoding_down);
    if (it_down_top != tile_encoding_inverse.end()) {
      down = it_down_top->second;
    }
  }
}

bool ShouldAttemptAutoMerge(Cell& cell) {
  return cell.automerge && cell.character.size() == 3;
}

}  // namespace

/// A fixed dimension.
/// @see Fit
/// @see Full
Dimensions Dimension::Fixed(int v) {
  return {v, v};
}

/// Use the terminal dimensions.
/// @see Fixed
/// @see Fit
Dimensions Dimension::Full() {
  return Terminal::Size();
}

// static
/// Create a screen with the given dimension along the x-axis and y-axis.
Screen Screen::Create(Dimensions width, Dimensions height) {
  return {width.dimx, height.dimy};
}

// static
/// Create a screen with the given dimension.
Screen Screen::Create(Dimensions dimension) {
  return {dimension.dimx, dimension.dimy};
}

Screen::Screen(int dimx, int dimy) : Surface{dimx, dimy} {
#if defined(_WIN32)
  // The placement of this call is a bit weird, however we can assume that
  // anybody who instantiates a Screen object eventually wants to output
  // something to the console. If that is not the case, use an instance of
  // Surface instead. As we require UTF8 for all input/output operations we will
  // just switch to UTF8 encoding here
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
  WindowsEmulateVT100Terminal();
#endif
}

/// Produce a std::string that can be used to print the Screen on the
/// terminal.
/// @note Don't forget to flush stdout. Alternatively, you can use
/// Screen::Print();
std::string Screen::ToString() const {
  // Pre-allocate: ~30 bytes per cell for character + escape codes.
  std::string ss;
  ss.reserve(static_cast<size_t>(dimx_) * static_cast<size_t>(dimy_) * 30);
  ToString(ss);
  return ss;
}

/// Produce a std::string that can be used to print the Screen on the
/// terminal.
/// @param ss The string to append to.
void Screen::ToString(std::string& ss) const {
  ToStringInternal(ss, nullptr, nullptr);
}

bool Screen::ToStringIncremental(std::string& ss) {
  std::vector<std::string> next;
  const bool changed = ToStringInternal(ss, &displayed_graphics_, &next);
  displayed_graphics_ = std::move(next);
  return changed;
}

void Screen::ResetGraphics() {
  displayed_graphics_.clear();
}

/// @brief Display a graphic over a rectangle of cells.
void Screen::DrawGraphic(Graphic graphic) {
  if (graphic.bitmap.empty() || graphic.width <= 0 || graphic.height <= 0 ||
      graphic.cell_width <= 0 || graphic.cell_height <= 0) {
    return;
  }
  if (graphics_.size() >= std::numeric_limits<uint16_t>::max()) {
    return;
  }

  const int x_min = std::max({graphic.x, stencil.x_min, 0});
  const int y_min = std::max({graphic.y, stencil.y_min, 0});
  const int x_max =
      std::min({graphic.x + graphic.width - 1, stencil.x_max, dimx_ - 1});
  const int y_max =
      std::min({graphic.y + graphic.height - 1, stencil.y_max, dimy_ - 1});
  if (x_min > x_max || y_min > y_max) {
    return;
  }

  const auto id = static_cast<uint16_t>(graphics_.size() + 1);
  for (int y = y_min; y <= y_max; ++y) {
    for (int x = x_min; x <= x_max; ++x) {
      Cell& cell = FastCellAt(x, y);
      cell.character = Cell::GraphicCharacter();
      cell.graphic = id;
    }
  }
  graphics_.push_back(std::move(graphic));
}

// Print the screen. The graphics listed in `previous` are assumed to be
// displayed by the terminal: they are left untouched instead of being sent
// again. The graphics displayed afterward are listed in `next`.
// Returns whether a graphic was added, updated or removed.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool Screen::ToStringInternal(std::string& ss,
                              const std::vector<std::string>* previous,
                              std::vector<std::string>* next) const {
  // ---------------------------------------------------------------------------
  // Step 1: Find what remains visible of each graphic.
  // ---------------------------------------------------------------------------
  std::vector<GraphicPlan> plans(graphics_.size());
  bool changed = false;

  // Drawing an image on the last row of a terminal makes many of them scroll,
  // because they move the cursor to the line below the image. The last row of
  // the screen might be the last row of the terminal. Unless the terminal is
  // known to be able to leave the cursor in place, graphics stay away from
  // it: their cells of the last row display nothing.
  const bool last_row_support = Terminal::SixelLastRowSupport();
  const int last_row = last_row_support ? dimy_ - 1 : dimy_ - 2;

  for (size_t i = 0; i < graphics_.size(); ++i) {
    const Graphic& graphic = graphics_[i];
    GraphicPlan& plan = plans[i];
    const size_t id = i + 1;

    const int x_min = std::max(graphic.x, 0);
    const int y_min = std::max(graphic.y, 0);
    const int x_max = std::min(graphic.x + graphic.width - 1, dimx_ - 1);
    const int y_max = std::min(graphic.y + graphic.height - 1, last_row);

    // The bounding box of its cells.
    Box& box = plan.box;
    box = Box{dimx_, -1, dimy_, -1};
    for (int y = y_min; y <= y_max; ++y) {
      for (int x = x_min; x <= x_max; ++x) {
        if (!IsGraphicCell(FastCellAt(x, y), id)) {
          continue;
        }
        box.x_min = std::min(box.x_min, x);
        box.x_max = std::max(box.x_max, x);
        box.y_min = std::min(box.y_min, y);
        box.y_max = std::max(box.y_max, y);
      }
    }
    if (box.x_min > box.x_max || box.y_min > box.y_max) {
      continue;
    }
    plan.visible = true;

    // Inside the bounding box: which cells are drawn. Also, a hash of what is
    // displayed behind them, which shows through the transparent pixels.
    const int columns = box.x_max - box.x_min + 1;
    const int rows = box.y_max - box.y_min + 1;
    SixelImage& image = plan.image;
    image.mask.assign(static_cast<size_t>(columns) * static_cast<size_t>(rows),
                      0);
    bool full = true;
    uint64_t style = 14695981039346656037ULL;  // FNV-1a
    auto hash = [&](const void* data, size_t size) {
      const auto* bytes = static_cast<const unsigned char*>(data);
      for (size_t k = 0; k < size; ++k) {
        style = (style ^ bytes[k]) * 1099511628211ULL;  // NOLINT
      }
    };
    for (int y = box.y_min; y <= box.y_max; ++y) {
      for (int x = box.x_min; x <= box.x_max; ++x) {
        const Cell& cell = FastCellAt(x, y);
        if (!IsGraphicCell(cell, id)) {
          full = false;
          continue;
        }
        image.mask[static_cast<size_t>(y - box.y_min) *
                       static_cast<size_t>(columns) +
                   static_cast<size_t>(x - box.x_min)] = 1;
        // Every style is visible on a blank cell, one way or another.
        const std::array<unsigned char, 6> styles = {
            static_cast<unsigned char>(cell.inverted),
            static_cast<unsigned char>(cell.underlined),
            static_cast<unsigned char>(cell.underlined_double),
            static_cast<unsigned char>(cell.strikethrough),
            static_cast<unsigned char>(cell.blink),
            cell.hyperlink,
        };
        hash(&cell.background_color, sizeof(Color));
        hash(&cell.foreground_color, sizeof(Color));
        hash(styles.data(), styles.size());
      }
    }
    if (full) {
      image.mask.clear();
    }

    image.bitmap = graphic.bitmap;
    image.palette = graphic.palette;
    image.colors = graphic.colors;
    image.dither = graphic.dither;
    image.width = columns * graphic.cell_width;
    image.height = rows * graphic.cell_height;
    image.offset_x =
        graphic.offset_x - (box.x_min - graphic.x) * graphic.cell_width;
    image.offset_y =
        graphic.offset_y - (box.y_min - graphic.y) * graphic.cell_height;
    image.mask_columns = columns;
    image.mask_cell_width = graphic.cell_width;
    image.mask_cell_height = graphic.cell_height;
    image.background = graphic.background;

    // What identifies the pixels sent to the terminal.
    std::string& key = plan.key;
    AppendBytes(key, graphic.bitmap.id());
    AppendBytes(key, graphic.palette ? graphic.palette->id() : uint64_t{0});
    AppendBytes(key, image.colors);
    AppendBytes(key, image.dither);
    AppendBytes(key, image.width);
    AppendBytes(key, image.height);
    AppendBytes(key, image.offset_x);
    AppendBytes(key, image.offset_y);
    AppendBytes(key, image.mask_cell_width);
    AppendBytes(key, image.mask_cell_height);
    AppendBytes(key, image.background.GetAlpha() != 0);
    AppendBytes(key, image.background.GetRed());
    AppendBytes(key, image.background.GetGreen());
    AppendBytes(key, image.background.GetBlue());
    key.append(image.mask.begin(), image.mask.end());

    // What identifies what the terminal displays: the same, plus where, and
    // above what.
    std::string signature = key;
    AppendBytes(signature, box.x_min);
    AppendBytes(signature, box.y_min);
    AppendBytes(signature, style);

    plan.retained = previous && std::find(previous->begin(), previous->end(),
                                          signature) != previous->end();
    changed |= !plan.retained;
    if (next) {
      next->push_back(std::move(signature));
    }
  }

  // A graphic which is gone is a change: text is printed over it below.
  if (previous && next) {
    for (const std::string& signature : *previous) {
      changed |=
          std::find(next->begin(), next->end(), signature) == next->end();
    }
  }

  // ---------------------------------------------------------------------------
  // Step 2: Print the text.
  // ---------------------------------------------------------------------------
  const Cell default_cell;
  const Cell* previous_cell_ref = &default_cell;
  const Terminal::Color color_support = Terminal::ColorSupport();
  ColorSequence last_colors;

  // The cells of a graphic the terminal is already displaying are skipped by
  // moving the cursor forward. Printing them would erase it.
  int skipped = 0;
  auto flush_skipped = [&] {
    if (skipped == 0) {
      return;
    }
    ss += "\x1B[";
    ss += std::to_string(skipped);
    ss += 'C';
    skipped = 0;
  };

  for (int y = 0; y < dimy_; ++y) {
    // New line in between two lines.
    if (y != 0) {
      UpdateCellStyle(this, ss, color_support, last_colors, *previous_cell_ref,
                      default_cell);
      previous_cell_ref = &default_cell;
      ss += "\r\n";
    }

    // After printing a fullwith character, we need to skip the next cell.
    bool previous_fullwidth = false;
    if (dimx_ > 0) {
      const Cell* line_start = &FastCellAt(0, y);
      const Cell* line_end = line_start + dimx_;
      for (const Cell* it = line_start; it != line_end; ++it) {
        const auto& cell = *it;

        // A cell displaying a graphic.
        if (FTXUI_UNLIKELY(cell.graphic != 0) && cell.graphic <= plans.size() &&
            IsGraphicCell(cell, cell.graphic)) {
          if (!previous_fullwidth) {
            if (plans[cell.graphic - 1].retained && y <= last_row) {
              skipped++;
            } else {
              flush_skipped();
              UpdateCellStyle(this, ss, color_support, last_colors,
                              *previous_cell_ref, cell);
              previous_cell_ref = &cell;
              ss += ' ';
            }
          }
          previous_fullwidth = false;
          continue;
        }

        if (!previous_fullwidth) {
          flush_skipped();
          UpdateCellStyle(this, ss, color_support, last_colors,
                          *previous_cell_ref, cell);
          previous_cell_ref = &cell;
          if (cell.character.empty()) {
            ss += ' ';
          } else {
            ss += cell.character;
          }
        }
        if (cell.character.size() <= 1) {
          previous_fullwidth = false;
        } else {
          previous_fullwidth = (string_width(cell.character) == 2);
        }
      }
    }
    flush_skipped();
  }

  // Reset the style to default:
  UpdateCellStyle(this, ss, color_support, last_colors, *previous_cell_ref,
                  default_cell);

  // ---------------------------------------------------------------------------
  // Step 3: Draw the graphics, over the text.
  // ---------------------------------------------------------------------------
  // The DEC private mode 8452: after an image, the cursor is left on its
  // last row, instead of being moved below. See above.
  bool cursor_mode_set = false;
  for (const GraphicPlan& plan : plans) {
    if (!plan.visible || plan.retained) {
      continue;
    }

    std::string sixel;
    if (!SixelCache::Get().Find(plan.key, &sixel)) {
      sixel = EncodeSixel(plan.image);
      SixelCache::Get().Insert(plan.key, sixel);
    }
    if (sixel.empty()) {
      continue;
    }

    if (last_row_support && !cursor_mode_set) {
      ss += "\x1B[?8452h";
      cursor_mode_set = true;
    }

    // The cursor is at the end of the last line. Save its position, move to
    // the top-left cell of the graphic, draw, then restore its position.
    // Where a terminal leaves the cursor after drawing a graphic is
    // unreliable.
    ss += "\0337";  // DECSC
    ss += '\r';
    const int up = dimy_ - 1 - plan.box.y_min;
    if (up > 0) {
      ss += "\x1B[";
      ss += std::to_string(up);
      ss += 'A';
    }
    if (plan.box.x_min > 0) {
      ss += "\x1B[";
      ss += std::to_string(plan.box.x_min);
      ss += 'C';
    }
    ss += sixel;
    ss += "\0338";  // DECRC
  }
  if (cursor_mode_set) {
    ss += "\x1B[?8452l";
  }

  return changed;
}

// Print the Screen to the terminal.
void Screen::Print() const {
  std::cout << ToString() << '\0' << std::flush;
}

/// @brief Return a string to be printed in order to reset the cursor position
///        to the beginning of the screen.
///
/// ```cpp
/// std::string reset_position;
/// while(true) {
///   auto document = render();
///   auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));
///   Render(screen, document);
///   std::cout << reset_position << screen.ToString() << std::flush;
///   reset_position = screen.ResetPosition();
///
///   using namespace std::chrono_literals;
///   std::this_thread::sleep_for(0.01s);
/// }
/// ```
///
/// @return The string to print in order to reset the cursor position to the
///         beginning.
std::string Screen::ResetPosition(bool clear) const {
  std::string ss;
  ss.reserve(static_cast<size_t>(dimy_) * 12);
  ResetPosition(ss, clear);
  return ss;
}

/// @brief Append to a string in order to reset the cursor position to the
///        beginning of the screen.
/// @param ss The string to append to.
/// @param clear Whether to clear the screen or not.
void Screen::ResetPosition(std::string& ss, bool clear) const {
  if (clear) {
    // The clear branch must move up one row at a time, because each row needs
    // its own CLEAR_LINE (\x1B[2K) erase. It cannot be collapsed into a single
    // parameterized cursor-up.
    ss += '\r';       // MOVE_LEFT;
    ss += "\x1b[2K";  // CLEAR_SCREEN;
    for (int y = 1; y < dimy_; ++y) {
      ss += "\x1B[1A";  // MOVE_UP;
      ss += "\x1B[2K";  // CLEAR_LINE;
    }
  } else {
    // The non-clear branch only needs to reposition the cursor at the top-left,
    // so the per-row walk-up is collapsed into a single parameterized
    // CSI cursor-up (\x1B[<n>A), emitting far fewer bytes per frame.
    ss += '\r';  // MOVE_LEFT;
    if (dimy_ > 1) {
      ss += "\x1B[" + std::to_string(dimy_ - 1) + "A";  // MOVE_UP;
    }
  }
}

/// @brief Clear all the cells from the screen.
void Screen::Clear() {
  Surface::Clear();

  cursor_.x = dimx_ - 1;
  cursor_.y = dimy_ - 1;

  hyperlinks_ = {
      "",
  };

  graphics_.clear();
}

// clang-format off
void Screen::ApplyShader() {
  // Merge box characters together.
  for (int y = 0; y < dimy_; ++y) {
    for (int x = 0; x < dimx_; ++x) {
      // Box drawing character uses exactly 3 byte.
      Cell& cur = FastCellAt(x, y);
      if (!ShouldAttemptAutoMerge(cur)) {
        continue;
      }

      if (x > 0) {
        Cell& left = FastCellAt(x - 1, y);
        if (ShouldAttemptAutoMerge(left)) {
          UpgradeLeftRight(left.character, cur.character);
        }
      }
      if (y > 0) {
        Cell& top = FastCellAt(x, y - 1);
        if (ShouldAttemptAutoMerge(top)) {
          UpgradeTopDown(top.character, cur.character);
        }
      }
    }
  }
}
// clang-format on

std::uint8_t Screen::RegisterHyperlink(std::string_view link) {
  for (std::size_t i = 0; i < hyperlinks_.size(); ++i) {
    if (hyperlinks_[i] == link) {
      return i;
    }
  }
  if (hyperlinks_.size() == std::numeric_limits<std::uint8_t>::max()) {
    return 0;
  }
  hyperlinks_.emplace_back(link);
  return hyperlinks_.size() - 1;
}

const std::string& Screen::Hyperlink(std::uint8_t id) const {
  if (id >= hyperlinks_.size()) {
    return hyperlinks_[0];
  }
  return hyperlinks_[id];
}

/// @brief Return the current selection style.
/// @see SetSelectionStyle
const Screen::SelectionStyle& Screen::GetSelectionStyle() const {
  return selection_style_;
}

/// @brief Set the current selection style.
/// @see GetSelectionStyle
void Screen::SetSelectionStyle(SelectionStyle decorator) {
  selection_style_ = std::move(decorator);
}

/// @brief Set how much memory is used to remember encoded graphics.
/// @param max_bytes The size, in bytes. The default is 64 MiB.
/// @ingroup screen
void SetSixelCacheSize(size_t max_bytes) {
  SixelCache::Get().SetMaxBytes(max_bytes);
}

void Screen::Reserved1() {}
void Screen::Reserved2() {}
void Screen::Reserved3() {}
void Screen::Reserved4() {}
void Screen::Reserved5() {}
void Screen::Reserved6() {}
void Screen::Reserved7() {}
void Screen::Reserved8() {}

}  // namespace ftxui
