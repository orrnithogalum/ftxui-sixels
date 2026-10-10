// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.

// A demo of the `image` element.
//
// Usage: ftxui_example_image_view [image files or URLs...]
//
// - "Playground": one image, and every option of ImageOptions: how it is
//   scaled, where it is placed, its margin, and its size constraints.
// - "Layouts": images combined with the other elements.
//
// Resize the terminal, or drag the vertical separator: the images follow.

#include <algorithm>  // for min, max
#include <cmath>      // for sqrt, sin, cos, abs
#include <cstdint>    // for uint8_t
#include <memory>     // for shared_ptr, allocator
#include <string>     // for string, to_string
#include <utility>    // for move
#include <vector>     // for vector

#include "ftxui/component/app.hpp"             // for App
#include "ftxui/component/component.hpp"       // for Renderer, Slider, ...
#include "ftxui/component/component_base.hpp"  // for ComponentBase
#include "ftxui/component/event.hpp"           // for Event
#include "ftxui/dom/elements.hpp"              // for image, text, vbox, ...
#include "ftxui/screen/bitmap.hpp"             // for Bitmap
#include "ftxui/screen/box.hpp"                // for Box
#include "ftxui/screen/color.hpp"              // for Color
#include "ftxui/screen/terminal.hpp"           // for CellPixelSize

using namespace ftxui;

namespace {

// -----------------------------------------------------------------------------
// A few pictures, drawn here, so that the demo doesn't depend on any file.
// -----------------------------------------------------------------------------

struct Canvas2D {
  int width;
  int height;
  std::vector<uint8_t> pixels;

  Canvas2D(int w, int h)
      : width(w), height(h), pixels(static_cast<size_t>(w * h * 4), 0) {}

  void Set(int x, int y, int r, int g, int b, int a = 255) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return;
    }
    uint8_t* pixel = &pixels[static_cast<size_t>((y * width + x) * 4)];
    pixel[0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
    pixel[1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
    pixel[2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
    pixel[3] = static_cast<uint8_t>(std::clamp(a, 0, 255));
  }

  Bitmap ToBitmap() const {
    return Bitmap::FromRGBA(width, height, pixels.data());
  }
};

// A test card, 16:10. Its circle and its squares tell whether the aspect
// ratio is preserved. Its frame tells whether it is cropped.
Bitmap TestCard() {
  const int width = 480;
  const int height = 300;
  Canvas2D canvas(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float u = float(x) / float(width - 1);
      const float v = float(y) / float(height - 1);

      // Background: a smooth gradient.
      int r = int(40 + 180 * u);
      int g = int(60 + 120 * v);
      int b = int(200 - 150 * u * v);

      // A grid of squares.
      if (x % 30 == 0 || y % 30 == 0) {
        r = r * 3 / 4;
        g = g * 3 / 4;
        b = b * 3 / 4;
      }

      // A circle.
      const float dx = float(x) - float(width) / 2.F;
      const float dy = float(y) - float(height) / 2.F;
      const float distance = std::sqrt(dx * dx + dy * dy);
      if (distance < 110.F) {
        const float t = distance / 110.F;
        r = int(255 - 60 * t);
        g = int(245 - 110 * t);
        b = int(225 - 180 * t);
      }
      if (std::abs(distance - 110.F) < 2.5F) {
        r = g = b = 250;
      }

      // The diagonals.
      const float diagonal_1 = std::abs(dy - dx * float(height) / float(width));
      const float diagonal_2 = std::abs(dy + dx * float(height) / float(width));
      if (diagonal_1 < 1.F || diagonal_2 < 1.F) {
        r = g = b = 250;
      }

      // A frame.
      const bool frame = x < 6 || y < 6 || x >= width - 6 || y >= height - 6;
      if (frame) {
        r = 250;
        g = 190;
        b = 40;
      }
      canvas.Set(x, y, r, g, b);
    }
  }

  // Color bars.
  const int bars[6][3] = {
      {235, 60, 60}, {240, 160, 50}, {240, 230, 70},
      {70, 200, 90}, {60, 140, 240}, {160, 90, 220},
  };
  for (int i = 0; i < 6; ++i) {
    for (int y = 250; y < 280; ++y) {
      for (int x = 60 + i * 60; x < 120 + i * 60; ++x) {
        canvas.Set(x, y, bars[i][0], bars[i][1], bars[i][2]);
      }
    }
  }
  return canvas.ToBitmap();
}

// A portrait picture, 2:3: a sunset.
Bitmap Sunset() {
  const int width = 240;
  const int height = 360;
  Canvas2D canvas(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float v = float(y) / float(height - 1);
      // The sky.
      int r = int(30 + 225 * v);
      int g = int(20 + 120 * v * v);
      int b = int(90 + 40 * (1.F - v));

      // The sun.
      const float dx = float(x) - 120.F;
      const float dy = float(y) - 200.F;
      const float distance = std::sqrt(dx * dx + dy * dy);
      if (distance < 55.F) {
        r = 255;
        g = int(240 - 60 * (distance / 55.F));
        b = int(170 - 120 * (distance / 55.F));
      }

      // The mountains.
      const float ridge = 250.F + 26.F * std::sin(float(x) * 0.045F) +
                          12.F * std::sin(float(x) * 0.13F + 1.F);
      if (float(y) > ridge) {
        const float depth = (float(y) - ridge) / float(height);
        r = int(40 - 60 * depth);
        g = int(22 - 30 * depth);
        b = int(60 - 60 * depth);
      }
      canvas.Set(x, y, r, g, b);
    }
  }
  return canvas.ToBitmap();
}

