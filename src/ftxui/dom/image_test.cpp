// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/image.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/bitmap.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/sixel_test_util.hpp"
#include "ftxui/screen/terminal.hpp"
#include "gtest/gtest.h"

namespace ftxui {

namespace {

using RGBA = std::array<uint8_t, 4>;

Bitmap Solid(int width, int height, RGBA color = {255, 0, 0, 255}) {
  std::vector<uint8_t> pixels;
  for (int i = 0; i < width * height; ++i) {
    pixels.insert(pixels.end(), color.begin(), color.end());
  }
  return Bitmap::FromRGBA(width, height, pixels.data());
}

// Left half red, right half blue.
Bitmap Halves(int width, int height) {
  std::vector<uint8_t> pixels;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const RGBA color =
          x < width / 2 ? RGBA{255, 0, 0, 255} : RGBA{0, 0, 255, 255};
      pixels.insert(pixels.end(), color.begin(), color.end());
    }
  }
  return Bitmap::FromRGBA(width, height, pixels.data());
}

// Where an image ended up, in pixels, relative to the top-left of the screen.
struct Rectangle {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  bool operator==(const Rectangle& other) const {
    return x == other.x && y == other.y && width == other.width &&
           height == other.height;
  }
};
std::ostream& operator<<(std::ostream& os, const Rectangle& r) {
  return os << "{x=" << r.x << ", y=" << r.y << ", width=" << r.width
            << ", height=" << r.height << "}";
}

// The pixels covered by the only graphic of a screen.
Rectangle Pixels(const Screen& screen) {
  if (screen.graphics().size() != 1) {
    return {-1, -1, -1, -1};
  }
  const Screen::Graphic& graphic = screen.graphics()[0];
  return {
      graphic.x * graphic.cell_width + graphic.offset_x,
      graphic.y * graphic.cell_height + graphic.offset_y,
      graphic.bitmap.width(),
      graphic.bitmap.height(),
  };
}

// Render an element on a screen of the given size, in cells.
Screen RenderOn(int width, int height, const Element& element) {
  Screen screen(width, height);
  Render(screen, element);
  return screen;
}

// The requirement of an element.
Requirement Require(const Element& element) {
  element->ComputeRequirement();
  return element->requirement();
}

// The tests use cells of 10x20 pixels, and sixel.
class ImageTest : public testing::Test {
 protected:
  void SetUp() override {
    Terminal::SetCellPixelSize({10, 20});
    Terminal::SetSixelSupport(true);
    Terminal::SetSixelLastRowSupport(true);
    Terminal::SetColorSupport(Terminal::Color::TrueColor);
  }
  void TearDown() override {
    Terminal::SetCellPixelSize({0, 0});
    Terminal::SetSixelSupport(false);
    Terminal::SetSixelLastRowSupport(false);
    SetImageLoadedCallback(nullptr);
    ClearImageCache();
  }
};

}  // namespace

// --- Size requested ----------------------------------------------------------

// An image asks for its original size.
TEST_F(ImageTest, RequirementIsTheOriginalSize) {
  // 100x100 pixels are 10x5 cells of 10x20 pixels.
  Requirement requirement = Require(image(Solid(100, 100)));
  EXPECT_EQ(requirement.min_x, 10);
  EXPECT_EQ(requirement.min_y, 5);

  // Rounded up.
  requirement = Require(image(Solid(101, 101)));
  EXPECT_EQ(requirement.min_x, 11);
  EXPECT_EQ(requirement.min_y, 6);

  // It depends on the size of the cells.
  Terminal::SetCellPixelSize({5, 10});
  requirement = Require(image(Solid(100, 100)));
  EXPECT_EQ(requirement.min_x, 20);
  EXPECT_EQ(requirement.min_y, 10);
}

// By default, an image follows the size of its container, both ways.
TEST_F(ImageTest, RequirementIsFlexible) {
  Requirement requirement = Require(image(Solid(100, 100)));
  EXPECT_EQ(requirement.flex_grow_x, 1);
  EXPECT_EQ(requirement.flex_grow_y, 1);
  EXPECT_EQ(requirement.flex_shrink_x, 1);
  EXPECT_EQ(requirement.flex_shrink_y, 1);

  requirement = Require(image(Solid(100, 100), ImageOptions().Flex(false)));
  EXPECT_EQ(requirement.flex_grow_x, 0);
  EXPECT_EQ(requirement.flex_grow_y, 0);
  EXPECT_EQ(requirement.flex_shrink_x, 0);
  EXPECT_EQ(requirement.flex_shrink_y, 0);

  requirement = Require(image(Solid(100, 100), ImageOptions().Grow(false)));
  EXPECT_EQ(requirement.flex_grow_x, 0);
  EXPECT_EQ(requirement.flex_shrink_x, 1);
}

