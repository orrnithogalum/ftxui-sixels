// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <cmath>     // for sin, cos, sqrt
#include <cstdint>   // for uint8_t
#include <iostream>  // for cout
#include <string>    // for string
#include <vector>    // for vector

#include "ftxui/dom/elements.hpp"     // for image, text, hbox, vbox, border
#include "ftxui/screen/bitmap.hpp"    // for Bitmap
#include "ftxui/screen/screen.hpp"    // for Screen
#include "ftxui/screen/terminal.hpp"  // for QueryGraphics

using namespace ftxui;

// A picture made of a gradient, and of a disk. The disk tells whether the
// aspect ratio is preserved.
Bitmap Picture(int width, int height) {
  std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      uint8_t* pixel = &pixels[static_cast<size_t>((y * width + x) * 4)];
      const float u = float(x) / float(width);
      const float v = float(y) / float(height);
      pixel[0] = static_cast<uint8_t>(255.F * u);
      pixel[1] = static_cast<uint8_t>(255.F * v);
      pixel[2] = static_cast<uint8_t>(255.F * (1.F - u));
      pixel[3] = 255;

      const float dx = float(x) - float(width) / 2.F;
      const float dy = float(y) - float(height) / 2.F;
      const float radius = float(height) / 3.F;
      if (dx * dx + dy * dy < radius * radius) {
        pixel[0] = 250;
        pixel[1] = 240;
        pixel[2] = 220;
      }
    }
  }
  return Bitmap::FromRGBA(width, height, pixels.data());
}

int main(int argc, char** argv) {
  // Without an ftxui::App, the terminal has to be asked explicitly whether it
  // supports sixel. Otherwise, the image is drawn with unicode blocks.
  Terminal::QueryGraphics();

  // The image is either a file given as argument, or a generated one.
  auto picture = [&](ImageOptions options) {
    return argc >= 2 ? image(argv[1], options)
                     : image(Picture(320, 200), options);
  };

  // An image is an element like any other: it takes the size its container
  // gives to it, and it can be decorated.
  auto cell = [&](const std::string& title, ImageOptions options) {
    return window(text(title), picture(options)) | size(WIDTH, EQUAL, 26) |
           size(HEIGHT, EQUAL, 12);
  };

  auto document = vbox({
      hbox({
          cell("Contain", ImageOptions().Fit(ImageFit::Contain)),
          cell("Cover", ImageOptions().Fit(ImageFit::Cover)),
          cell("Fill", ImageOptions().Fit(ImageFit::Fill)),
      }),
      hbox({
          cell("Top left", ImageOptions()
                               .Align(ImageAlign::Start, ImageAlign::Start)
                               .MaxSize(12, 5)),
          cell("Margin", ImageOptions().Fit(ImageFit::Fill).Margin(4, 2)),
          cell("Bottom right", ImageOptions()
                                   .Align(ImageAlign::End, ImageAlign::End)
                                   .MaxSize(12, 5)),
      }),
      text(Terminal::SixelSupport()
               ? "Drawn with sixel."
               : "Drawn with unicode blocks: the terminal doesn't advertise "
                 "sixel. Set FTXUI_SIXEL=1 to force it."),
  });

  // The whole document is printed, even if it is larger than the terminal.
  auto screen =
      Screen::Create(Dimension::Fit(document, /*extend_beyond_screen=*/true));
  Render(screen, document);
  screen.Print();
  std::cout << std::endl;
  return 0;
}