// Pixel art: 16x16 pixels. This is the kind of picture a terminal could not
// display at a decent size without scaling it up.
Bitmap Sprite() {
  const char* rows[16] = {
      "................",  //
      "....########....",  //
      "..############..",  //
      ".##############.",  //
      ".###oo####oo###.",  //
      ".###oo####oo###.",  //
      ".##############.",  //
      ".##############.",  //
      ".####..##..####.",  //
      ".###.##..##.###.",  //
      "..############..",  //
      "...##.####.##...",  //
      "..##..#..#..##..",  //
      ".##...#..#...##.",  //
      ".#....#..#....#.",  //
      "................",  //
  };
  Canvas2D canvas(16, 16);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      switch (rows[y][x]) {
        case '#':
          canvas.Set(x, y, 90, 220, 120);
          break;
        case 'o':
          canvas.Set(x, y, 20, 30, 40);
          break;
        default:
          break;  // Transparent.
      }
    }
  }
  return canvas.ToBitmap();
}

// A logo on a transparent background, with smooth edges.
Bitmap Logo() {
  const int size = 256;
  Canvas2D canvas(size, size);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const float dx = float(x) - 127.5F;
      const float dy = float(y) - 127.5F;
      const float distance = std::sqrt(dx * dx + dy * dy);
      // A ring.
      const float ring =
          1.F - std::clamp(std::abs(distance - 96.F) - 22.F, 0.F, 1.F);
      // A disk.
      const float disk = 1.F - std::clamp(distance - 44.F, 0.F, 1.F);
      const float alpha = std::max(ring, disk);
      if (alpha <= 0.F) {
        continue;
      }
      const float angle = std::atan2(dy, dx);
      const int r = int(150 + 100 * std::cos(angle));
      const int g = int(150 + 100 * std::cos(angle + 2.1F));
      const int b = int(150 + 100 * std::cos(angle + 4.2F));
      if (disk > 0.F) {
        canvas.Set(x, y, 250, 250, 250, int(255 * alpha));
      } else {
        canvas.Set(x, y, r, g, b, int(255 * alpha));
      }
    }
  }
  return canvas.ToBitmap();
}

// A picture is either made of pixels, or loaded from a file or a URL.
struct Picture {
  std::string name;
  Bitmap bitmap;
  std::string path;

  Element Render(const ImageOptions& options) const {
    return path.empty() ? image(bitmap, options) : image(path, options);
  }
};

