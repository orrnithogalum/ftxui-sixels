// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/screen/bitmap.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/sixel_test_util.hpp"
#include "ftxui/screen/terminal.hpp"
#include "gtest/gtest.h"

namespace ftxui {

namespace {

using RGBA = std::array<uint8_t, 4>;
constexpr RGBA g_red = {255, 0, 0, 255};

Bitmap Red(int width, int height) {
  std::vector<uint8_t> pixels;
  for (int i = 0; i < width * height; ++i) {
    pixels.insert(pixels.end(), g_red.begin(), g_red.end());
  }
  return Bitmap::FromRGBA(width, height, pixels.data());
}

// A graphic covering `width` x `height` cells of 10x20 pixels.
Screen::Graphic Graphic(int x, int y, int width, int height) {
  Screen::Graphic graphic;
  graphic.x = x;
  graphic.y = y;
  graphic.width = width;
  graphic.height = height;
  graphic.cell_width = 10;
  graphic.cell_height = 20;
  graphic.bitmap = Red(width * 10, height * 20);
  return graphic;
}

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

class GraphicsTest : public testing::Test {
 protected:
  void SetUp() override { Terminal::SetSixelLastRowSupport(true); }
  void TearDown() override { Terminal::SetSixelLastRowSupport(false); }
};

}  // namespace

TEST_F(GraphicsTest, NoGraphic) {
  Screen screen(4, 2);
  EXPECT_TRUE(screen.graphics().empty());
  EXPECT_TRUE(test::FindSixels(screen.ToString()).empty());

  // Invalid graphics are ignored.
  screen.DrawGraphic(Screen::Graphic());
  Screen::Graphic no_bitmap = Graphic(0, 0, 2, 2);
  no_bitmap.bitmap = Bitmap();
  screen.DrawGraphic(no_bitmap);
  Screen::Graphic no_cell_size = Graphic(0, 0, 2, 2);
  no_cell_size.cell_width = 0;
  screen.DrawGraphic(no_cell_size);
  EXPECT_TRUE(screen.graphics().empty());

  // Out of the screen.
  screen.DrawGraphic(Graphic(10, 10, 2, 2));
  EXPECT_TRUE(screen.graphics().empty());
}

TEST_F(GraphicsTest, DrawGraphicMarksTheCells) {
  Screen screen(6, 4);
  screen.DrawGraphic(Graphic(1, 1, 3, 2));
  ASSERT_EQ(screen.graphics().size(), 1U);
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 6; ++x) {
      const bool inside = x >= 1 && x <= 3 && y >= 1 && y <= 2;
      EXPECT_EQ(screen.CellAt(x, y).graphic, inside ? 1 : 0);
      EXPECT_EQ(screen.CellAt(x, y).character == Cell::GraphicCharacter(),
                inside);
    }
  }

  screen.Clear();
  EXPECT_TRUE(screen.graphics().empty());
  EXPECT_EQ(screen.CellAt(1, 1).graphic, 0);
}

TEST_F(GraphicsTest, ToString) {
  Screen screen(6, 4);
  screen.at(0, 0) = "a";
  screen.DrawGraphic(Graphic(1, 1, 3, 2));
  const std::string output = screen.ToString();

  // The text: the cells of the graphic are blank.
  EXPECT_EQ(output.substr(0, 30),
            "a     \r\n"
            "      \r\n"
            "      \r\n"
            "      ");

  // Then the graphic. The cursor is saved, moved to its top-left cell: 2 rows
  // up, 1 column right, and restored afterward.
  const std::vector<std::string> sixels = test::FindSixels(output);
  ASSERT_EQ(sixels.size(), 1U);
  EXPECT_TRUE(Contains(output, "\0337\r\x1B[2A\x1B[1C" + sixels[0] + "\0338"));

  const test::Decoded decoded = test::DecodeSixel(sixels[0]);
  ASSERT_TRUE(decoded.valid);
  EXPECT_EQ(decoded.width, 30);
  EXPECT_EQ(decoded.height, 40);
  EXPECT_EQ(decoded.at(0, 0), g_red);
  EXPECT_EQ(decoded.at(29, 39), g_red);
}

