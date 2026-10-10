@page images Images (sixel)

@tableofcontents

The `image` element displays a picture. It is drawn with real pixels, using the
[sixel](https://en.wikipedia.org/wiki/Sixel) protocol, in the terminals
supporting it. In the others, it falls back to unicode half blocks.

An image is an element like any other. It is given a box by its container, and
it scales to it, **up or down**, each time the layout changes: when the
terminal is resized, when a split is dragged, when a neighbor grows.

```cpp
#include <ftxui/dom/elements.hpp>

auto document = vbox({
    text("A picture:"),
    image("picture.png") | border,
    text("Its caption."),
});
```

Two examples are provided:
- `examples/component/image_view.cpp`: an interactive playground for every
  option below. Start here.
- `examples/dom/image.cpp`: printing images without an `App`.

# The element {#image-element}

```cpp
Element image(std::string_view path_or_url, ImageOptions options = {});
Element image(Bitmap bitmap, ImageOptions options = {});
```

- A **path** is decoded once, then shared by every element displaying it. The
  formats are PNG, JPEG, GIF (first frame), BMP, TGA, PSD and PNM.
- A **URL** (`http://` or `https://`) is downloaded using `curl`, or `wget`.
  They must be installed. This is not available on Windows.
- A **Bitmap** is a grid of RGBA pixels of your own:
  `Bitmap::FromRGBA(width, height, pixels)`, `Bitmap::Load(path)`.

# Size {#image-size}

Every dimension is expressed in **cells**, as everywhere else in FTXUI. The
conversion to pixels uses the size of the cells of the terminal, see
ftxui::Terminal::CellPixelSize.

**What the element asks for** is the original size of the picture. A picture of
400x400 pixels, in a terminal whose cells are 10x20 pixels, asks for 40x20
cells.

**What the element gets** depends on its container. By default, an image is
flexible both ways, like `graph` is:
- `grow`: it takes the extra space its container has to offer.
- `shrink`: it gives up space when its container lacks some.

The picture is then scaled to whatever the element got.

Since a picture has an aspect ratio, an image given less width than it asked
for needs less height as well. It tells its container, so that no blank is left
around it. The same goes for the other axis.

## Size constraints {#image-size-constraints}

| Option                      | Meaning                                              |
|-----------------------------|------------------------------------------------------|
| `Width(w)`, `Height(h)`     | The size asked for. With only one of them, the other one follows the aspect ratio. |
| `Size(w, h)`                | Both.                                                |
| `MinWidth`, `MinHeight`, `MinSize` | A lower bound of the size asked for.          |
| `MaxWidth`, `MaxHeight`, `MaxSize` | An upper bound of the picture, however large the element gets. |
| `MaxScale(s)`, `MinScale(s)` | Bounds of the zoom factor. `MaxScale(1)` never enlarges the picture. |
| `Grow(bool)`, `Shrink(bool)`, `Flex(bool)` | Whether the element follows its container. |

```cpp
// A thumbnail of exactly 12x6 cells.
image("cover.jpg", ImageOptions().Size(12, 6).Flex(false));

// As large as possible, but never more than 60x20 cells.
image("cover.jpg", ImageOptions().MaxSize(60, 20));

// As large as possible, but never blurry.
image("cover.jpg", ImageOptions().MaxScale(1));
```

The regular decorators work too: `size(WIDTH, EQUAL, 12)`, `flex`, `border`,
`center`, ... The difference with `size` is that the options above leave the
element flexible in between its bounds.

# Scaling {#image-scaling}

`Fit` tells how the picture is scaled to the area of the element. This is the
equivalent of `object-fit` in CSS.

| `ImageFit`  | The picture is...                                              |
|-------------|----------------------------------------------------------------|
| `Contain`   | as large as possible while entirely visible. The default.      |
| `Cover`     | covering the whole area. What overflows is cropped.            |
| `Fill`      | stretched to the area. Its aspect ratio is not preserved.      |
| `ScaleDown` | like `Contain`, but never larger than its original size.       |
| `None`      | at its original size. What overflows is cropped.               |

`Filter(BitmapFilter::Nearest)` keeps the pixels sharp when scaling up, which
is what pixel art wants. The default, `BitmapFilter::Smooth`, interpolates when
scaling up, and averages when scaling down.

# Centering {#image-centering}

When the picture doesn't fill its area, `Align` tells where it goes. When it is
cropped, it tells which part is kept. The default is the center, on both axes.

```cpp
image("logo.png", ImageOptions().Align(ImageAlign::Start, ImageAlign::End));
image("logo.png", ImageOptions().AlignX(ImageAlign::End));
image("logo.png", ImageOptions().Center());
```

The position is computed in pixels, not in cells: a picture is centered
exactly.

# Margin {#image-margin}

Empty cells around the picture, inside the element.

```cpp
image("logo.png", ImageOptions().Margin(1));           // Every side.
image("logo.png", ImageOptions().Margin(4, 1));        // Horizontal, vertical.
image("logo.png", ImageOptions().Margin(1, 2, 3, 4));  // Left, top, right, bottom.
```

# Rendering {#image-rendering}

| Option                | Meaning                                                    |
|-----------------------|------------------------------------------------------------|
| `Renderer(r)`         | `Auto` (default), `Sixel` or `Blocks`.                     |
| `Colors(n)`           | Sixel: the number of colors, up to 256. Fewer colors means less data. |
| `Dither(d)`           | Sixel: `FloydSteinberg` (default), `Ordered` or `None`.    |
| `Background(color)`   | The color partially transparent pixels are blended with. By default, a pixel is either drawn or not. |

Transparent pixels are not drawn: they let the background of the cells show
through. Use `bgcolor` to choose it.

## Terminal support {#image-terminal}

An `App` asks the terminal what it supports when it starts:
- **sixel**: from its "Device Attributes".
- **the size of a cell**: from the operating system, or from the terminal.

Sixel was verified with **xterm** (started with `-ti vt340`) and **mlterm**.
Other terminals advertising sixel are expected to work the same. Terminals
which don't advertise it get the half blocks.

The detection can be overridden:

| Environment variable        | Function                              | Effect                |
|-----------------------------|---------------------------------------|-----------------------|
| `FTXUI_SIXEL=1` or `0`      | `Terminal::SetSixelSupport(bool)`     | Force sixel on or off. |
| `FTXUI_CELL_SIZE=10x20`     | `Terminal::SetCellPixelSize({10,20})` | The size of a cell, in pixels. |
| `FTXUI_SIXEL_LAST_ROW=1` or `0` | `Terminal::SetSixelLastRowSupport(bool)` | See below.     |

A program printing a `Screen` without an `App` must ask by itself, before
rendering:

```cpp
Terminal::QueryGraphics();
```

## The last row {#image-last-row}

Many terminals move the cursor below an image after drawing it. When the image
reaches the last row of the terminal, this makes it scroll. Some terminals are
able to leave the cursor in place instead (DEC private mode 8452). An `App`
asks for it.

When the terminal doesn't know about this mode, an image reaching the last row
of the screen is displayed without its last row of cells. Putting anything
below the image, a border or a status line, avoids it altogether.

# Loading {#image-loading}

Within an `App`, files are decoded by background threads. The element is empty
until its picture is ready, then the `App` draws a new frame by itself.

Without an `App`, there is nothing to draw a new frame, so the picture is
loaded right away. This can also be requested: `ImageOptions().Async(false)`.

A picture which can't be loaded displays the name of its file, or
`ImageOptions().Alt("some text")`.

| Function                          | Meaning                                       |
|-----------------------------------|-----------------------------------------------|
| `GetImageStatus(path, &error)`    | `Unknown`, `Loading`, `Ready` or `Failed`.    |
| `ReloadImage(path)`               | Forget a picture, after its file was modified. |
| `ClearImageCache()`               | Forget every picture.                         |
| `SetImageCacheSize(n)`            | The number of pictures kept. 64 by default.   |
| `SetScaledImageCacheSize(bytes)`  | The memory used by the scaled pictures. 128 MiB by default. |
| `SetMaxConcurrentImageLoads(n)`   | The number of loading threads. 4 by default.  |
| `SetImageLoadedCallback(fn)`      | Called from another thread when a picture is ready. Only useful without an `App`. |

# Replacing ftxui-image-view {#image-view}

This replaces the libraries
[ftxui-image-view](https://github.com/ljrrjl/ftxui-image-view) and
[ftxui-image-view-async](https://github.com/orrnithogalum/ftxui-image-view-async).
Their API is provided as is:

```cpp
#include "image_view.hpp"

Element image_view(std::string_view url);
void setOnImageLoadedCallback(std::function<void()> callback);
void setImageCacheMaxSize(size_t max_entries);
void setImageResizeCacheMaxSize(size_t max_entries);
void setImageCharCacheMaxSize(size_t max_entries);
void setMaxConcurrentImageLoads(int max_concurrent);
```

To migrate a project using CMake:
1. Fetch this repository in place of FTXUI.
2. Stop fetching the image library. The target `ftxui-image-view` it used to
   define is provided here, so `target_link_libraries` stays as it is.

Nothing else has to change. What differs:

|                         | ftxui-image-view              | Here                              |
|-------------------------|-------------------------------|-----------------------------------|
| Drawn with              | Characters, 4x8 "pixels" per cell | Pixels (sixel), or half blocks |
| Scaling                 | Down only                     | Down and up                       |
| Size asked for          | Original size, with cells of 4x8 | Original size, with the real cells |
| Refresh once loaded     | Manual, via the callback      | Automatic. The callback still works. |
| Dependencies            | CImg, ImageMagick, X11        | None                              |
| `setImageCharCacheMaxSize` | Bounds a cache            | Does nothing: no characters       |

`image_view(url)` keeps the layout of the original: the element asks for the
original size, doesn't grow by itself, and the picture sits in the top-left
corner. This is `image(url, ImageOptions::ImageView())`. As with the original,
use `| flex` to give it more space: the difference is that the picture then
grows with it.

To get the behavior of `image`, growing and centered, pass options:
`image_view(url, ImageOptions())`.

# How it works {#image-how}

- A graphic is attached to the cells it covers. Whatever is drawn over those
  cells afterward, a dialog for instance, cuts a hole in it. The borders of a
  `frame` clip it.
- A graphic is sent to the terminal once. The following frames skip its cells,
  until it changes, moves, or gets covered differently.
- The palette is computed once per picture, not once per size, so the colors
  are stable while resizing.
- Decoding, scaling and encoding are cached. Drawing a frame where nothing
  changed costs nothing.

# Limitations {#image-limitations}

- Sixel is a verbose format: a picture covering a large terminal is around a
  megabyte. This is fine locally, and slow over a slow connection. Use
  `Colors` and `Dither(SixelDither::None)` to reduce it.
- Sixel has 256 colors per picture at most, and no partial transparency.
- Animated GIF are not animated: the first frame is displayed.
- WebP and AVIF are not supported.
