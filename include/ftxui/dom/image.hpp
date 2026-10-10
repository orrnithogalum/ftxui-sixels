// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_DOM_IMAGE_HPP
#define FTXUI_DOM_IMAGE_HPP

#include <cstddef>      // for size_t
#include <cstdint>      // for uint8_t
#include <functional>   // for function
#include <memory>       // for shared_ptr
#include <string>       // for string
#include <string_view>  // for string_view

#include "ftxui/dom/node.hpp"       // for Element
#include "ftxui/screen/bitmap.hpp"  // for Bitmap, BitmapFilter
#include "ftxui/screen/color.hpp"   // for Color
#include "ftxui/screen/sixel.hpp"   // for SixelDither
#include "ftxui/util/export.hpp"    // for FTXUI_EXPORT

namespace ftxui {

/// @brief How an image is scaled to the area it is given.
///
/// This is the equivalent of the CSS property `object-fit`.
/// @ingroup dom
enum class ImageFit : uint8_t {
  /// Scale the image, up or down, to be as large as possible while being
  /// entirely visible. Its aspect ratio is preserved. This is the default.
  Contain,
  /// Scale the image, up or down, to cover the whole area. Its aspect ratio
  /// is preserved, what overflows is cropped.
  Cover,
  /// Stretch the image to the area. Its aspect ratio is not preserved.
  Fill,
  /// Same as Contain, but the image is never displayed larger than its
  /// original size.
  ScaleDown,
  /// The image keeps its original size. What overflows is cropped.
  None,
};

/// @brief Where an image is placed in the area it is given, along one axis,
/// when it doesn't fill it.
/// @ingroup dom
enum class ImageAlign : uint8_t {
  Start,   ///< Left, or top.
  Center,  ///< This is the default.
  End,     ///< Right, or bottom.
};

/// @brief What an image is drawn with.
/// @ingroup dom
enum class ImageRenderer : uint8_t {
  /// Sixel when the terminal supports it, see Terminal::SixelSupport().
  /// Otherwise, Blocks.
  Auto,
  /// Pixels, using the sixel protocol.
  Sixel,
  /// Two "pixels" per cell, using the unicode half block characters. It works
  /// in every terminal.
  Blocks,
};

/// @brief Empty space around an image, in cells.
/// @ingroup dom
struct ImageMargin {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

/// @brief Options of the ftxui::image element.
///
/// Every dimension is expressed in terminal cells, as for any other FTXUI
/// element.
///
/// The options can be set directly, or by chaining the methods:
/// ```cpp
/// image("photo.jpg", ImageOptions()
///     .Fit(ImageFit::Cover)
///     .Margin(1)
///     .MaxSize(60, 20));
/// ```
///
/// @ingroup dom
struct FTXUI_EXPORT(DOM) ImageOptions {
  // --- Scaling ---

  /// How the image is scaled to its area.
  ImageFit fit = ImageFit::Contain;

  /// The largest and the smallest zoom factors, relative to the original size
  /// of the image. Zero means no limit.
  /// `max_scale = 1` forbids displaying the image larger than it is.
  float max_scale = 0.F;
  float min_scale = 0.F;

  /// How pixels are interpolated when scaling.
  BitmapFilter filter = BitmapFilter::Smooth;

  // --- Position ---

  /// Where the image is placed in its area, when it doesn't fill it.
  ImageAlign align_x = ImageAlign::Center;
  ImageAlign align_y = ImageAlign::Center;

  /// Empty cells around the image.
  ImageMargin margin;

  // --- Size ---

  /// The size the element asks for, margin excluded. Zero means automatic:
  /// - none set: the original size of the image.
  /// - one set: the other one follows the aspect ratio of the image.
  int width = 0;
  int height = 0;

  /// Bounds of the size of the image, margin excluded. Zero means none.
  /// Unlike the `size` decorator, they leave the element flexible in between.
  int min_width = 0;
  int min_height = 0;
  int max_width = 0;
  int max_height = 0;