// The bitmap is placed inside the cells, at the pixel.
TEST_F(GraphicsTest, Offset) {
  Screen screen(6, 4);
  Screen::Graphic graphic = Graphic(0, 0, 3, 2);
  graphic.bitmap = Red(12, 8);
  graphic.offset_x = 9;
  graphic.offset_y = 16;
  screen.DrawGraphic(graphic);

  const std::vector<std::string> sixels = test::FindSixels(screen.ToString());
  ASSERT_EQ(sixels.size(), 1U);
  const test::Decoded decoded = test::DecodeSixel(sixels[0]);
  ASSERT_TRUE(decoded.valid);
  EXPECT_EQ(decoded.width, 30);
  EXPECT_EQ(decoded.height, 40);
  for (int y = 0; y < 40; ++y) {
    for (int x = 0; x < 30; ++x) {
      const bool inside = x >= 9 && x < 21 && y >= 16 && y < 24;
      EXPECT_EQ(decoded.drawn(x, y), inside) << x << "," << y;
    }
  }
}

// What is drawn over a graphic punches a hole in it.
TEST_F(GraphicsTest, Hole) {
  Screen screen(6, 4);
  screen.DrawGraphic(Graphic(0, 0, 4, 3));
  screen.at(1, 1) = "X";
  const std::string output = screen.ToString();
  EXPECT_TRUE(Contains(output, "X"));

  const std::vector<std::string> sixels = test::FindSixels(output);
  ASSERT_EQ(sixels.size(), 1U);
  const test::Decoded decoded = test::DecodeSixel(sixels[0]);
  ASSERT_TRUE(decoded.valid);
  EXPECT_EQ(decoded.width, 40);
  EXPECT_EQ(decoded.height, 60);
  for (int y = 0; y < 60; ++y) {
    for (int x = 0; x < 40; ++x) {
      const bool hole = x >= 10 && x < 20 && y >= 20 && y < 40;
      EXPECT_EQ(decoded.drawn(x, y), !hole) << x << "," << y;
    }
  }
}

// Only what remains of the graphic is sent.
TEST_F(GraphicsTest, CoveredSides) {
  Screen screen(6, 4);
  screen.DrawGraphic(Graphic(0, 0, 4, 3));
  for (int y = 0; y < 3; ++y) {
    screen.at(0, y) = "|";  // The left column.
  }
  for (int x = 0; x < 4; ++x) {
    screen.at(x, 0) = "-";  // The top row.
  }
  const std::string output = screen.ToString();
  const std::vector<std::string> sixels = test::FindSixels(output);
  ASSERT_EQ(sixels.size(), 1U);
  const test::Decoded decoded = test::DecodeSixel(sixels[0]);
  EXPECT_EQ(decoded.width, 30);
  EXPECT_EQ(decoded.height, 40);
  // Drawn from the cell (1,1).
  EXPECT_TRUE(Contains(output, "\0337\r\x1B[2A\x1B[1C" + sixels[0]));

  // Nothing remains.
  Screen covered(6, 4);
  covered.DrawGraphic(Graphic(0, 0, 2, 1));
  covered.at(0, 0) = "a";
  covered.at(1, 0) = "b";
  EXPECT_TRUE(test::FindSixels(covered.ToString()).empty());
}

// A graphic is clipped by the stencil, as text is.
TEST_F(GraphicsTest, Stencil) {
  Screen screen(8, 6);
  screen.stencil = Box{2, 3, 1, 2};
  screen.DrawGraphic(Graphic(0, 0, 8, 6));
  screen.stencil = Box{0, 7, 0, 5};

  int cells = 0;
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 8; ++x) {
      cells += screen.CellAt(x, y).graphic != 0;
    }
  }
  EXPECT_EQ(cells, 4);

  const std::vector<std::string> sixels = test::FindSixels(screen.ToString());
  ASSERT_EQ(sixels.size(), 1U);
  const test::Decoded decoded = test::DecodeSixel(sixels[0]);
  EXPECT_EQ(decoded.width, 20);
  EXPECT_EQ(decoded.height, 40);
}

// A graphic partly out of the screen is cropped.
TEST_F(GraphicsTest, Cropped) {
  Screen screen(4, 3);
  screen.DrawGraphic(Graphic(-2, -1, 4, 3));
  const std::vector<std::string> sixels = test::FindSixels(screen.ToString());
  ASSERT_EQ(sixels.size(), 1U);
  const test::Decoded decoded = test::DecodeSixel(sixels[0]);
  EXPECT_EQ(decoded.width, 20);
  EXPECT_EQ(decoded.height, 40);
  EXPECT_EQ(decoded.at(0, 0), g_red);
}