TEST_F(ImageTest, RequirementWithSize) {
  // Both.
  Requirement requirement =
      Require(image(Solid(100, 100), ImageOptions().Size(30, 4)));
  EXPECT_EQ(requirement.min_x, 30);
  EXPECT_EQ(requirement.min_y, 4);

  // Only the width: the height follows the aspect ratio. A square image of
  // 30 cells, which are 300 pixels, is 15 cells tall.
  requirement = Require(image(Solid(100, 100), ImageOptions().Width(30)));
  EXPECT_EQ(requirement.min_x, 30);
  EXPECT_EQ(requirement.min_y, 15);

  // Only the height.
  requirement = Require(image(Solid(100, 100), ImageOptions().Height(10)));
  EXPECT_EQ(requirement.min_x, 20);
  EXPECT_EQ(requirement.min_y, 10);
}

TEST_F(ImageTest, RequirementWithBounds) {
  // 400x400 pixels are 40x20 cells.
  // The maximal width shrinks the height too.
  Requirement requirement =
      Require(image(Solid(400, 400), ImageOptions().MaxWidth(10)));
  EXPECT_EQ(requirement.min_x, 10);
  EXPECT_EQ(requirement.min_y, 5);

  requirement = Require(image(Solid(400, 400), ImageOptions().MaxHeight(10)));
  EXPECT_EQ(requirement.min_x, 20);
  EXPECT_EQ(requirement.min_y, 10);

  requirement = Require(image(Solid(400, 400), ImageOptions().MaxSize(30, 5)));
  EXPECT_EQ(requirement.min_x, 10);
  EXPECT_EQ(requirement.min_y, 5);

  // The minimal width enlarges the height too.
  requirement = Require(image(Solid(100, 100), ImageOptions().MinWidth(30)));
  EXPECT_EQ(requirement.min_x, 30);
  EXPECT_EQ(requirement.min_y, 15);

  requirement = Require(image(Solid(100, 100), ImageOptions().MinHeight(10)));
  EXPECT_EQ(requirement.min_x, 20);
  EXPECT_EQ(requirement.min_y, 10);

  // Bounds which are already respected change nothing.
  requirement = Require(
      image(Solid(100, 100), ImageOptions().MinSize(2, 2).MaxSize(50, 50)));
  EXPECT_EQ(requirement.min_x, 10);
  EXPECT_EQ(requirement.min_y, 5);
}

TEST_F(ImageTest, RequirementWithMargin) {
  Requirement requirement =
      Require(image(Solid(100, 100), ImageOptions().Margin(1, 2, 3, 4)));
  EXPECT_EQ(requirement.min_x, 10 + 1 + 3);
  EXPECT_EQ(requirement.min_y, 5 + 2 + 4);

  requirement = Require(image(Solid(100, 100), ImageOptions().Margin(2)));
  EXPECT_EQ(requirement.min_x, 14);
  EXPECT_EQ(requirement.min_y, 9);

  requirement = Require(image(Solid(100, 100), ImageOptions().Margin(3, 1)));
  EXPECT_EQ(requirement.min_x, 16);
  EXPECT_EQ(requirement.min_y, 7);
}

// --- Scaling -----------------------------------------------------------------

// The image is scaled up to the space it is given.
TEST_F(ImageTest, ContainScalesUp) {
  // 100x100 pixels, in 40x20 cells, which are 400x400 pixels.
  const Screen screen = RenderOn(40, 20, image(Solid(100, 100)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 400, 400}));
}

// The image is scaled down to the space it is given.
TEST_F(ImageTest, ContainScalesDown) {
  // 1000x1000 pixels, in 10x5 cells, which are 100x100 pixels.
  const Screen screen = RenderOn(10, 5, image(Solid(1000, 1000)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 100}));
}