  /// Whether the element takes the extra space offered by its container, and
  /// whether it gives up space when the container lacks some. This is what
  /// makes the image follow the size of the UI.
  bool grow = true;
  bool shrink = true;

  // --- Rendering ---

  /// What the image is drawn with.
  ImageRenderer renderer = ImageRenderer::Auto;

  /// Sixel only: the number of colors, in [2, 256]. Fewer colors means less
  /// data to send to the terminal.
  int colors = 256;

  /// Sixel only: how the missing colors are approximated.
  SixelDither dither = SixelDither::FloydSteinberg;

  /// The color partially transparent pixels are blended with. When left to
  /// its default, a pixel is either fully drawn, or not drawn at all.
  Color background = Color::Default;

  // --- Loading ---

  /// A text displayed in place of an image which failed to load.
  std::string alt;

  /// Whether a file is decoded in the background. When it is, the element is
  /// empty until the image is ready, and the UI is then drawn again. This is
  /// ignored when there is nothing to draw the UI again: no ftxui::App is
  /// running, and no callback was given to SetImageLoadedCallback().
  bool async = true;

  // --- Chaining ---
  // One method per option above, named after it, setting it and returning the
  // options. A few set several at once: Align, Center, Margin, Size, MinSize,
  // MaxSize and Flex.
  // clang-format off
  ImageOptions& Fit(ImageFit v) { fit = v; return *this; }
  ImageOptions& MaxScale(float v) { max_scale = v; return *this; }
  ImageOptions& MinScale(float v) { min_scale = v; return *this; }
  ImageOptions& Filter(BitmapFilter v) { filter = v; return *this; }
  ImageOptions& Align(ImageAlign x, ImageAlign y) { align_x = x; align_y = y; return *this; }
  ImageOptions& AlignX(ImageAlign v) { align_x = v; return *this; }
  ImageOptions& AlignY(ImageAlign v) { align_y = v; return *this; }
  ImageOptions& Center() { return Align(ImageAlign::Center, ImageAlign::Center); }
  ImageOptions& Margin(int all) { margin = {all, all, all, all}; return *this; }
  ImageOptions& Margin(int horizontal, int vertical) { margin = {horizontal, vertical, horizontal, vertical}; return *this; }
  ImageOptions& Margin(int left, int top, int right, int bottom) { margin = {left, top, right, bottom}; return *this; }
  ImageOptions& Width(int v) { width = v; return *this; }
  ImageOptions& Height(int v) { height = v; return *this; }
  ImageOptions& Size(int w, int h) { width = w; height = h; return *this; }
  ImageOptions& MinWidth(int v) { min_width = v; return *this; }
  ImageOptions& MinHeight(int v) { min_height = v; return *this; }
  ImageOptions& MaxWidth(int v) { max_width = v; return *this; }
  ImageOptions& MaxHeight(int v) { max_height = v; return *this; }
  ImageOptions& MinSize(int w, int h) { min_width = w; min_height = h; return *this; }
  ImageOptions& MaxSize(int w, int h) { max_width = w; max_height = h; return *this; }
  ImageOptions& Grow(bool v) { grow = v; return *this; }
  ImageOptions& Shrink(bool v) { shrink = v; return *this; }
  ImageOptions& Flex(bool v) { grow = v; shrink = v; return *this; }
  ImageOptions& Renderer(ImageRenderer v) { renderer = v; return *this; }
  ImageOptions& Colors(int v) { colors = v; return *this; }
  ImageOptions& Dither(SixelDither v) { dither = v; return *this; }
  ImageOptions& Background(Color v) { background = v; return *this; }
  ImageOptions& Alt(std::string_view v) { alt = std::string(v); return *this; }
  ImageOptions& Async(bool v) { async = v; return *this; }
  // clang-format on