// The latest graphic is above.
TEST_F(GraphicsTest, Overlap) {
  Screen screen(6, 4);
  screen.DrawGraphic(Graphic(0, 0, 4, 2));
  screen.DrawGraphic(Graphic(2, 0, 4, 2));
  const std::vector<std::string> sixels = test::FindSixels(screen.ToString());
  ASSERT_EQ(sixels.size(), 2U);
  EXPECT_EQ(test::DecodeSixel(sixels[0]).width, 20);  // What is left.
  EXPECT_EQ(test::DecodeSixel(sixels[1]).width, 40);
}

// A graphic is sent once.
TEST_F(GraphicsTest, Incremental) {
  const Bitmap bitmap = Red(30, 40);
  Screen screen(6, 4);
  auto draw = [&](int x, const std::string& text) {
    screen.Clear();
    Screen::Graphic graphic = Graphic(x, 1, 3, 2);
    graphic.bitmap = bitmap;
    screen.DrawGraphic(graphic);
    screen.at(5, 3) = text;
    std::string output;
    const bool changed = screen.ToStringIncremental(output);
    return std::make_pair(changed, output);
  };

  // First frame: the graphic is sent.
  auto [changed_1, output_1] = draw(1, "a");
  EXPECT_TRUE(changed_1);
  EXPECT_EQ(test::FindSixels(output_1).size(), 1U);

  // Second frame: only the text changed. The graphic is not sent, and its
  // cells are skipped, by moving the cursor 3 cells forward.
  auto [changed_2, output_2] = draw(1, "b");
  EXPECT_FALSE(changed_2);
  EXPECT_TRUE(test::FindSixels(output_2).empty());
  EXPECT_TRUE(Contains(output_2, " \x1B[3C  \r\n"));
  EXPECT_TRUE(Contains(output_2, "b"));

  // Third frame: the graphic moved. It is sent again, and the cells are
  // printed, which erases the previous one.
  auto [changed_3, output_3] = draw(2, "b");
  EXPECT_TRUE(changed_3);
  EXPECT_EQ(test::FindSixels(output_3).size(), 1U);
  EXPECT_FALSE(Contains(output_3, "\x1B[3C"));

  // Fourth frame: nothing changed.
  auto [changed_4, output_4] = draw(2, "b");
  EXPECT_FALSE(changed_4);
  EXPECT_TRUE(test::FindSixels(output_4).empty());

  // Fifth frame: the terminal lost what it displayed.
  screen.ResetGraphics();
  auto [changed_5, output_5] = draw(2, "b");
  EXPECT_TRUE(changed_5);
  EXPECT_EQ(test::FindSixels(output_5).size(), 1U);

  // Sixth frame: the graphic is gone. This is a change too.
  screen.Clear();
  std::string output_6;
  EXPECT_TRUE(screen.ToStringIncremental(output_6));
  EXPECT_TRUE(test::FindSixels(output_6).empty());
  std::string output_7;
  EXPECT_FALSE(screen.ToStringIncremental(output_7));
}

// A graphic is sent again when what is behind, or above, changes.
TEST_F(GraphicsTest, IncrementalSurroundings) {
  const Bitmap bitmap = Red(30, 40);
  Screen screen(6, 4);
  auto draw = [&](Color background, bool hole) {
    screen.Clear();
    Screen::Graphic graphic = Graphic(1, 1, 3, 2);
    graphic.bitmap = bitmap;
    screen.DrawGraphic(graphic);
    screen.CellAt(2, 2).background_color = background;
    if (hole) {
      screen.at(3, 1) = "X";
    }
    std::string output;
    screen.ToStringIncremental(output);
    return test::FindSixels(output).size();
  };

  EXPECT_EQ(draw(Color::Default, false), 1U);
  EXPECT_EQ(draw(Color::Default, false), 0U);
  EXPECT_EQ(draw(Color::Blue, false), 1U);  // Behind.
  EXPECT_EQ(draw(Color::Blue, false), 0U);
  EXPECT_EQ(draw(Color::Blue, true), 1U);  // Above.
  EXPECT_EQ(draw(Color::Blue, true), 0U);
  EXPECT_EQ(draw(Color::Blue, false), 1U);
}

