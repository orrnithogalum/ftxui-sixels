// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ftxui/screen/bitmap.hpp"
#include "gtest/gtest.h"

namespace ftxui {

namespace {

// A 3x2 PNG:
//   red   green blue
//   white black transparent
const std::array<uint8_t, 85> g_png = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x9d, 0x74, 0x66, 0x1a,
    0x00, 0x00, 0x00, 0x1c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x05,
    0xc1, 0xa1, 0x01, 0x00, 0x00, 0x08, 0xc0, 0x20, 0xb2, 0xd9, 0xec,
    0xff, 0x5f, 0x4e, 0x10, 0x49, 0x94, 0x2a, 0x34, 0x7b, 0x1e, 0xa1,
    0x19, 0x0b, 0x32, 0xba, 0x1e, 0x2e, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

Bitmap Solid(int width, int height, std::array<uint8_t, 4> color) {
  std::vector<uint8_t> pixels;
  for (int i = 0; i < width * height; ++i) {
    pixels.insert(pixels.end(), color.begin(), color.end());
  }
  return Bitmap::FromRGBA(width, height, pixels.data());
}

std::array<uint8_t, 4> At(const Bitmap& bitmap, int x, int y) {
  const uint8_t* pixel = bitmap.data() + (y * bitmap.width() + x) * 4;
  return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

}  // namespace

TEST(BitmapTest, Empty) {
  const Bitmap bitmap;
  EXPECT_TRUE(bitmap.empty());
  EXPECT_EQ(bitmap.width(), 0);
  EXPECT_EQ(bitmap.height(), 0);
  EXPECT_EQ(bitmap.data(), nullptr);
  EXPECT_EQ(bitmap.id(), 0U);
  EXPECT_TRUE(bitmap.Resized(10, 10).empty());
}

TEST(BitmapTest, InvalidSize) {
  EXPECT_TRUE(Bitmap(0, 10).empty());
  EXPECT_TRUE(Bitmap(10, -1).empty());
  EXPECT_TRUE(Bitmap::FromRGBA(2, 2, nullptr).empty());
  EXPECT_TRUE(Solid(4, 4, {1, 2, 3, 255}).Resized(0, 4).empty());
}

TEST(BitmapTest, FromRGBA) {
  const std::vector<uint8_t> pixels = {
      1, 2, 3, 255, 4, 5, 6, 128,  //
  };
  const Bitmap bitmap = Bitmap::FromRGBA(2, 1, pixels.data());
  ASSERT_FALSE(bitmap.empty());
  EXPECT_EQ(bitmap.width(), 2);
  EXPECT_EQ(bitmap.height(), 1);
  EXPECT_FALSE(bitmap.opaque());
  EXPECT_EQ(At(bitmap, 0, 0), (std::array<uint8_t, 4>{1, 2, 3, 255}));
  EXPECT_EQ(At(bitmap, 1, 0), (std::array<uint8_t, 4>{4, 5, 6, 128}));
}

TEST(BitmapTest, FromRGB) {
  const std::vector<uint8_t> pixels = {1, 2, 3, 4, 5, 6};
  const Bitmap bitmap = Bitmap::FromRGB(2, 1, pixels.data());
  ASSERT_FALSE(bitmap.empty());
  EXPECT_TRUE(bitmap.opaque());
  EXPECT_EQ(At(bitmap, 1, 0), (std::array<uint8_t, 4>{4, 5, 6, 255}));
}

TEST(BitmapTest, CopySharesTheContent) {
  const Bitmap a = Solid(2, 2, {1, 2, 3, 255});
  const Bitmap b = a;  // NOLINT
  EXPECT_EQ(a.id(), b.id());
  EXPECT_EQ(a.data(), b.data());
  EXPECT_NE(a.id(), Solid(2, 2, {1, 2, 3, 255}).id());
}

TEST(BitmapTest, MutableDataDetaches) {
  Bitmap a = Solid(2, 2, {1, 2, 3, 255});
  const Bitmap b = a;
  const uint64_t id = a.id();

  a.MutableData()[0] = 42;
  EXPECT_NE(a.id(), id);
  EXPECT_EQ(b.id(), id);
  EXPECT_EQ(At(a, 0, 0)[0], 42);
  EXPECT_EQ(At(b, 0, 0)[0], 1);
}

TEST(BitmapTest, LoadFromMemory) {
  const Bitmap bitmap = Bitmap::LoadFromMemory(g_png.data(), g_png.size());
  ASSERT_FALSE(bitmap.empty());
  EXPECT_EQ(bitmap.width(), 3);
  EXPECT_EQ(bitmap.height(), 2);
  EXPECT_FALSE(bitmap.opaque());
  EXPECT_EQ(At(bitmap, 0, 0), (std::array<uint8_t, 4>{255, 0, 0, 255}));
  EXPECT_EQ(At(bitmap, 1, 0), (std::array<uint8_t, 4>{0, 255, 0, 255}));
  EXPECT_EQ(At(bitmap, 2, 0), (std::array<uint8_t, 4>{0, 0, 255, 255}));
  EXPECT_EQ(At(bitmap, 0, 1), (std::array<uint8_t, 4>{255, 255, 255, 255}));
  EXPECT_EQ(At(bitmap, 1, 1), (std::array<uint8_t, 4>{0, 0, 0, 255}));
  EXPECT_EQ(At(bitmap, 2, 1)[3], 0);
}

TEST(BitmapTest, LoadFailure) {
  std::string error;
  const std::array<uint8_t, 8> garbage = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_TRUE(
      Bitmap::LoadFromMemory(garbage.data(), garbage.size(), &error).empty());
  EXPECT_FALSE(error.empty());

  error.clear();
  EXPECT_TRUE(Bitmap::Load("/this/file/does/not/exist.png", &error).empty());
  EXPECT_FALSE(error.empty());

  // A truncated file.
  error.clear();
  EXPECT_TRUE(Bitmap::LoadFromMemory(g_png.data(), 40, &error).empty());
  EXPECT_FALSE(error.empty());
}

TEST(BitmapTest, ResizedSameSizeSharesTheContent) {
  const Bitmap bitmap = Solid(4, 3, {10, 20, 30, 255});
  EXPECT_EQ(bitmap.Resized(4, 3).id(), bitmap.id());
}

// Scaling a uniform image, up or down, must not change its color.
TEST(BitmapTest, ResizedKeepsUniformColor) {
  const std::array<uint8_t, 4> color = {10, 120, 250, 255};
  const Bitmap bitmap = Solid(7, 5, color);
  for (const BitmapFilter filter :
       {BitmapFilter::Smooth, BitmapFilter::Nearest}) {
    for (const auto& [width, height] :
         std::vector<std::pair<int, int>>{{70, 50}, {3, 2}, {13, 4}, {1, 1}}) {
      const Bitmap scaled = bitmap.Resized(width, height, filter);
      ASSERT_FALSE(scaled.empty());
      EXPECT_EQ(scaled.width(), width);
      EXPECT_EQ(scaled.height(), height);
      EXPECT_TRUE(scaled.opaque());
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          EXPECT_EQ(At(scaled, x, y), color);
        }
      }
    }
  }
}