// The aspect ratio is preserved, and the image is centered.
TEST_F(ImageTest, ContainKeepsTheAspectRatio) {
  // 40x20 cells are 400x400 pixels.
  // A wide image: bars above and below.
  Screen screen = RenderOn(40, 20, image(Solid(200, 100)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 100, 400, 200}));

  // A tall image: bars on the sides.
  screen = RenderOn(40, 20, image(Solid(100, 200)));
  EXPECT_EQ(Pixels(screen), (Rectangle{100, 0, 200, 400}));
}

TEST_F(ImageTest, Cover) {
  // A wide image, in 400x400 pixels: scaled to 800x400. The sides are cropped.
  const Bitmap bitmap = Halves(200, 100);
  const Screen screen =
      RenderOn(40, 20, image(bitmap, ImageOptions().Fit(ImageFit::Cover)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 400, 400}));

  // The center is displayed: half red, half blue.
  const Bitmap& displayed = screen.graphics()[0].bitmap;
  EXPECT_EQ(displayed.data()[0], 255);
  EXPECT_EQ(displayed.data()[2], 0);
  EXPECT_EQ(displayed.data()[399 * 4 + 0], 0);
  EXPECT_EQ(displayed.data()[399 * 4 + 2], 255);
}

// The alignment tells what part of a cropped image is displayed.
TEST_F(ImageTest, CoverAlignment) {
  const Bitmap bitmap = Halves(200, 100);
  // The left part: only red.
  Screen screen = RenderOn(
      40, 20,
      image(bitmap,
            ImageOptions().Fit(ImageFit::Cover).AlignX(ImageAlign::Start)));
  const Bitmap left = screen.graphics()[0].bitmap;
  EXPECT_EQ(left.data()[0], 255);
  EXPECT_EQ(left.data()[390 * 4 + 0], 255);

  // The right part: only blue.
  screen = RenderOn(
      40, 20,
      image(bitmap,
            ImageOptions().Fit(ImageFit::Cover).AlignX(ImageAlign::End)));
  const Bitmap right = screen.graphics()[0].bitmap;
  EXPECT_EQ(right.data()[10 * 4 + 2], 255);
  EXPECT_EQ(right.data()[399 * 4 + 2], 255);
}

TEST_F(ImageTest, Fill) {
  const Screen screen = RenderOn(
      40, 10, image(Solid(100, 100), ImageOptions().Fit(ImageFit::Fill)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 400, 200}));
}

TEST_F(ImageTest, ScaleDown) {
  // Never larger than the original.
  Screen screen = RenderOn(
      40, 20, image(Solid(100, 100), ImageOptions().Fit(ImageFit::ScaleDown)));
  EXPECT_EQ(Pixels(screen), (Rectangle{150, 150, 100, 100}));

  // But smaller.
  screen = RenderOn(
      10, 5, image(Solid(1000, 1000), ImageOptions().Fit(ImageFit::ScaleDown)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 100}));
}

TEST_F(ImageTest, None) {
  // The original size.
  Screen screen = RenderOn(
      40, 20, image(Solid(100, 100), ImageOptions().Fit(ImageFit::None)));
  EXPECT_EQ(Pixels(screen), (Rectangle{150, 150, 100, 100}));

  // Cropped when it is too large. This is the same bitmap: not scaled.
  screen = RenderOn(
      10, 5, image(Solid(1000, 1000), ImageOptions().Fit(ImageFit::None)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 100}));
}

TEST_F(ImageTest, MaxScale) {
  // At most twice its size.
  Screen screen =
      RenderOn(40, 20, image(Solid(100, 100), ImageOptions().MaxScale(2.F)));
  EXPECT_EQ(Pixels(screen), (Rectangle{100, 100, 200, 200}));

  // At least four times its size, even if it doesn't fit.
  screen =
      RenderOn(20, 10, image(Solid(100, 100), ImageOptions().MinScale(4.F)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 200, 200}));
}

TEST_F(ImageTest, Filter) {
  // Two pixels, scaled 100 times.
  const Bitmap bitmap = Halves(2, 1);
  Screen screen = RenderOn(
      20, 5, image(bitmap, ImageOptions().Filter(BitmapFilter::Nearest)));
  const Bitmap nearest = screen.graphics()[0].bitmap;
  ASSERT_EQ(nearest.width(), 200);
  EXPECT_EQ(nearest.data()[99 * 4 + 0], 255);  // Still red.
  EXPECT_EQ(nearest.data()[100 * 4 + 0], 0);   // Already blue.

  screen = RenderOn(20, 5, image(bitmap));
  const Bitmap smooth = screen.graphics()[0].bitmap;
  ASSERT_EQ(smooth.width(), 200);
  EXPECT_GT(smooth.data()[100 * 4 + 0], 100);  // In between.
  EXPECT_LT(smooth.data()[100 * 4 + 0], 155);
}

