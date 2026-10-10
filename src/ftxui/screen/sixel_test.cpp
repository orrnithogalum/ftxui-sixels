// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "ftxui/screen/bitmap.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/screen/sixel.hpp"
#include "ftxui/screen/sixel_test_util.hpp"
#include "gtest/gtest.h"

namespace ftxui {

namespace {

using RGBA = std::array<uint8_t, 4>;

// Colors sixel can express exactly: multiples of 51 are multiples of 20%.
constexpr RGBA g_red = {255, 0, 0, 255};
constexpr RGBA g_green = {0, 255, 0, 255};
constexpr RGBA g_blue = {0, 0, 255, 255};
constexpr RGBA g_gray = {102, 102, 102, 255};
constexpr RGBA g_none = {0, 0, 0, 0};

template <class F>
Bitmap Make(int width, int height, F color) {
  std::vector<uint8_t> pixels;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const RGBA c = color(x, y);
      pixels.insert(pixels.end(), c.begin(), c.end());
    }
  }
  return Bitmap::FromRGBA(width, height, pixels.data());
}

// Vertical stripes: red, green, blue.
Bitmap Stripes(int width, int height) {
  return Make(width, height, [&](int x, int) {
    return x * 3 < width ? g_red : x * 3 < 2 * width ? g_green : g_blue;
  });
}

}  // namespace

TEST(SixelTest, Empty) {
  EXPECT_EQ(EncodeSixel(Bitmap()), "");
  // Nothing to draw.
  EXPECT_EQ(EncodeSixel(Make(4, 4, [](int, int) { return g_none; })), "");
}

TEST(SixelTest, Format) {
  const std::string sixel = EncodeSixel(Stripes(9, 13));
  // A Device Control String, with a transparent background.
  EXPECT_EQ(sixel.substr(0, 8), "\x1BP0;1;0q");
  // The raster attributes: square pixels, and the size.
  EXPECT_EQ(sixel.substr(8, 10), "\"1;1;9;13#");
  // The String Terminator.
  EXPECT_EQ(sixel.substr(sixel.size() - 2), "\x1B\\");
  // 13 rows: 3 bands, hence 2 new lines. None after the last one, since it
  // would make a terminal scroll when the image is at the bottom.
  EXPECT_EQ(std::count(sixel.begin(), sixel.end(), '-'), 2);
  EXPECT_NE(sixel[sixel.size() - 3], '-');
  // Only printable characters in between.
  for (size_t i = 2; i < sixel.size() - 2; ++i) {
    EXPECT_GE(sixel[i], ' ');
    EXPECT_LE(sixel[i], '~');
  }
}

// What is encoded is what a terminal decodes.
TEST(SixelTest, RoundTrip) {
  for (const auto& [width, height] : std::vector<std::pair<int, int>>{
           {1, 1}, {6, 6}, {7, 5}, {30, 12}, {31, 13}, {64, 100}}) {
    const Bitmap bitmap = Stripes(width, height);
    const test::Decoded decoded = test::DecodeSixel(EncodeSixel(bitmap));
    ASSERT_TRUE(decoded.valid);
    EXPECT_EQ(decoded.width, width);
    EXPECT_EQ(decoded.height, height);
    EXPECT_EQ(decoded.drawn_width, width);
    EXPECT_EQ(decoded.drawn_height, height);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const uint8_t* pixel = bitmap.data() + (y * width + x) * 4;
        EXPECT_EQ(decoded.at(x, y),
                  (RGBA{pixel[0], pixel[1], pixel[2], pixel[3]}))
            << x << "," << y;
      }
    }
  }
}

// Only the colors in use are sent.
TEST(SixelTest, Colors) {
  test::Decoded decoded = test::DecodeSixel(EncodeSixel(Stripes(9, 6)));
  EXPECT_EQ(decoded.colors, 3);

  decoded = test::DecodeSixel(
      EncodeSixel(Make(8, 8, [](int, int) { return g_gray; })));
  EXPECT_EQ(decoded.colors, 1);
  EXPECT_EQ(decoded.at(3, 3), g_gray);
}

// Transparent pixels are not drawn.
TEST(SixelTest, Transparency) {
  const Bitmap bitmap = Make(12, 12, [](int x, int y) {
    return (x >= 4 && x < 8 && y >= 4 && y < 8) ? g_none : g_red;
  });
  const test::Decoded decoded = test::DecodeSixel(EncodeSixel(bitmap));
  ASSERT_TRUE(decoded.valid);
  for (int y = 0; y < 12; ++y) {
    for (int x = 0; x < 12; ++x) {
      const bool hole = (x >= 4 && x < 8 && y >= 4 && y < 8);
      EXPECT_EQ(decoded.drawn(x, y), !hole);
    }
  }
}

// Partially transparent pixels are either drawn or not, unless there is a
// color to blend them with.
TEST(SixelTest, Alpha) {
  const Bitmap bitmap = Make(2, 1, [](int x, int) {
    return x == 0 ? RGBA{255, 0, 0, 200} : RGBA{255, 0, 0, 51};
  });

  test::Decoded decoded = test::DecodeSixel(EncodeSixel(bitmap));
  EXPECT_EQ(decoded.at(0, 0), g_red);
  EXPECT_FALSE(decoded.drawn(1, 0));

  SixelImage image;
  image.bitmap = bitmap;
  image.dither = SixelDither::None;
  image.background = Color::RGB(0, 0, 255);
  decoded = test::DecodeSixel(EncodeSixel(image));
  ASSERT_TRUE(decoded.drawn(1, 0));
  // 20% of red over blue.
  EXPECT_NEAR(decoded.at(1, 0)[0], 51, 8);
  EXPECT_NEAR(decoded.at(1, 0)[2], 204, 8);
}

