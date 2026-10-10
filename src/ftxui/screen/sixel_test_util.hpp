// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_SIXEL_TEST_UTIL_HPP
#define FTXUI_SCREEN_SIXEL_TEST_UTIL_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Tools for tests: a sixel decoder, and bitmaps.
namespace ftxui::test {

// A decoded sixel image. A pixel which was not drawn has an alpha of 0.
struct Decoded {
  bool valid = false;
  int width = 0;   // From the raster attributes.
  int height = 0;  // From the raster attributes.
  int colors = 0;  // The number of colors defined.
  // The extent of the pixels actually drawn.
  int drawn_width = 0;
  int drawn_height = 0;
  std::vector<std::array<uint8_t, 4>> pixels;  // width * height, RGBA.

  std::array<uint8_t, 4> at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return {0, 0, 0, 0};
    }
    return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) +
                  static_cast<size_t>(x)];
  }
  bool drawn(int x, int y) const { return at(x, y)[3] != 0; }
};

// Decode one sixel sequence: `ESC P ... q ... ESC \`.
inline Decoded DecodeSixel(std::string_view data) {
  Decoded out;
  const std::string_view intro = "\x1BP";
  const std::string_view terminator = "\x1B\\";
  if (data.size() < intro.size() + terminator.size() ||
      data.substr(0, intro.size()) != intro ||
      data.substr(data.size() - terminator.size()) != terminator) {
    return out;
  }
  size_t i = intro.size();
  const size_t end = data.size() - terminator.size();

  auto number = [&] {
    int value = 0;
    while (i < end && data[i] >= '0' && data[i] <= '9') {
      value = value * 10 + (data[i] - '0');
      i++;
    }
    return value;
  };
  auto numbers = [&] {
    std::vector<int> values;
    values.push_back(number());
    while (i < end && data[i] == ';') {
      i++;
      values.push_back(number());
    }
    return values;
  };

  // The parameters, up to 'q'.
  numbers();
  if (i >= end || data[i] != 'q') {
    return out;
  }
  i++;

  std::map<int, std::array<uint8_t, 3>> palette;
  int color = 0;
  int x = 0;
  int y = 0;
  int repeat = 1;
  // The pixels drawn, before the size is known for sure.
  std::map<std::pair<int, int>, std::array<uint8_t, 3>> drawn;

  while (i < end) {
    const char c = data[i];
    if (c == '"') {
      i++;
      const std::vector<int> values = numbers();
      if (values.size() == 4) {
        out.width = values[2];
        out.height = values[3];
      }
    } else if (c == '#') {
      i++;
      const std::vector<int> values = numbers();
      color = values[0];
      if (values.size() == 5 && values[1] == 2) {
        palette[color] = {
            static_cast<uint8_t>((values[2] * 255 + 50) / 100),
            static_cast<uint8_t>((values[3] * 255 + 50) / 100),
            static_cast<uint8_t>((values[4] * 255 + 50) / 100),
        };
      }
    } else if (c == '!') {
      i++;
      repeat = number();
    } else if (c == '$') {
      i++;
      x = 0;
    } else if (c == '-') {
      i++;
      x = 0;
      y += 6;
    } else if (c >= '?' && c <= '~') {
      i++;
      const int bits = c - '?';
      for (int r = 0; r < repeat; ++r) {
        for (int bit = 0; bit < 6; ++bit) {
          if (bits & (1 << bit)) {
            drawn[{x, y + bit}] = palette[color];
            out.drawn_width = std::max(out.drawn_width, x + 1);
            out.drawn_height = std::max(out.drawn_height, y + bit + 1);
          }
        }
        x++;
      }
      repeat = 1;
    } else {
      return out;  // Unexpected.
    }
  }

  out.colors = static_cast<int>(palette.size());
  out.pixels.assign(
      static_cast<size_t>(out.width) * static_cast<size_t>(out.height),
      {0, 0, 0, 0});
  for (const auto& [position, rgb] : drawn) {
    if (position.first >= out.width || position.second >= out.height) {
      continue;
    }
    out.pixels[static_cast<size_t>(position.second) *
                   static_cast<size_t>(out.width) +
               static_cast<size_t>(position.first)] = {rgb[0], rgb[1], rgb[2],
                                                       255};
  }
  out.valid = true;
  return out;
}

// The sixel sequences found in what a Screen prints.
inline std::vector<std::string> FindSixels(std::string_view data) {
  std::vector<std::string> out;
  size_t position = 0;
  while (true) {
    const size_t begin = data.find("\x1BP", position);
    if (begin == std::string_view::npos) {
      break;
    }
    const size_t end = data.find("\x1B\\", begin);
    if (end == std::string_view::npos) {
      break;
    }
    out.emplace_back(data.substr(begin, end + 2 - begin));
    position = end + 2;
  }
  return out;
}

}  // namespace ftxui::test

#endif  // FTXUI_SCREEN_SIXEL_TEST_UTIL_HPP