// --- Position ----------------------------------------------------------------

TEST_F(ImageTest, Alignment) {
  // 100x100 pixels at its original size, in 400x400 pixels.
  auto at = [](ImageAlign x, ImageAlign y) {
    return Pixels(
        RenderOn(40, 20,
                 image(Solid(100, 100),
                       ImageOptions().Fit(ImageFit::None).Align(x, y))));
  };
  EXPECT_EQ(at(ImageAlign::Start, ImageAlign::Start),
            (Rectangle{0, 0, 100, 100}));
  EXPECT_EQ(at(ImageAlign::Center, ImageAlign::Start),
            (Rectangle{150, 0, 100, 100}));
  EXPECT_EQ(at(ImageAlign::End, ImageAlign::Start),
            (Rectangle{300, 0, 100, 100}));
  EXPECT_EQ(at(ImageAlign::Start, ImageAlign::Center),
            (Rectangle{0, 150, 100, 100}));
  EXPECT_EQ(at(ImageAlign::Center, ImageAlign::Center),
            (Rectangle{150, 150, 100, 100}));
  EXPECT_EQ(at(ImageAlign::End, ImageAlign::End),
            (Rectangle{300, 300, 100, 100}));
}

// Centering is done at the pixel, not at the cell.
TEST_F(ImageTest, CenteringIsPixelAccurate) {
  // 30x30 pixels in 100x100 pixels: 35 pixels on each side.
  const Screen screen =
      RenderOn(10, 5, image(Solid(30, 30), ImageOptions().Fit(ImageFit::None)));
  EXPECT_EQ(Pixels(screen), (Rectangle{35, 35, 30, 30}));

  // The graphic starts at the cell containing its first pixel.
  ASSERT_EQ(screen.graphics().size(), 1U);
  const Screen::Graphic& graphic = screen.graphics()[0];
  EXPECT_EQ(graphic.x, 3);
  EXPECT_EQ(graphic.y, 1);
  EXPECT_EQ(graphic.offset_x, 5);
  EXPECT_EQ(graphic.offset_y, 15);
  EXPECT_EQ(graphic.width, 4);   // The cells 3 to 6.
  EXPECT_EQ(graphic.height, 3);  // The cells 1 to 3.
}

TEST_F(ImageTest, Margin) {
  // 40x20 cells, minus the margin: 30x10 cells, which are 300x200 pixels.
  const Screen screen =
      RenderOn(40, 20,
               image(Solid(100, 100),
                     ImageOptions().Fit(ImageFit::Fill).Margin(4, 3, 6, 7)));
  EXPECT_EQ(Pixels(screen), (Rectangle{40, 60, 300, 200}));

  // The cells of the margin are left untouched.
  EXPECT_EQ(screen.CellAt(3, 3).graphic, 0);
  EXPECT_EQ(screen.CellAt(4, 2).graphic, 0);
  EXPECT_EQ(screen.CellAt(4, 3).graphic, 1);
  EXPECT_EQ(screen.CellAt(33, 12).graphic, 1);
  EXPECT_EQ(screen.CellAt(34, 12).graphic, 0);
  EXPECT_EQ(screen.CellAt(33, 13).graphic, 0);
}

// A margin larger than the element leaves nothing to draw.
TEST_F(ImageTest, MarginTooLarge) {
  const Screen screen =
      RenderOn(4, 2, image(Solid(100, 100), ImageOptions().Margin(3)));
  EXPECT_TRUE(screen.graphics().empty());
}

// The maximal size bounds the image, wherever the element is stretched to.
TEST_F(ImageTest, MaxSize) {
  auto options = [] {
    return ImageOptions().Fit(ImageFit::Fill).MaxSize(10, 5);
  };
  // Centered.
  Screen screen = RenderOn(40, 20, image(Solid(100, 100), options()));
  EXPECT_EQ(Pixels(screen), (Rectangle{150, 160, 100, 100}));

  // Top left.
  screen = RenderOn(40, 20,
                    image(Solid(100, 100), options().Align(ImageAlign::Start,
                                                           ImageAlign::Start)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 100}));

  // Bottom right.
  screen = RenderOn(40, 20,
                    image(Solid(100, 100),
                          options().Align(ImageAlign::End, ImageAlign::End)));
  EXPECT_EQ(Pixels(screen), (Rectangle{300, 300, 100, 100}));
}

