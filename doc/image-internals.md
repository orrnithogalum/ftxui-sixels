@page images-internals Images: how the code works

@tableofcontents

This page documents the code behind the `image` element: what was added to
FTXUI, where it lives, and why it is built this way. It is meant for whoever
maintains this fork, debugs it, or extends it.

To *use* images, read @ref images instead.

# Overview {#images-internals-overview}

FTXUI is made of three libraries, each one built on the previous one. Images
required something in each of them:

```
┌ component ──────────────────────────────────────────────────────────────┐
│ App: asks the terminal what it supports, draws a new frame when a       │
│      picture is loaded, sends the graphics incrementally.               │
│ ┌ dom ────────────────────────────────────────────────────────────────┐ │
│ │ image(): the element. Layout, fitting, centering, margins.          │ │
│ │          Loading files in the background. Scaling, once per size.   │ │
│ │ ┌ screen ─────────────────────────────────────────────────────────┐ │ │
│ │ │ Bitmap:       pixels. Decoding, resampling.                     │ │ │
│ │ │ SixelPalette: the colors of a picture.                          │ │ │
│ │ │ EncodeSixel:  pixels => escape sequence.                        │ │ │
│ │ │ Screen:       graphics drawn over cells, printed with the text. │ │ │
│ │ │ Terminal:     sixel support, size of a cell.                    │ │ │
│ │ └─────────────────────────────────────────────────────────────────┘ │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

The guiding ideas:

1. **An image is laid out in cells, and drawn in pixels.** The layout of FTXUI
   only knows about cells. The element converts in between using the size of a
   cell in pixels.
2. **A graphic belongs to cells.** The screen remembers which cells display a
   graphic. This is what makes clipping, overlays and incremental updates
   work, with the rules which already apply to text.
3. **Nothing is computed twice.** Elements are rebuilt for every frame, so
   whatever is expensive lives in caches outside of them: decoded pictures,
   scaled pictures, palettes, encoded sixel.
4. **The terminal keeps what it was sent.** A picture is megabytes of escape
   sequences. It is sent once, then left alone for as long as it is unchanged.

# The files {#images-internals-files}

## Added

| File | Content |
|------|---------|
| `include/ftxui/screen/bitmap.hpp`, `src/ftxui/screen/bitmap.cpp` | `Bitmap`: RGBA pixels, decoding, resampling. |
| `src/ftxui/screen/bitmap_internal.hpp` | `Bitmap::Storage`: what a `Bitmap` points to. Shared with `sixel.cpp`. |
| `src/ftxui/screen/stb_image.h` | [stb_image](https://github.com/nothings/stb) v2.30, vendored. The image decoder. |
| `include/ftxui/screen/sixel.hpp`, `src/ftxui/screen/sixel.cpp` | `SixelPalette`, `SixelImage`, `EncodeSixel`. |
| `include/ftxui/dom/image.hpp`, `src/ftxui/dom/image.cpp` | `image`, `ImageOptions`, the loader, the scaling cache, the `ftxui-image-view` API. |
| `src/ftxui/dom/image_internal.hpp` | `internal::SetImageRefreshHook`: how `App` is told a picture was loaded. |
| `include/image_view.hpp` | The header of the `ftxui-image-view` library. It includes `ftxui/dom/image.hpp`. |
| `examples/component/image_view.cpp` | The interactive demo. |
| `examples/dom/image.cpp` | Printing images without an `App`. |
| `src/ftxui/screen/bitmap_test.cpp`, `sixel_test.cpp`, `graphics_test.cpp`, `src/ftxui/dom/image_test.cpp` | The tests. |
| `src/ftxui/screen/sixel_test_util.hpp` | A sixel decoder, for the tests. |
| `doc/image.md`, `doc/image-internals.md` | The documentation. |

## Modified

| File | Change |
|------|--------|
| `include/ftxui/screen/cell.hpp` | `Cell::graphic`, `Cell::GraphicCharacter()`. |
| `include/ftxui/screen/screen.hpp`, `src/ftxui/screen/screen.cpp` | `Screen::Graphic`, `DrawGraphic`, `ToStringIncremental`, `ResetGraphics`. `ToString` prints the graphics. |
| `include/ftxui/screen/terminal.hpp`, `src/ftxui/screen/terminal.cpp` | `SixelSupport`, `CellPixelSize`, `SixelLastRowSupport`, `QueryGraphics`, and their setters. |
| `src/ftxui/component/app.cpp` | The detection, the refresh, the incremental drawing. |
| `include/ftxui/dom/elements.hpp`, `include/ftxui/ftxui.hpp` | Include the new headers. |
| `src/ftxui/screen.cppm`, `src/ftxui/dom.cppm` | Export the new API from the C++20 modules. |
| `CMakeLists.txt`, `cmake/ftxui_*.cmake`, `meson.build`, `src/ftxui/*/meson.build`, `examples/meson.build`, `BUILD.bazel` | The new files, the threads, the `ftxui-image-view` target, the installation of `image_view.hpp`. |
| `tools/amalgamate.py` | Inline `stb_image.h` as it is in the single-file versions. |
| `.gitignore` | It is a whitelist: allow `include/image_view.hpp` and `stb_image.h`. |

# The life of a frame {#images-internals-frame}

What happens between `image("picture.png")` and the terminal, within an `App`:

1. **The element is created.** `image()` returns an `ImageNode` holding the
   path and the options. Nothing else happens yet.
2. **`ComputeRequirement`.** The node asks the `Loader` for the picture. The
   first time, it is not there: the loader queues it for a background thread
   and the node carries on without a picture, asking for no space. Once the
   picture is known, the node asks for its original size, converted to cells.
3. **`SetBox`.** The container gives the node a box. The node removes the
   margin and applies the maximal size: what remains is the *area* of the
   picture.
4. **`Check`.** If the box is smaller than what was asked along one axis, the
   node asks for one more layout pass, with a size matching its aspect ratio.
5. **`Render`.** The node computes where the picture lands in its area, in
   pixels, gets the picture scaled to this size from the `ScaledCache`, and
   calls `Screen::DrawGraphic`. The screen marks the cells covered.
6. **The other elements render.** Whatever writes over the marked cells takes
   them back.
7. **`Screen::ToStringIncremental`.** For each graphic, the screen finds which
   of its cells are still marked, and builds a signature of what is to be
   displayed. If the previous frame displayed the same thing, the graphic is
   *retained*: its cells are skipped. Otherwise its cells are printed as
   blanks, which erases what was there, and it is encoded as sixel and sent
   after the text.
8. **Later, the picture is loaded.** The loading thread calls the refresh
   hook. `App` invalidates its frame, and everything above happens again, this
   time with a picture.

# The screen library {#images-internals-screen}

## Bitmap {#images-internals-bitmap}

`include/ftxui/screen/bitmap.hpp`, `src/ftxui/screen/bitmap.cpp`

A `Bitmap` is a handle to a `Bitmap::Storage`: a size, the pixels (RGBA, 8
bits per channel, alpha not premultiplied), an `opaque` flag and an **id**.

- Copying a `Bitmap` shares the storage. This is what lets elements, caches and
  the screen pass pictures around for free.
- The id is a process-wide counter. Two bitmaps with the same id hold the same
  pixels. Every cache is keyed with it. `MutableData()` detaches the pixels and
  takes a new id, which is how modifying a bitmap invalidates what was cached
  about it. This is the way to animate a picture.
- Sizes are bounded to 2^28 pixels, and files to 512 MiB, so that a broken or
  hostile file can't make the process allocate without limit.

**Decoding** is done by `stb_image`. It is compiled only in `bitmap.cpp`, with
`STB_IMAGE_STATIC`, so its symbols don't clash with an application embedding
its own copy. The file is read in memory first (`STBI_NO_STDIO`), then decoded
to 4 channels.

**Resampling** (`Bitmap::Resampled`) takes a source rectangle with fractional
coordinates, and a destination size. Taking a rectangle is what makes `Cover`
cheap: the part which is cropped is never computed. It is separable: one
horizontal pass into floats with premultiplied alpha, limited to the rows
which are needed, then one vertical pass back to bytes. Premultiplying is
what prevents the color of a transparent pixel from leaking into its
neighbors. `BuildAxis` computes, for each destination pixel, which source
pixels contribute and how much:

| Case | Weights |
|------|---------|
| `Nearest` | The source pixel under the center of the destination pixel. |
| `Smooth`, scaling up | The two nearest pixels, linearly (bilinear). |
| `Smooth`, scaling down | Every source pixel covered by the destination pixel, weighted by the fraction covered (area averaging). |

Opaque bitmaps skip the alpha arithmetic. Asking for the size the bitmap
already has returns the same storage.

## SixelPalette {#images-internals-palette}

`include/ftxui/screen/sixel.hpp`, `src/ftxui/screen/sixel.cpp`

Sixel is a palette format. The palette is computed from the **original**
picture, not from the scaled one: it is computed once, and the colors don't
shift while the picture is resized. `SixelPalette::For` stores the result in
the storage of the bitmap, per number of colors and per background color.

`SixelPalette::Build`:

1. **Few colors: keep them all.** If the picture has no more colors than the
   palette may hold, they are used as they are. Logos, diagrams and pixel art
   stay exact. The colors are found with a small open addressing hash set,
   giving up as soon as there are too many.
2. **Otherwise: median cut.** At most 2^19 pixels are sampled into an histogram
   of 5 bits per channel. The bins are then split, always splitting the box
   with the largest weighted squared error along its worst axis, at its mean,
   until there are enough boxes. Each box becomes its mean color.
3. **Refinement.** Two passes of k-means over the bins move each color to the
   mean of what it represents.
4. **Snapping.** Sixel expresses a channel as a percentage: 101 levels. The
   palette is rounded to what a terminal is going to display, so that
   dithering accounts for it.

Partially transparent pixels are blended with the background first when there
is one, and ignored when they are mostly transparent otherwise. This is the
`Blend` helper, shared with the encoder.

`Nearest(r, g, b)` answers through a lookup table of 2^16 entries (5 bits of
red, 6 of green, 5 of blue), filled on demand by a linear search. The entries
are atomics: a palette is shared, and may be used from several threads. An
exact palette is first looked up in a hash table, so that its colors map to
themselves.

## EncodeSixel {#images-internals-encoder}

A `SixelImage` describes a *canvas* in pixels, a bitmap placed on it at an
offset which may be negative, and an optional mask made of cells. What the
bitmap doesn't cover, and what the mask hides, is transparent.

1. **Indices.** Every pixel becomes an index of the palette, or -1 for
   transparent. Floyd-Steinberg diffuses the error to the next pixels, the
   ordered dithering adds an 8x8 Bayer threshold. The mask is applied *after*
   dithering: cutting a hole doesn't change the look of what remains.
2. **Header.** `ESC P 0;1;0 q`: the second parameter asks the terminal to leave
   untouched the pixels which are not drawn. Then the raster attributes: a
   double quote followed by `1;1;width;height`, for square pixels and the size
   of the canvas. Then one `#n;2;r;g;b` per color **in use**.
3. **Bands.** Sixel sends 6 rows at a time. For each band, the pixels are
   distributed by color into *planes*: for each column having this color,
   which of its 6 pixels. A plane is sent as `#n` followed by one character
   per column (`?` + the 6 bits), run-length encoded (`!count character`).
   `$` goes back to the left for the next color, `-` goes to the next band.
4. **End.** The string terminator: `ESC` followed by a backslash. There is no
   `-` after the last band: a terminal would move one band down, and possibly
   scroll.

The cost is proportional to the number of pixels, not to the number of colors.

## Graphics on a Screen {#images-internals-graphics}

`include/ftxui/screen/screen.hpp`, `src/ftxui/screen/screen.cpp`

### Marking the cells

`Screen::DrawGraphic(graphic)` stores the graphic, and marks the cells it
covers, restricted to the `stencil`:

```cpp
cell.character = Cell::GraphicCharacter();  // U+FFFC
cell.graphic = id;                          // The index of the graphic + 1.
```

A cell displays a graphic for as long as **both** hold. Elements write text by
assigning `character`, so anything drawn afterward over the cell takes it
back, without knowing about graphics. This single rule gives:
- **Overlays**: a dialog in a `dbox` cuts a hole.
- **Clipping**: a `frame` sets the stencil, cells out of it are never marked.
- **Stacking**: the latest of two overlapping graphics owns the cells.

A marker in `character` alone would not tell *which* graphic. A number alone
would survive text being written. Hence both.

### Printing

`ToStringInternal` does everything, in three steps:

1. **Plan.** For each graphic: the bounding box of the cells it still owns, a
   mask of those cells inside the box, and a hash of the style of those cells,
   since the background shows through transparent pixels. From this, two
   strings:
   - the **key**: what identifies the pixels to send. The id of the bitmap and
     of the palette, the size, the offset, the size of a cell, the dithering,
     the background, the mask.
   - the **signature**: the key, plus the position, plus the hash of the
     styles. It identifies what the terminal displays.

   A graphic whose signature is in the list of the previous frame is
   *retained*.
2. **Text.** Cells are printed as before. A cell of a retained graphic is
   skipped, by accumulating a cursor movement (`CSI n C`). A cell of another
   graphic is printed as a blank with its style, which erases whatever graphic
   the terminal had there.
3. **Graphics.** For each graphic which is not retained: save the cursor
   (`ESC 7`), go to the top-left cell with relative movements, send the sixel,
   restore the cursor (`ESC 8`). Saving and restoring is required: where a
   terminal leaves the cursor after an image is not reliable. The sixel comes
   from the `SixelCache`, keyed by the key, or from `EncodeSixel`.

`ToString()` calls it without a previous frame: everything is sent.
`ToStringIncremental()` passes `displayed_graphics_`, and replaces it with the
signatures of the new frame. `ResetGraphics()` empties it.

The cursor ends exactly where it would have without any graphic. `App` relies
on it to position the next frame.

### The last row

After an image, many terminals put the cursor on the line below it. If the
image reaches the last row of the terminal, they scroll, and the whole
interface is shifted by one line. Two things are done about it:

- When `Terminal::SixelLastRowSupport()` is true, the graphics are surrounded
  with `CSI ? 8452 h` and `CSI ? 8452 l`. With this mode, the cursor is left
  next to the image.
- When it is false, the cells of the last row of the **screen** are excluded
  from every graphic (`last_row` in `ToStringInternal`). They are printed as
  blanks. The screen can't know whether its last row is the last row of the
  terminal, so it assumes it is.

## Terminal {#images-internals-terminal}

`include/ftxui/screen/terminal.hpp`, `src/ftxui/screen/terminal.cpp`

Three facts about the terminal, each with the same structure: a value forced by
the program, an environment variable, a value detected, a default.

| Fact | Forced by | Environment | Detected from | Default |
|------|-----------|-------------|---------------|---------|
| `SixelSupport()` | `SetSixelSupport` | `FTXUI_SIXEL` | `SetDetectedSixelSupport`: the device attributes include 4. | false |
| `CellPixelSize()` | `SetCellPixelSize` | `FTXUI_CELL_SIZE` | `ioctl(TIOCGWINSZ)` on the standard streams, then `SetReportedCellPixelSize`: the reply to `CSI 16 t`. | 10x20 |
| `SixelLastRowSupport()` | `SetSixelLastRowSupport` | `FTXUI_SIXEL_LAST_ROW` | `SetDetectedSixelLastRowSupport`: the terminal knows the mode 8452. | false |

The size of a cell is rounded down. A picture slightly smaller than its cells
is invisible, a picture overflowing from them is not.

`QueryGraphics()` is for programs without an `App`. It opens `/dev/tty`, turns
off echo and line buffering, sends the three queries listed in
@ref images-internals-sequences, reads the replies until the one about the
device attributes or a timeout, and restores the terminal.

# The dom library {#images-internals-dom}

`include/ftxui/dom/image.hpp`, `src/ftxui/dom/image.cpp`

## ImageNode {#images-internals-node}

| Method | Role |
|--------|------|
| `Resolve` | Get the picture from the `Loader`, once per node. |
| `Preferred` | The size asked for, in cells: `width`/`height`, or the original size, then the bounds. A dimension left automatic follows the aspect ratio. |
| `ComputeRequirement` | `Preferred`, plus the margin. `grow` and `shrink` become the flex factors. |
| `SetBox` | The area: the box minus the margin, bounded by `max_width`/`max_height` (`Bound`), what is left going around according to the alignment. |
| `UpdateHint`, `Check` | The extra layout pass, see below. |
| `Render` | Nothing if the area is empty or out of the stencil. Otherwise `RenderSixel`, `RenderBlocks`, or `RenderText` when there is no picture. |

### One more layout pass

The layout of FTXUI is: each element tells the size it wants, then it is given
a box. An element can't say "my height depends on my width".

Take a picture of 40x20 cells in a `vbox` only 20 columns wide. It is given 20
columns, and the 20 rows it asked for. Fitted, it is only 10 rows tall: 10 rows
are left blank, and what follows is pushed away.

FTXUI lets an element ask for another iteration of the layout
(`Node::Check`). `UpdateHint` is called at the end of `SetBox`: when the box is
smaller than what was asked along an axis, it computes the size the picture is
actually displayed at, and stores it as a *hint*. `Check` requests another
iteration, where `ComputeRequirement` returns the hint. The container then
gives back the space. The hint only ever shrinks, and there are at most 3 extra
passes, so this always terminates.

This only applies to `Contain` and `ScaleDown`: the other modes fill their
box, whatever its shape.

### Placement

`Place` computes where the picture lands in its area. It works in pixels, then
converts to *units*: whatever the renderer draws.

1. `ComputeScale`: the zoom factors for the fitting mode, bounded by
   `min_scale` and `max_scale`.
2. The position: `(area - picture) * factor`, with a factor of 0, 0.5 or 1 for
   start, center and end. When the picture overflows, the result is negative.
   This is the same formula, and it selects which part is cropped.
3. Conversion to units, rounding, and clipping to the area.
4. The part of the original picture which remains visible, as a fractional
   rectangle. It is what is given to the resampler.

| Renderer | Units per cell | Drawing |
|----------|----------------|---------|
| `RenderSixel` | cell width x cell height: pixels | A `Screen::Graphic` covering the cells touched, the bitmap at an offset inside the first one. |
| `RenderBlocks` | 1 x 2 | One cell per column and per pair of rows: `▀` with the upper pixel as foreground and the lower one as background. `▀` or `▄` alone when one half is transparent. |

Both go through the same code. This is why the layout is identical with and
without sixel.

## Loader {#images-internals-loader}

A map from a path or a URL to its state: `Loading`, `Ready` with a bitmap, or
`Failed` with an error.

- `Request` returns what is known. The first time, it either queues the source
  for a worker and returns `Loading`, or loads it right away. It loads right
  away when the options ask for it, or when **nobody listens**: with neither a
  refresh hook nor a user callback, nothing would draw the frame displaying
  the picture.
- Workers are detached threads, started on demand, 4 at most by default. A
  worker without work for 10 seconds exits.
- When a picture is loaded, the worker calls the refresh hook, set by `App`,
  then the callback, set by the user.
- When there are more than 64 entries, the least recently requested ones are
  evicted. Evicting is safe: a `Bitmap` in use is kept alive by its users.
- A failure is remembered, and not retried at every frame. `ReloadImage`
  forgets an entry.
- The loader is allocated once and never destroyed, because the workers may
  outlive `main`.

A URL is downloaded by `Download`, into a temporary file, with `curl` or
`wget`. They are run with `posix_spawnp`: no shell is involved, so a URL can't
inject a command. Their standard streams are redirected to `/dev/null` so they
can't write on the interface. Only what starts with `http://` or `https://` is
downloaded, and `curl` is told to refuse any other protocol, redirections
included.

## ScaledCache {#images-internals-scaled}

Scaling is done once per size. The key is the id of the original, the source
rectangle in 1/256 of a pixel, the destination size and the filter. It evicts
the least recently used entries above 128 MiB or 512 entries. The entry just
added is always kept: it is being displayed.

While the terminal is being resized, each frame adds an entry, and the oldest
ones go.

# The component library {#images-internals-component}

`src/ftxui/component/app.cpp`

| Where | What |
|-------|------|
| `InstallTerminalInfo` | Sends `CSI 16 t` and `CSI ? 8452 $ p` before the query for the device attributes. This one was already sent, every terminal replies to it, and the loop already waits for it: no delay is added. |
| The same loop, and `HandleTask` | `ParseCellPixelSize`, `ParseSixelCursorMode` and `HasSixel` recognize the replies and record them in `Terminal`. In `HandleTask`, the replies are consumed: components don't see them. |
| `Install` | `ResetGraphics()`, and sets the refresh hook: post a task invalidating the frame. `Post` is thread safe. The hook is removed when the app is uninstalled. |
| `Draw` | `ResetGraphics()` when the frame is resized or when something is printed above it: the terminal clears or moves what it displayed. |
| `Draw` | After a resize, sends `CSI 16 t` again: the font may have changed. |
| `Draw` | `ToStringIncremental` instead of `ToString`. When a graphic changed, the frame is surrounded with `CSI ? 2026 h` and `CSI ? 2026 l`: a terminal supporting synchronized updates displays it at once. |

# Escape sequences {#images-internals-sequences}

| Sequence | Name | Direction | Purpose |
|----------|------|-----------|---------|
| `CSI c` | DA1 | sent | Device attributes. Already sent by FTXUI. |
| `CSI ? ... ; 4 ; ... c` | | received | The attributes include 4: sixel. |
| `CSI 16 t` | XTWINOPS | sent | The size of a cell, in pixels. |
| `CSI 6 ; height ; width t` | | received | |
| `CSI ? 8452 $ p` | DECRQM | sent | Does the terminal know the mode 8452? |
| `CSI ? 8452 ; state $ y` | DECRPM | received | 1, 2 or 3: it does. 0: it doesn't. |
| `CSI ? 8452 h` / `l` | | sent | Leave the cursor next to an image, instead of below. |
| `ESC 7` / `ESC 8` | DECSC / DECRC | sent | Save and restore the cursor, around an image. |
| `CSI n A`, `CSI n C` | CUU, CUF | sent | Move to an image. Skip the cells of a retained one. |
| `ESC P 0;1;0 q ... ST` | DCS | sent | A sixel image. `ST` is `ESC` followed by a backslash. |
| `CSI ? 2026 h` / `l` | | sent | Synchronized update. |

# Caches {#images-internals-caches}

| Cache | Where | Key | Default limit | Setter |
|-------|-------|-----|---------------|--------|
| Decoded pictures | `Loader` | Path or URL | 64 entries | `SetImageCacheSize` |
| Scaled pictures | `ScaledCache` | Original, source rectangle, size, filter | 128 MiB, 512 entries | `SetScaledImageCacheSize`, `setImageResizeCacheMaxSize` |
| Palettes | `Bitmap::Storage` | Number of colors, background | Lives with the bitmap | |
| Nearest color | `SixelPalette` | Color, on 16 bits | 2^16 entries per palette | |
| Encoded sixel | `SixelCache` in `screen.cpp` | The key of a graphic | 64 MiB | `SetSixelCacheSize` |
| Displayed graphics | `Screen::displayed_graphics_` | The signature of a graphic | One frame | `ResetGraphics` |

# Threads {#images-internals-threads}

Everything runs on the thread drawing the interface, except loading.

| Shared state | Protection |
|--------------|------------|
| `Loader`: entries, queue, workers | A mutex. Decoding is done without holding it. |
| `Loader`: the hook and the callback | A recursive mutex, held while they are called. Removing one returns once it is no longer running. |
| `ScaledCache`, `SixelCache` | A mutex each. |
| The palettes of a bitmap | A mutex in the storage. Two threads may compute the same palette: the first one wins. |
| The lookup table of a palette | Atomics. |
| The ids | Atomic counters. |

The functions of `Terminal` are not synchronized. Call them from the thread
drawing the interface.

# Tests {#images-internals-tests}

```bash
cmake -S . -B build -DFTXUI_BUILD_TESTS=ON
cmake --build build
./build/ftxui-tests --gtest_filter='BitmapTest*:SixelTest*:GraphicsTest*:ImageTest*'
```

| File | Covers |
|------|--------|
| `bitmap_test.cpp` | Sharing and ids, decoding a PNG and failing on garbage, resampling: uniform colors, averaging, interpolation, nearest, alpha, sub-rectangles. |
| `sixel_test.cpp` | The format, the round trip through a decoder, transparency, blending, offsets, cropping, masks, quantization with every dithering, palettes. |
| `graphics_test.cpp` | Marking cells, what `ToString` prints, holes, the stencil, cropping, overlaps, the incremental mode and what invalidates a graphic, the last row. |
| `image_test.cpp` | The size asked for, every fitting mode, the alignment, the margin, the bounds, layouts with boxes and decorators, the extra layout pass, clipping, overlays, the blocks, loading files now and in the background, failures, the `ftxui-image-view` API. |

`sixel_test_util.hpp` holds `test::DecodeSixel`, a small sixel decoder. The
tests don't compare escape sequences byte for byte: they decode what was
produced and check the pixels.

The tests of the element set the size of a cell to 10x20 pixels and force
sixel, in a fixture, so they don't depend on the terminal running them.

**In a terminal.** The tests can't tell whether a terminal displays the right
thing. This was checked by eye, in xterm and in mlterm:

```bash
xterm -ti vt340 -xrm 'XTerm*numColorRegisters: 1024' \
      -xrm 'XTerm*maxGraphicSize: 4000x4000' \
      -e ./build/examples/component/ftxui_example_image_view
```

What to look at: resizing the window, dragging the separator, every option of
the playground, the dialog of the "Layouts" tab, an image reaching the last
row, and that moving the mouse doesn't make anything flicker.

# Build systems {#images-internals-build}

- **CMake** is the reference. The new sources are listed in `CMakeLists.txt`,
  the tests in `cmake/ftxui_test.cmake`. `dom` links to threads.
- **The target `ftxui-image-view`** is an interface library linking to
  `component`. It is what a project used to get from the library of this
  name. It is exported as `ftxui::image-view`. `include/image_view.hpp` is
  installed next to the `ftxui` directory.
- **Meson** and **Bazel** list the same files. Bazel was not run.
- **C++20 modules**: `screen.cppm` and `dom.cppm` export the new names.
- **The amalgamation** (`tools/amalgamate.py`) puts every source file in a
  single one. Two consequences: names in anonymous namespaces must be unique
  across the whole library, and `stb_image.h` must be inlined untouched,
  because its includes depend on its own macros.

# Extending {#images-internals-extending}

**Another protocol** (kitty, iTerm2). The element doesn't know about sixel: it
hands a `Screen::Graphic` to the screen. The work is in `ToStringInternal`,
step 3, where a graphic is turned into bytes, and in `Terminal`, to detect it.
The planning, the masks and the incremental logic stay the same. A protocol
able to place and delete images by id could do better than erasing cells.

**Another image format.** `Bitmap::LoadFromMemory` is the only place decoding
happens. Anything producing RGBA pixels can also be given to `image` as a
`Bitmap`.

**Animation.** Give `image` a `Bitmap`, and call `MutableData()` to modify it
in between frames. Each modification is a new id, hence a new graphic. Nothing
decodes the frames of a GIF.

# Keeping up with FTXUI {#images-internals-upstream}

The fork is two commits on top of FTXUI. To update it, rebase them. Most of
the code is in new files. The conflicts, if any, are in the three files where
existing functions were modified:

| File | Function |
|------|----------|
| `src/ftxui/screen/screen.cpp` | `Screen::ToString`, whose body became `ToStringInternal`. |
| `src/ftxui/component/app.cpp` | `Install`, `InstallTerminalInfo`, `HandleTask`, `Draw`. |
| `src/ftxui/screen/terminal.cpp` | Only additions. |

FTXUI checks its binary interface (`tools/abi_fingerprint.txt`). This fork
changes it: `Cell` and `Screen` have new fields. The fingerprint was not
regenerated.