// Scaling down averages the pixels.
TEST(BitmapTest, ResizedDownAverages) {
  const std::vector<uint8_t> pixels = {
      0,   0, 0, 255, 200, 0, 0, 255,  //
      200, 0, 0, 255, 0,   0, 0, 255,  //
  };
  const Bitmap bitmap = Bitmap::FromRGBA(2, 2, pixels.data());
  const Bitmap scaled = bitmap.Resized(1, 1);
  EXPECT_EQ(At(scaled, 0, 0), (std::array<uint8_t, 4>{100, 0, 0, 255}));
}

// Scaling up interpolates: the result is in between the pixels, and the
// pixels are found back at the borders.
TEST(BitmapTest, ResizedUpInterpolates) {
  const std::vector<uint8_t> pixels = {
      0, 0, 0, 255, 200, 0, 0, 255,  //
  };
  const Bitmap bitmap = Bitmap::FromRGBA(2, 1, pixels.data());
  const Bitmap scaled = bitmap.Resized(8, 1);
  EXPECT_EQ(At(scaled, 0, 0)[0], 0);
  EXPECT_EQ(At(scaled, 7, 0)[0], 200);
  for (int x = 1; x < 8; ++x) {
    EXPECT_GE(At(scaled, x, 0)[0], At(scaled, x - 1, 0)[0]);
  }
  EXPECT_GT(At(scaled, 4, 0)[0], 0);
  EXPECT_LT(At(scaled, 3, 0)[0], 200);
}

// The nearest filter doesn't create new colors.
TEST(BitmapTest, ResizedNearest) {
  const std::vector<uint8_t> pixels = {
      0, 0, 0, 255, 200, 0, 0, 255,  //
  };
  const Bitmap bitmap = Bitmap::FromRGBA(2, 1, pixels.data());
  const Bitmap scaled = bitmap.Resized(8, 3, BitmapFilter::Nearest);
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 8; ++x) {
      EXPECT_EQ(At(scaled, x, y)[0], x < 4 ? 0 : 200);
    }
  }
}

// The color of a transparent pixel must not leak into its neighbors.
TEST(BitmapTest, ResizedAlpha) {
  const std::vector<uint8_t> pixels = {
      255, 0, 0, 255, 0, 255, 0, 0,  //
  };
  const Bitmap bitmap = Bitmap::FromRGBA(2, 1, pixels.data());
  const Bitmap scaled = bitmap.Resized(1, 1);
  EXPECT_FALSE(scaled.opaque());
  // Half transparent, and still purely red.
  EXPECT_EQ(At(scaled, 0, 0), (std::array<uint8_t, 4>{255, 0, 0, 128}));
}

TEST(BitmapTest, ResampledSubRectangle) {
  // Four quadrants.
  std::vector<uint8_t> pixels;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      pixels.push_back(x < 2 ? 0 : 255);
      pixels.push_back(y < 2 ? 0 : 255);
      pixels.push_back(0);
      pixels.push_back(255);
    }
  }
  const Bitmap bitmap = Bitmap::FromRGBA(4, 4, pixels.data());

  // The bottom right quadrant, scaled up.
  const Bitmap scaled = bitmap.Resampled(2, 2, 2, 2, 6, 6);
  ASSERT_EQ(scaled.width(), 6);
  ASSERT_EQ(scaled.height(), 6);
  EXPECT_EQ(At(scaled, 5, 5), (std::array<uint8_t, 4>{255, 255, 0, 255}));
  EXPECT_EQ(At(scaled, 3, 3), (std::array<uint8_t, 4>{255, 255, 0, 255}));

  // The top left quadrant, scaled down.
  const Bitmap small = bitmap.Resampled(0, 0, 2, 2, 1, 1);
  EXPECT_EQ(At(small, 0, 0), (std::array<uint8_t, 4>{0, 0, 0, 255}));
}

}  // namespace ftxui