// --- Layout ------------------------------------------------------------------

// The image shares the space with the other elements.
TEST_F(ImageTest, InsideBoxes) {
  const Screen screen = RenderOn(
      40, 20,
      vbox({
          text("title"),
          hbox({
              text("left") | size(WIDTH, EQUAL, 10),
              image(Solid(100, 100), ImageOptions().Fit(ImageFit::Fill)),
          }) | flex,
          text("footer"),
      }));
  EXPECT_EQ(Pixels(screen), (Rectangle{100, 20, 300, 360}));
}

// The usual decorators apply.
TEST_F(ImageTest, Decorators) {
  // A border takes one cell on each side.
  Screen screen = RenderOn(
      40, 20,
      image(Solid(100, 100), ImageOptions().Fit(ImageFit::Fill)) | border);
  EXPECT_EQ(Pixels(screen), (Rectangle{10, 20, 380, 360}));

  // A fixed size.
  screen = RenderOn(
      40, 20,
      vbox({
          hbox({
              image(Solid(100, 100), ImageOptions().Fit(ImageFit::Fill)) |
                  size(WIDTH, EQUAL, 12) | size(HEIGHT, EQUAL, 3),
              filler(),
          }),
          filler(),
      }));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 120, 60}));

  // Centered, at its original size.
  screen = RenderOn(
      40, 20, image(Solid(100, 100), ImageOptions().Flex(false)) | center);
  EXPECT_EQ(Pixels(screen), (Rectangle{150, 140, 100, 100}));
}

// Without `grow`, an image is not larger than what it asks for.
TEST_F(ImageTest, NotGrowing) {
  const Screen screen =
      RenderOn(40, 20,
               vbox({
                   hbox({
                       image(Solid(100, 100), ImageOptions().Grow(false)),
                       text("next"),
                   }),
                   text("below"),
               }));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 100}));
  EXPECT_EQ(screen.at(10, 0), "n");
  EXPECT_EQ(screen.at(0, 5), "b");
}

// An image which is given less width than it asked for needs less height too.
// What follows it must not be separated from it by a blank.
TEST_F(ImageTest, ShrinkingKeepsTheAspectRatio) {
  // 400x400 pixels are 40x20 cells. In 20 columns, only 10 rows are needed.
  Screen screen =
      RenderOn(20, 30,
               vbox({
                   image(Solid(400, 400), ImageOptions().Grow(false)),
                   text("below"),
               }));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 200, 200}));
  EXPECT_EQ(screen.at(0, 10), "b");

  // The same along the other axis: in 5 rows, only 10 columns are needed.
  screen = RenderOn(60, 5,
                    hbox({
                        image(Solid(400, 400), ImageOptions().Grow(false)),
                        text("next"),
                    }));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 100}));
  EXPECT_EQ(screen.at(10, 0), "n");
}

// An image inside a frame is clipped.
TEST_F(ImageTest, Clipped) {
  const Screen screen =
      RenderOn(20, 5,
               vbox({
                   text("first"),
                   image(Solid(200, 400), ImageOptions().Flex(false)),
               }) | yframe);
  // The image is 20 rows tall. Only 4 are visible.
  ASSERT_EQ(screen.graphics().size(), 1U);
  int cells = 0;
  for (int y = 0; y < 5; ++y) {
    cells += screen.CellAt(0, y).graphic != 0;
  }
  EXPECT_EQ(cells, 4);
  const std::vector<std::string> sixels = test::FindSixels(screen.ToString());
  ASSERT_EQ(sixels.size(), 1U);
  EXPECT_EQ(test::DecodeSixel(sixels[0]).height, 80);
}