// The bitmap can be placed anywhere on a larger canvas.
TEST(SixelTest, Offset) {
  SixelImage image;
  image.bitmap = Stripes(6, 4);
  image.width = 20;
  image.height = 15;
  image.offset_x = 5;
  image.offset_y = 7;
  const test::Decoded decoded = test::DecodeSixel(EncodeSixel(image));
  ASSERT_TRUE(decoded.valid);
  EXPECT_EQ(decoded.width, 20);
  EXPECT_EQ(decoded.height, 15);
  for (int y = 0; y < 15; ++y) {
    for (int x = 0; x < 20; ++x) {
      const bool inside = x >= 5 && x < 11 && y >= 7 && y < 11;
      EXPECT_EQ(decoded.drawn(x, y), inside) << x << "," << y;
    }
  }
  EXPECT_EQ(decoded.at(5, 7), g_red);
  EXPECT_EQ(decoded.at(10, 10), g_blue);
}

// A negative offset crops the bitmap.
TEST(SixelTest, Crop) {
  SixelImage image;
  image.bitmap = Stripes(9, 9);
  image.width = 4;
  image.height = 4;
  image.offset_x = -5;
  image.offset_y = -2;
  const test::Decoded decoded = test::DecodeSixel(EncodeSixel(image));
  ASSERT_TRUE(decoded.valid);
  EXPECT_EQ(decoded.width, 4);
  EXPECT_EQ(decoded.height, 4);
  EXPECT_EQ(decoded.at(0, 0), g_green);  // Column 5.
  EXPECT_EQ(decoded.at(1, 0), g_blue);   // Column 6.
  EXPECT_EQ(decoded.at(3, 3), g_blue);   // Column 8.

  // Entirely out.
  image.offset_x = 10;
  EXPECT_EQ(EncodeSixel(image), "");
}

// The mask punches holes, one cell at a time.
TEST(SixelTest, Mask) {
  SixelImage image;
  image.bitmap = Make(8, 6, [](int, int) { return g_gray; });
  image.mask_columns = 4;
  image.mask_cell_width = 2;
  image.mask_cell_height = 3;
  image.mask = {
      1, 0, 1, 1,  //
      1, 1, 0, 1,  //
  };
  const test::Decoded decoded = test::DecodeSixel(EncodeSixel(image));
  ASSERT_TRUE(decoded.valid);
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 8; ++x) {
      const bool expected = image.mask[(y / 3) * 4 + x / 2] != 0;
      EXPECT_EQ(decoded.drawn(x, y), expected) << x << "," << y;
    }
  }
}

// A picture with more colors than the palette is approximated.
TEST(SixelTest, Quantization) {
  const Bitmap bitmap = Make(64, 64, [](int x, int y) {
    return RGBA{
        static_cast<uint8_t>(x * 4),
        static_cast<uint8_t>(y * 4),
        static_cast<uint8_t>((x + y) * 2),
        255,
    };
  });

  for (const SixelDither dither :
       {SixelDither::None, SixelDither::Ordered, SixelDither::FloydSteinberg}) {
    for (const int colors : {256, 16}) {
      const test::Decoded decoded =
          test::DecodeSixel(EncodeSixel(bitmap, colors, dither));
      ASSERT_TRUE(decoded.valid);
      EXPECT_LE(decoded.colors, colors);
      EXPECT_GT(decoded.colors, colors / 2);

      // Every pixel is drawn, and the picture is close to the original.
      double error = 0.0;
      for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
          ASSERT_TRUE(decoded.drawn(x, y));
          const uint8_t* pixel = bitmap.data() + (y * 64 + x) * 4;
          for (int c = 0; c < 3; ++c) {
            error += std::abs(int(decoded.at(x, y)[size_t(c)]) - int(pixel[c]));
          }
        }
      }
      error /= 64 * 64 * 3;
      EXPECT_LT(error, colors == 256 ? 8.0 : 30.0);
    }
  }
}

TEST(SixelTest, Palette) {
  // A few colors: they are all kept.
  auto palette = SixelPalette::Build(Stripes(9, 9), 256);
  EXPECT_EQ(palette->size(), 3);
  const SixelPalette::RGB color = palette->color(palette->Nearest(250, 10, 5));
  EXPECT_EQ(color.red, 255);
  EXPECT_EQ(color.green, 0);
  EXPECT_EQ(color.blue, 0);

  // The number of colors is bounded.
  const Bitmap gradient = Make(64, 64, [](int x, int y) {
    return RGBA{static_cast<uint8_t>(x * 4), static_cast<uint8_t>(y * 4), 0,
                255};
  });
  EXPECT_LE(SixelPalette::Build(gradient, 256)->size(), 256);
  EXPECT_LE(SixelPalette::Build(gradient, 8)->size(), 8);
  EXPECT_GE(SixelPalette::Build(gradient, 1)->size(), 1);
  EXPECT_LE(SixelPalette::Build(gradient, 100000)->size(), 256);

  // Never empty, even without any pixel to take colors from.
  EXPECT_EQ(SixelPalette::Build(Bitmap(), 16)->size(), 1);
  EXPECT_EQ(SixelPalette::Build(Bitmap(4, 4), 16)->size(), 1);
}

// The palette of a bitmap is computed once.
TEST(SixelTest, PaletteIsRemembered) {
  const Bitmap bitmap = Stripes(9, 9);
  const auto a = SixelPalette::For(bitmap, 256);
  const auto b = SixelPalette::For(bitmap, 256);
  const auto c = SixelPalette::For(bitmap, 16);
  EXPECT_EQ(a.get(), b.get());
  EXPECT_NE(a.get(), c.get());
  EXPECT_NE(a->id(), c->id());
}

}  // namespace ftxui