// The pixels are part of what identifies a graphic.
TEST_F(GraphicsTest, IncrementalContent) {
  Screen screen(6, 4);
  auto draw = [&](const Bitmap& bitmap) {
    screen.Clear();
    Screen::Graphic graphic = Graphic(1, 1, 3, 2);
    graphic.bitmap = bitmap;
    screen.DrawGraphic(graphic);
    std::string output;
    screen.ToStringIncremental(output);
    return test::FindSixels(output).size();
  };
  Bitmap bitmap = Red(30, 40);
  EXPECT_EQ(draw(bitmap), 1U);
  EXPECT_EQ(draw(bitmap), 0U);
  bitmap.MutableData()[1] = 255;
  EXPECT_EQ(draw(bitmap), 1U);
  EXPECT_EQ(draw(bitmap), 0U);
}

// Many terminals scroll when an image reaches their last row. Unless the
// terminal is known to cope, graphics stay away from the last row.
TEST_F(GraphicsTest, LastRow) {
  // Supported: the cursor is asked to stay on the last row of the image, for
  // the time of drawing.
  {
    Screen screen(4, 3);
    screen.DrawGraphic(Graphic(0, 0, 4, 3));
    const std::string output = screen.ToString();
    const std::vector<std::string> sixels = test::FindSixels(output);
    ASSERT_EQ(sixels.size(), 1U);
    EXPECT_EQ(test::DecodeSixel(sixels[0]).height, 60);
    const size_t set = output.find("\x1B[?8452h");
    const size_t reset = output.find("\x1B[?8452l");
    const size_t sixel = output.find(sixels[0]);
    ASSERT_NE(set, std::string::npos);
    ASSERT_NE(reset, std::string::npos);
    EXPECT_LT(set, sixel);
    EXPECT_LT(sixel, reset);
  }

  // Not supported: the last row is left out.
  Terminal::SetSixelLastRowSupport(false);
  {
    Screen screen(4, 3);
    screen.DrawGraphic(Graphic(0, 0, 4, 3));
    const std::string output = screen.ToString();
    const std::vector<std::string> sixels = test::FindSixels(output);
    ASSERT_EQ(sixels.size(), 1U);
    EXPECT_EQ(test::DecodeSixel(sixels[0]).height, 40);
    EXPECT_FALSE(Contains(output, "\x1B[?8452"));
    // Drawn from the first row: 2 rows up.
    EXPECT_TRUE(Contains(output, "\0337\r\x1B[2A" + sixels[0]));
  }

  // A graphic away from the last row is not affected.
  {
    Screen screen(4, 3);
    screen.DrawGraphic(Graphic(0, 0, 4, 2));
    const std::vector<std::string> sixels = test::FindSixels(screen.ToString());
    ASSERT_EQ(sixels.size(), 1U);
    EXPECT_EQ(test::DecodeSixel(sixels[0]).height, 40);
  }

  // The cells left out are printed, even when the graphic is not sent again.
  {
    Screen screen(4, 3);
    const Bitmap bitmap = Red(40, 60);
    std::string output;
    for (int i = 0; i < 2; ++i) {
      screen.Clear();
      Screen::Graphic graphic = Graphic(0, 0, 4, 3);
      graphic.bitmap = bitmap;
      screen.DrawGraphic(graphic);
      output.clear();
      screen.ToStringIncremental(output);
    }
    EXPECT_TRUE(test::FindSixels(output).empty());
    EXPECT_EQ(output, "\x1B[4C\r\n\x1B[4C\r\n    ");
  }
}

// The cursor ends where it would have without any graphic: the code printing
// the next frame relies on it.
TEST_F(GraphicsTest, IncrementalFullWidth) {
  const Bitmap bitmap = Red(40, 40);
  Screen screen(4, 2);
  std::string output;
  for (int i = 0; i < 2; ++i) {
    screen.Clear();
    Screen::Graphic graphic = Graphic(0, 0, 4, 2);
    graphic.bitmap = bitmap;
    screen.DrawGraphic(graphic);
    output.clear();
    screen.ToStringIncremental(output);
  }
  EXPECT_EQ(output, "\x1B[4C\r\n\x1B[4C");
}

}  // namespace ftxui