// What is drawn above an image is displayed.
TEST_F(ImageTest, Overlay) {
  const Screen screen = RenderOn(10, 5,
                                 dbox({
                                     image(Solid(100, 100)),
                                     text("hello") | center,
                                 }));
  EXPECT_EQ(screen.at(2, 2), "h");
  EXPECT_EQ(screen.CellAt(1, 2).character, Cell::GraphicCharacter());
  const std::vector<std::string> sixels = test::FindSixels(screen.ToString());
  ASSERT_EQ(sixels.size(), 1U);
  const test::Decoded decoded = test::DecodeSixel(sixels[0]);
  EXPECT_TRUE(decoded.drawn(15, 50));   // Left of the text.
  EXPECT_FALSE(decoded.drawn(25, 50));  // Below the text.
  EXPECT_TRUE(decoded.drawn(75, 50));   // Right of the text.
}

// --- Rendering ---------------------------------------------------------------

// The same bitmap is used from one frame to the next: nothing is computed
// again, and nothing is sent again.
TEST_F(ImageTest, ScalingIsCached) {
  const Bitmap bitmap = Solid(100, 100);
  const Screen a = RenderOn(40, 20, image(bitmap));
  const Screen b = RenderOn(40, 20, image(bitmap));
  const Screen c = RenderOn(30, 20, image(bitmap));
  ASSERT_EQ(a.graphics().size(), 1U);
  EXPECT_EQ(a.graphics()[0].bitmap.id(), b.graphics()[0].bitmap.id());
  EXPECT_NE(a.graphics()[0].bitmap.id(), c.graphics()[0].bitmap.id());
  EXPECT_EQ(a.graphics()[0].palette.get(), c.graphics()[0].palette.get());
}

TEST_F(ImageTest, SixelOptions) {
  const Screen screen =
      RenderOn(10, 5,
               image(Solid(100, 100), ImageOptions()
                                          .Colors(16)
                                          .Dither(SixelDither::None)
                                          .Background(Color::RGB(1, 2, 3))));
  ASSERT_EQ(screen.graphics().size(), 1U);
  const Screen::Graphic& graphic = screen.graphics()[0];
  EXPECT_EQ(graphic.colors, 16);
  EXPECT_EQ(graphic.dither, SixelDither::None);
  EXPECT_EQ(graphic.background, Color::RGB(1, 2, 3));
  EXPECT_EQ(graphic.cell_width, 10);
  EXPECT_EQ(graphic.cell_height, 20);
}

// Without sixel, an image is drawn with half blocks: two pixels per cell.
TEST_F(ImageTest, Blocks) {
  Terminal::SetSixelSupport(false);
  // Top half red, bottom half blue.
  std::vector<uint8_t> pixels;
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < 100; ++x) {
      const RGBA color = y < 50 ? RGBA{255, 0, 0, 255} : RGBA{0, 0, 255, 255};
      pixels.insert(pixels.end(), color.begin(), color.end());
    }
  }
  // In 10x1 cells: one row of 10x2 "pixels".
  const Screen screen =
      RenderOn(10, 1,
               image(Bitmap::FromRGBA(100, 100, pixels.data()),
                     ImageOptions().Fit(ImageFit::Fill)));
  EXPECT_TRUE(screen.graphics().empty());
  for (int x = 0; x < 10; ++x) {
    const Cell& cell = screen.CellAt(x, 0);
    EXPECT_EQ(cell.character, "▀");
    EXPECT_EQ(cell.foreground_color, Color::RGB(255, 0, 0));
    EXPECT_EQ(cell.background_color, Color::RGB(0, 0, 255));
  }
}

// The layout doesn't depend on what the image is drawn with.
TEST_F(ImageTest, BlocksLayout) {
  Terminal::SetSixelSupport(false);
  // 100x100 pixels, in 40x20 cells: a square of 20 columns, in the middle.
  const Screen screen = RenderOn(40, 20, image(Solid(100, 200)));
  EXPECT_EQ(screen.CellAt(9, 10).character, "");
  EXPECT_EQ(screen.CellAt(10, 0).character, "▀");
  EXPECT_EQ(screen.CellAt(29, 19).character, "▀");
  EXPECT_EQ(screen.CellAt(30, 10).character, "");
}

// Half a cell can be left transparent.
TEST_F(ImageTest, BlocksTransparency) {
  Terminal::SetSixelSupport(false);
  // 10x30 pixels at the top of 10x40 pixels: one cell and a half.
  const Screen screen = RenderOn(
      1, 2,
      image(Solid(10, 30),
            ImageOptions().Fit(ImageFit::None).AlignY(ImageAlign::Start)));
  EXPECT_EQ(screen.CellAt(0, 0).character, "▀");
  EXPECT_EQ(screen.CellAt(0, 0).background_color, Color::RGB(255, 0, 0));
  EXPECT_EQ(screen.CellAt(0, 1).character, "▀");
  EXPECT_EQ(screen.CellAt(0, 1).foreground_color, Color::RGB(255, 0, 0));
  EXPECT_EQ(screen.CellAt(0, 1).background_color, Color());
}