  /// @brief The options reproducing the layout of the `ftxui-image-view`
  /// library: the element asks for the original size of the image, doesn't
  /// take extra space on its own, and the image sits in the top-left corner.
  ///
  /// The difference is that the image is scaled to whatever area it ends up
  /// with, both down and up.
  static ImageOptions ImageView();
};

// --- Elements ---

/// @brief Display an image, loaded from a file or from an http(s) URL.
/// @see ftxui::ImageOptions
/// @ingroup dom
FTXUI_EXPORT(DOM)
Element image(std::string_view path_or_url, ImageOptions options = {});

/// @brief Display an image, from its pixels.
/// @see ftxui::ImageOptions
/// @ingroup dom
FTXUI_EXPORT(DOM) Element image(Bitmap bitmap, ImageOptions options = {});

// --- Loading ---

/// @brief The state of an image loaded by ftxui::image from a path or a URL.
/// @ingroup dom
enum class ImageStatus : uint8_t {
  Unknown,  ///< Never requested, or forgotten since.
  Loading,
  Ready,
  Failed,
};

/// @brief Get the state of an image, without triggering its loading.
/// @param path_or_url What was given to ftxui::image.
/// @param error If non-null, receives the reason of a failure.
/// @ingroup dom
FTXUI_EXPORT(DOM)
ImageStatus GetImageStatus(std::string_view path_or_url,
                           std::string* error = nullptr);

/// @brief Set a function called every time an image has been loaded in the
/// background. It is called from another thread.
///
/// An ftxui::App draws the UI again on its own when this happens. This is
/// only needed for a custom main loop.
/// @ingroup dom
FTXUI_EXPORT(DOM) void SetImageLoadedCallback(std::function<void()> callback);

/// @brief Forget about an image: it is loaded again the next time it is
/// displayed. Useful after its file was modified.
/// @ingroup dom
FTXUI_EXPORT(DOM) void ReloadImage(std::string_view path_or_url);

/// @brief Forget about every image.
/// @ingroup dom
FTXUI_EXPORT(DOM) void ClearImageCache();

/// @brief The number of decoded images kept in memory. The default is 64.
/// @ingroup dom
FTXUI_EXPORT(DOM) void SetImageCacheSize(size_t max_images);

/// @brief The amount of memory used to keep the images once scaled to the
/// size they are displayed at. The default is 128 MiB.
/// @ingroup dom
FTXUI_EXPORT(DOM) void SetScaledImageCacheSize(size_t max_bytes);

/// @brief The number of images which can be loading at the same time, each
/// one in its own thread. The default is 4.
/// @ingroup dom
FTXUI_EXPORT(DOM) void SetMaxConcurrentImageLoads(int max_concurrent);

// --- ftxui-image-view ---
//
// The API of the `ftxui-image-view` and of the `ftxui-image-view-async`
// libraries, so that this can replace them. See also <image_view.hpp>.

/// @brief Display an image. Same as ftxui::image, with the options
/// ImageOptions::ImageView().
/// @ingroup dom
FTXUI_EXPORT(DOM) Element image_view(std::string_view url);

/// @brief Display an image, with the given options instead of
/// ImageOptions::ImageView(). Same as ftxui::image.
/// @ingroup dom
FTXUI_EXPORT(DOM)
Element image_view(std::string_view url, ImageOptions options);

/// @brief Same as SetImageLoadedCallback().
FTXUI_EXPORT(DOM) void setOnImageLoadedCallback(std::function<void()> callback);
/// @brief Same as SetImageCacheSize().
FTXUI_EXPORT(DOM) void setImageCacheMaxSize(size_t max_entries);
/// @brief The number of scaled images kept in memory. See also
/// SetScaledImageCacheSize(), which bounds their memory.
FTXUI_EXPORT(DOM) void setImageResizeCacheMaxSize(size_t max_entries);
/// @brief Does nothing. There are no characters to cache anymore.
FTXUI_EXPORT(DOM) void setImageCharCacheMaxSize(size_t max_entries);
/// @brief Same as SetMaxConcurrentImageLoads().
FTXUI_EXPORT(DOM) void setMaxConcurrentImageLoads(int max_concurrent);

}  // namespace ftxui

#endif  // FTXUI_DOM_IMAGE_HPP