std::string Basename(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

int main(int argc, char** argv) {
  // ---------------------------------------------------------------------------
  // The pictures.
  // ---------------------------------------------------------------------------
  std::vector<Picture> pictures;
  for (int i = 1; i < argc; ++i) {
    pictures.push_back({Basename(argv[i]), Bitmap(), argv[i]});
  }
  pictures.push_back({"Test card (480x300)", TestCard(), ""});
  pictures.push_back({"Sunset (240x360)", Sunset(), ""});
  pictures.push_back({"Sprite (16x16)", Sprite(), ""});
  pictures.push_back({"Logo (256x256, alpha)", Logo(), ""});

  std::vector<std::string> picture_names;
  for (const Picture& picture : pictures) {
    picture_names.push_back(picture.name);
  }

  // ---------------------------------------------------------------------------
  // The state of the playground.
  // ---------------------------------------------------------------------------
  int picture_index = 0;
  int fit = 0;
  int align_x = 1;
  int align_y = 1;
  int margin_x = 0;
  int margin_y = 0;
  int max_width = 0;
  int max_height = 0;
  int min_width = 0;
  int min_height = 0;
  int max_scale = 0;
  int renderer = 0;
  int filter = 0;
  int dither = 2;
  int colors = 256;
  bool grow = true;
  bool checkerboard = false;

  const std::vector<std::string> fit_names = {
      "Contain   : fit inside",    "Cover     : fill, crop",
      "Fill      : stretch",       "ScaleDown : never enlarge",
      "None      : original size",
  };
  const std::vector<std::string> align_x_names = {"left", "center", "right"};
  const std::vector<std::string> align_y_names = {"top", "center", "bottom"};
  const std::vector<std::string> max_scale_names = {"no limit", "x1", "x2",
                                                    "x4"};
  const std::vector<std::string> renderer_names = {"auto", "sixel", "blocks"};
  const std::vector<std::string> filter_names = {"smooth", "nearest"};
  const std::vector<std::string> dither_names = {"none", "ordered",
                                                 "diffusion"};

  auto options = [&] {
    const float scales[] = {0.F, 1.F, 2.F, 4.F};
    return ImageOptions()
        .Fit(static_cast<ImageFit>(fit))
        .Align(static_cast<ImageAlign>(align_x),
               static_cast<ImageAlign>(align_y))
        .Margin(margin_x, margin_y)
        .MinSize(min_width, min_height)
        .MaxSize(max_width, max_height)
        .MaxScale(scales[max_scale])
        .Renderer(static_cast<ImageRenderer>(renderer))
        .Filter(static_cast<BitmapFilter>(filter))
        .Dither(static_cast<SixelDither>(dither))
        .Colors(colors)
        .Grow(grow);
  };

  // ---------------------------------------------------------------------------
  // Playground: the controls.
  // ---------------------------------------------------------------------------
  auto picture_menu = Menu(&picture_names, &picture_index);
  auto fit_radiobox = Radiobox(&fit_names, &fit);
  auto align_x_toggle = Toggle(&align_x_names, &align_x);
  auto align_y_toggle = Toggle(&align_y_names, &align_y);
  auto margin_x_slider = Slider("", &margin_x, 0, 20, 1);
  auto margin_y_slider = Slider("", &margin_y, 0, 10, 1);
  auto max_width_slider = Slider("", &max_width, 0, 120, 2);
  auto max_height_slider = Slider("", &max_height, 0, 50, 1);
  auto min_width_slider = Slider("", &min_width, 0, 120, 2);
  auto min_height_slider = Slider("", &min_height, 0, 50, 1);
  auto max_scale_toggle = Toggle(&max_scale_names, &max_scale);
  auto grow_checkbox = Checkbox("grow: take the space available", &grow);
  auto renderer_toggle = Toggle(&renderer_names, &renderer);
  auto filter_toggle = Toggle(&filter_names, &filter);
  auto dither_toggle = Toggle(&dither_names, &dither);
  auto colors_slider = Slider("", &colors, 2, 256, 2);
  auto checkerboard_checkbox =
      Checkbox("checkerboard behind the image", &checkerboard);

  auto controls_container = Container::Vertical({
      picture_menu,
      fit_radiobox,
      align_x_toggle,
      align_y_toggle,
      margin_x_slider,
      margin_y_slider,
      max_width_slider,
      max_height_slider,
      min_width_slider,
      min_height_slider,
      max_scale_toggle,
      grow_checkbox,
      renderer_toggle,
      filter_toggle,
      dither_toggle,
      colors_slider,
      checkerboard_checkbox,
  });

  auto row = [](const std::string& label, Element value, Element control) {
    return hbox({
        text(label) | size(WIDTH, EQUAL, 11),
        std::move(value) | size(WIDTH, EQUAL, 5),
        std::move(control) | xflex,
    });
  };
  auto number = [](int value, const std::string& zero = "") {
    return text(value == 0 && !zero.empty() ? zero : std::to_string(value)) |
           dim;
  };
  auto title = [](const std::string& value) {
    return hbox({text(value) | bold, text(" "), separator() | dim | xflex}) |
           color(Color::Cyan);
  };

  auto controls = Renderer(controls_container, [&] {
    return vbox({
               title("Picture"),
               picture_menu->Render(),
               title("Scaling"),
               fit_radiobox->Render(),
               row("max scale", text(""), max_scale_toggle->Render()),
               title("Centering"),
               row("horizontal", text(""), align_x_toggle->Render()),
               row("vertical", text(""), align_y_toggle->Render()),
               title("Margin (cells)"),
               row("left/right", number(margin_x), margin_x_slider->Render()),
               row("top/bottom", number(margin_y), margin_y_slider->Render()),
               title("Size constraints (cells)"),
               row("max width", number(max_width, "none"),
                   max_width_slider->Render()),
               row("max height", number(max_height, "none"),
                   max_height_slider->Render()),
               row("min width", number(min_width, "none"),
                   min_width_slider->Render()),
               row("min height", number(min_height, "none"),
                   min_height_slider->Render()),
               grow_checkbox->Render(),
               title("Rendering"),
               row("renderer", text(""), renderer_toggle->Render()),
               row("filter", text(""), filter_toggle->Render()),
               row("dithering", text(""), dither_toggle->Render()),
               row("colors", number(colors), colors_slider->Render()),
               checkerboard_checkbox->Render(),
           }) |
           vscroll_indicator | yframe | yflex;
  });

  // ---------------------------------------------------------------------------
  // Playground: the image.
  // ---------------------------------------------------------------------------
  Box image_box;
  Box image_box_displayed;
  auto view = Renderer([&] {
    const Picture& picture = pictures[static_cast<size_t>(picture_index)];
    const Dimensions cell = Terminal::CellPixelSize();

    // The size of the element is only known once the frame is drawn. Check it
    // afterward, and draw one more frame when it changed, to display it in the
    // status line.
    if (App* app = App::Active()) {
      app->Post([&, app] {
        if (image_box != image_box_displayed) {
          image_box_displayed = image_box;
          app->PostEvent(Event::Custom);
        }
      });
    }

    Element picture_element = picture.Render(options()) | reflect(image_box);
    if (checkerboard) {
      // A transparent image lets what is below show through.
      picture_element = dbox({
          canvas([](Canvas& c) {
            for (int y = 0; y < c.height(); y += 4) {
              for (int x = 0; x < c.width(); x += 2) {
                if (((x / 4) + (y / 8)) % 2 == 0) {
                  c.DrawText(x, y, " ", [](Cell& cell_under) {
                    cell_under.background_color = Color::GrayDark;
                  });
                }
              }
            }
          }) | flex,
          picture_element,
      });
      // A `dbox` isn't flexible on its own.
      if (grow) {
        picture_element = picture_element | flex;
      }
    }

    const std::string status =
        "cell: " + std::to_string(cell.dimx) + "x" + std::to_string(cell.dimy) +
        " px   sixel: " + (Terminal::SixelSupport() ? "yes" : "no") +
        "   element: " + std::to_string(image_box.x_max - image_box.x_min + 1) +
        "x" + std::to_string(image_box.y_max - image_box.y_min + 1) + " cells";

    // The image is placed in a `vbox`, in between two lines of text. With
    // `grow`, it is given whatever space is left, and scales accordingly.
    // Without, it has the size it asks for, and what follows sticks to it.
    return vbox({
        text(status) | dim,
        separator(),
        picture_element,
        separator(),
        text(grow ? "The image takes the space left in between those lines."
                  : "The image has the size it asks for. This follows it.") |
            dim,
        grow ? emptyElement() : filler(),
    });
  });

  int controls_width = 44;
  auto playground = ResizableSplitLeft(controls, view, &controls_width);

  // ---------------------------------------------------------------------------
  // Layouts: images combined with the other elements.
  // ---------------------------------------------------------------------------
  bool show_dialog = false;
  auto dialog_button = Button(
      "Open a dialog above the images", [&] { show_dialog = true; },
      ButtonOption::Ascii());
  auto close_button =
      Button("Close", [&] { show_dialog = false; }, ButtonOption::Ascii());

  auto layouts_content = Renderer(dialog_button, [&] {
    const Bitmap& card = pictures[pictures.size() - 4].bitmap;
    const Bitmap& sunset = pictures[pictures.size() - 3].bitmap;
    const Bitmap& sprite = pictures[pictures.size() - 2].bitmap;
    const Bitmap& logo = pictures[pictures.size() - 1].bitmap;

    auto card_window = [&](const std::string& name, ImageOptions o) {
      return window(text(name), image(card, std::move(o))) | flex;
    };

    return vbox({
        text("Three flexible windows sharing the width. Same image, three "
             "ways of fitting it:") |
            dim,
        hbox({
            card_window(" Contain ", ImageOptions().Fit(ImageFit::Contain)),
            card_window(" Cover ", ImageOptions().Fit(ImageFit::Cover)),
            card_window(" Fill ", ImageOptions().Fit(ImageFit::Fill)),
        }) | flex,

        text("Fixed size thumbnails, next to flexible text:") | dim,
        hbox({
            image(sunset, ImageOptions().Size(12, 6).Flex(false)) | border,
            image(logo, ImageOptions().Size(12, 6).Flex(false)) | border,
            image(sprite, ImageOptions().Size(12, 6).Flex(false).Filter(
                              BitmapFilter::Nearest)) |
                border,
            vbox({
                text("Size constraints") | bold,
                paragraph(
                    "Each thumbnail asks for 12x6 cells and is not flexible. "
                    "This text takes the rest of the row. The sprite is 16x16 "
                    "pixels: it is scaled up, with the nearest filter to keep "
                    "its pixels sharp."),
            }) | flex |
                border,
        }),

        text("Centered, with a margin, and never wider than 40 cells:") | dim,
        image(card,
              ImageOptions().Margin(2, 1).MaxWidth(40).Fit(ImageFit::Cover)) |
            bgcolor(Color::RGB(40, 44, 52)) | size(HEIGHT, EQUAL, 8),

        dialog_button->Render() | hcenter,
    });
  });

  auto dialog = Renderer(close_button, [&] {
    return vbox({
               text("A dialog") | bold | hcenter,
               separator(),
               text("It is drawn above the images:"),
               text("the cells it covers are cut out of them."),
               separator(),
               close_button->Render() | hcenter,
           }) |
           border | clear_under;
  });
  auto layouts = Modal(layouts_content, dialog, &show_dialog);

  // ---------------------------------------------------------------------------
  // The tabs.
  // ---------------------------------------------------------------------------
  int tab = 0;
  const std::vector<std::string> tab_names = {"Playground", "Layouts"};
  auto tab_toggle = Toggle(&tab_names, &tab);
  auto tabs = Container::Tab({playground, layouts}, &tab);
  auto main_container = Container::Vertical({tab_toggle, tabs});

  auto screen = App::Fullscreen();
  auto main_renderer = Renderer(main_container, [&] {
    return vbox({
        hbox({
            text(" FTXUI image ") | bold | inverted,
            text(" "),
            tab_toggle->Render(),
            filler(),
            text("resize the terminal, drag the separator - q: quit ") | dim,
        }),
        separator(),
        tabs->Render() | flex,
    });
  });

  auto root = CatchEvent(main_renderer, [&](const Event& event) {
    if (event == Event::Character('q')) {
      screen.Exit();
      return true;
    }
    return false;
  });

  screen.Loop(root);
  return 0;
}