TEST_F(ImageTest, Renderer) {
  // Forced, whatever the terminal supports.
  Terminal::SetSixelSupport(false);
  Screen screen = RenderOn(
      10, 5,
      image(Solid(100, 100), ImageOptions().Renderer(ImageRenderer::Sixel)));
  EXPECT_EQ(screen.graphics().size(), 1U);

  Terminal::SetSixelSupport(true);
  screen = RenderOn(
      10, 5,
      image(Solid(100, 100), ImageOptions().Renderer(ImageRenderer::Blocks)));
  EXPECT_TRUE(screen.graphics().empty());
  EXPECT_EQ(screen.CellAt(0, 0).character, "▀");
}

// --- Loading -----------------------------------------------------------------

namespace {
// Write a 4x2 image to a file: the PPM format is plain enough.
std::string WriteImage(const std::string& name) {
  const std::string path = testing::TempDir() + name;
  std::ofstream file(path, std::ios::binary);
  file << "P6\n4 2\n255\n";
  for (int i = 0; i < 8; ++i) {
    file << static_cast<char>(0) << static_cast<char>(-1)
         << static_cast<char>(0);
  }
  return path;
}
}  // namespace

TEST_F(ImageTest, LoadFile) {
  const std::string path = WriteImage("ftxui_image_test_load.ppm");
  EXPECT_EQ(GetImageStatus(path), ImageStatus::Unknown);

  // 4x2 pixels are a single cell.
  const Element element = image(path, ImageOptions().Flex(false));
  const Requirement requirement = Require(element);
  EXPECT_EQ(requirement.min_x, 1);
  EXPECT_EQ(requirement.min_y, 1);
  EXPECT_EQ(GetImageStatus(path), ImageStatus::Ready);

  const Screen screen = RenderOn(10, 5, image(path));
  ASSERT_EQ(screen.graphics().size(), 1U);
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 25, 100, 50}));
  EXPECT_EQ(screen.graphics()[0].bitmap.data()[1], 255);  // Green.

  std::remove(path.c_str());
}

// The file is decoded once, whatever the number of elements displaying it.
TEST_F(ImageTest, LoadFileOnce) {
  const std::string path = WriteImage("ftxui_image_test_once.ppm");
  Require(image(path));
  std::remove(path.c_str());

  // The file is gone, and the image is still there.
  const Screen screen = RenderOn(10, 5, image(path));
  EXPECT_EQ(screen.graphics().size(), 1U);

  // Until it is asked to be loaded again.
  ReloadImage(path);
  EXPECT_EQ(GetImageStatus(path), ImageStatus::Unknown);
  EXPECT_TRUE(RenderOn(10, 5, image(path)).graphics().empty());
  EXPECT_EQ(GetImageStatus(path), ImageStatus::Failed);
}

TEST_F(ImageTest, LoadFailure) {
  const std::string path = testing::TempDir() + "ftxui_image_missing.png";

  // The name of the file is displayed.
  Screen screen = RenderOn(30, 3, image(path));
  EXPECT_TRUE(screen.graphics().empty());
  EXPECT_EQ(screen.ToString(),
            "                              \r\n"
            "   \x1B[2mftxui_image_missing.png\x1B[22m    \r\n"
            "                              ");

  std::string error;
  EXPECT_EQ(GetImageStatus(path, &error), ImageStatus::Failed);
  EXPECT_FALSE(error.empty());

  // Or the alternative text.
  screen = RenderOn(30, 3, image(path, ImageOptions().Alt("no image")));
  EXPECT_EQ(screen.ToString(),
            "                              \r\n"
            "           \x1B[2mno image\x1B[22m           \r\n"
            "                              ");

  // An image which is not loaded asks for no space.
  const Requirement requirement = Require(image(path));
  EXPECT_EQ(requirement.min_x, 0);
  EXPECT_EQ(requirement.min_y, 0);
  // Unless it is told how much.
  EXPECT_EQ(Require(image(path, ImageOptions().Size(8, 4))).min_x, 8);
}

