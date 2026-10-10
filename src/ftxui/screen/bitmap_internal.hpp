// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_BITMAP_INTERNAL_HPP
#define FTXUI_SCREEN_BITMAP_INTERNAL_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "ftxui/screen/bitmap.hpp"

namespace ftxui {

class SixelPalette;

struct Bitmap::Storage {
  int width = 0;
  int height = 0;
  uint64_t id = 0;
  bool opaque = true;
  std::vector<uint8_t> pixels;

  // The palettes computed for this content, by number of colors and by
  // background color. Computing one requires a pass over the pixels, so they
  // are kept next to them.
  std::mutex palettes_mutex;
  std::map<uint64_t, std::shared_ptr<const SixelPalette>> palettes;
};

}  // namespace ftxui

#endif  // FTXUI_SCREEN_BITMAP_INTERNAL_HPP