TEST_F(ImageTest, EmptyBitmap) {
  const Screen screen = RenderOn(10, 3, image(Bitmap()));
  EXPECT_TRUE(screen.graphics().empty());
  EXPECT_EQ(screen.ToString(),
            "          \r\n"
            "          \r\n"
            "          ");
}

// With something to tell when an image is loaded, loading is done in the
// background.
TEST_F(ImageTest, LoadInBackground) {
  const std::string path = WriteImage("ftxui_image_test_async.ppm");
  std::atomic<int> loaded{0};
  SetImageLoadedCallback([&] { loaded++; });

  // The first frame is drawn without the image.
  Screen screen = RenderOn(10, 5, image(path));
  EXPECT_TRUE(screen.graphics().empty());
  const ImageStatus status = GetImageStatus(path);
  EXPECT_TRUE(status == ImageStatus::Loading || status == ImageStatus::Ready);

  // Wait for the callback.
  for (int i = 0; i < 500 && loaded == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_EQ(loaded, 1);
  EXPECT_EQ(GetImageStatus(path), ImageStatus::Ready);

  // The next frame is drawn with it.
  screen = RenderOn(10, 5, image(path));
  EXPECT_EQ(screen.graphics().size(), 1U);

  // Unless asked not to, the next images wait as well.
  const std::string other = WriteImage("ftxui_image_test_sync.ppm");
  screen = RenderOn(10, 5, image(other, ImageOptions().Async(false)));
  EXPECT_EQ(screen.graphics().size(), 1U);

  SetImageLoadedCallback(nullptr);
  std::remove(path.c_str());
  std::remove(other.c_str());
}

// Several images, loaded by several threads.
TEST_F(ImageTest, LoadManyInBackground) {
  constexpr int count = 12;
  std::vector<std::string> paths;
  for (int i = 0; i < count; ++i) {
    paths.push_back(
        WriteImage("ftxui_image_test_many_" + std::to_string(i) + ".ppm"));
  }
  std::atomic<int> loaded{0};
  SetImageLoadedCallback([&] { loaded++; });
  SetMaxConcurrentImageLoads(3);

  Elements elements;
  for (const std::string& path : paths) {
    elements.push_back(image(path));
  }
  RenderOn(60, 5, hbox(elements));

  for (int i = 0; i < 500 && loaded < count; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_EQ(loaded, count);

  elements.clear();
  for (const std::string& path : paths) {
    elements.push_back(image(path));
  }
  EXPECT_EQ(RenderOn(60, 5, hbox(elements)).graphics().size(),
            static_cast<size_t>(count));

  SetImageLoadedCallback(nullptr);
  SetMaxConcurrentImageLoads(4);
  for (const std::string& path : paths) {
    std::remove(path.c_str());
  }
}

// --- ftxui-image-view --------------------------------------------------------

// The API of the libraries this replaces.
TEST_F(ImageTest, ImageView) {
  const std::string path = WriteImage("ftxui_image_test_view.ppm");

  // Same layout as with them: the original size, not growing, top-left.
  const Element element = image_view(path);
  const Requirement requirement = Require(element);
  EXPECT_EQ(requirement.min_x, 1);
  EXPECT_EQ(requirement.min_y, 1);
  EXPECT_EQ(requirement.flex_grow_x, 0);
  EXPECT_EQ(requirement.flex_grow_y, 0);

  // But it is scaled up when it is given more space: 4x2 pixels in 100x100.
  Screen screen = RenderOn(10, 5, image_view(path) | flex);
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 50}));

  // And down: in a single cell of 10x20 pixels.
  screen = RenderOn(1, 1, image_view(path));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 10, 5}));

  // The options are available.
  screen =
      RenderOn(10, 5, image_view(path, ImageOptions().Fit(ImageFit::Fill)));
  EXPECT_EQ(Pixels(screen), (Rectangle{0, 0, 100, 100}));

  // The functions tuning the caches.
  setImageCacheMaxSize(10);
  setImageResizeCacheMaxSize(10);
  setImageCharCacheMaxSize(10);
  setMaxConcurrentImageLoads(2);
  setOnImageLoadedCallback([] {});
  setOnImageLoadedCallback(nullptr);
  SetImageCacheSize(64);
  SetMaxConcurrentImageLoads(4);
  setImageResizeCacheMaxSize(512);

  std::remove(path.c_str());
}

}  // namespace ftxui
