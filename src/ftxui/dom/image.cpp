// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include "ftxui/dom/image.hpp"

#include <algorithm>           // for min, max, clamp
#include <atomic>              // for atomic
#include <cctype>              // for tolower
#include <cerrno>              // for errno, EINTR
#include <chrono>              // for seconds
#include <cmath>               // for ceil, floor, lround
#include <condition_variable>  // for condition_variable
#include <cstddef>             // for size_t
#include <cstdint>             // for uint8_t, uint64_t, int64_t
#include <deque>               // for deque
#include <functional>          // for function, hash
#include <list>                // for list
#include <memory>              // for make_shared
#include <mutex>               // for mutex, lock_guard, unique_lock
#include <string>              // for string
#include <string_view>         // for string_view
#include <thread>              // for thread
#include <unordered_map>       // for unordered_map
#include <utility>             // for move
#include <vector>              // for vector

#include "ftxui/dom/image_internal.hpp"  // for SetImageRefreshHook
#include "ftxui/dom/node.hpp"            // for Node, Element
#include "ftxui/dom/requirement.hpp"     // for Requirement
#include "ftxui/screen/box.hpp"          // for Box
#include "ftxui/screen/screen.hpp"       // for Screen, Cell
#include "ftxui/screen/string.hpp"       // for Utf8ToGlyphs
#include "ftxui/screen/terminal.hpp"     // for CellPixelSize, SixelSupport

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define FTXUI_IMAGE_HAS_DOWNLOAD 1
#include <fcntl.h>     // for open, O_RDWR
#include <spawn.h>     // for posix_spawnp, posix_spawn_file_actions_t
#include <sys/wait.h>  // for waitpid
#include <unistd.h>    // for close, unlink
#include <cstdlib>     // for mkstemp

extern char** environ;  // NOLINT
#endif

namespace ftxui {

namespace {

// -----------------------------------------------------------------------------
// Loading
// -----------------------------------------------------------------------------

bool IsUrl(std::string_view source) {
  auto starts_with = [&](std::string_view prefix) {
    if (source.size() < prefix.size()) {
      return false;
    }
    return std::equal(
        prefix.begin(), prefix.end(), source.begin(),
        [](char expected, char actual) {
          return std::tolower(static_cast<unsigned char>(actual)) == expected;
        });
  };
  return starts_with("http://") || starts_with("https://");
}

#if defined(FTXUI_IMAGE_HAS_DOWNLOAD)
// Run a program, without a shell, and without access to the terminal. Returns
// its exit code, or -1 if it could not be run.
int Run(const std::vector<std::string>& arguments) {
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1);
  for (const std::string& argument : arguments) {
    argv.push_back(const_cast<char*>(argument.c_str()));  // NOLINT
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    return -1;
  }
  // The program must not read from, nor write to the terminal.
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

  pid_t pid = 0;
  const int status =
      posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (status != 0) {
    return -1;
  }

  int exit_status = 0;
  while (waitpid(pid, &exit_status, 0) < 0) {
    if (errno != EINTR) {
      // The application reaps its children itself. The outcome is unknown.
      return 0;
    }
  }
  if (!WIFEXITED(exit_status)) {
    return -1;
  }
  const int code = WEXITSTATUS(exit_status);
  constexpr int command_not_found = 127;
  return code == command_not_found ? -1 : code;
}

// Download a URL to a temporary file, using `curl`, or `wget`.
Bitmap Download(const std::string& url, std::string* error) {
  const char* tmpdir = std::getenv("TMPDIR");  // NOLINT
  std::string path = (tmpdir && tmpdir[0]) ? tmpdir : "/tmp";
  path += "/ftxui-image-XXXXXX";
  const int fd = mkstemp(path.data());
  if (fd < 0) {
    *error = "cannot create a temporary file";
    return {};
  }
  close(fd);

  int code = Run({
      "curl",
      "--silent",
      "--fail",
      "--location",
      "--max-time",
      "60",
      "--max-filesize",
      "268435456",
      "--proto",
      "=http,https",
      "--proto-redir",
      "=http,https",
      "--output",
      path,
      "--",
      url,
  });
  if (code < 0) {
    code = Run({
        "wget",
        "--quiet",
        "--timeout=60",
        "--output-document=" + path,
        "--",
        url,
    });
  }

  Bitmap bitmap;
  if (code < 0) {
    *error = "downloading requires curl or wget";
  } else if (code != 0) {
    *error = "the download failed";
  } else {
    bitmap = Bitmap::Load(path, error);
  }
  unlink(path.c_str());
  return bitmap;
}
#endif

Bitmap LoadSource(const std::string& source, std::string* error) {
  if (IsUrl(source)) {
#if defined(FTXUI_IMAGE_HAS_DOWNLOAD)
    return Download(source, error);
#else
    *error = "downloading is not supported on this platform";
    return {};
#endif
  }
  return Bitmap::Load(source, error);
}

// The images loaded from a path or a URL. Elements are short-lived: the ones
// of a frame are destroyed before the next one. So what they load is owned
// here, and shared in between every element displaying the same source.
//
// This is never destroyed, because the loading threads are detached, and may
// outlive the main function.
class Loader {
 public:
  static Loader& Get() {
    static auto* loader = new Loader();  // NOLINT
    return *loader;
  }

  struct Result {
    ImageStatus status = ImageStatus::Unknown;
    Bitmap bitmap;
    std::string error;
  };

  Result Request(const std::string& source, bool async) {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      auto it = entries_.find(source);
      if (it != entries_.end()) {
        it->second.last_use = ++clock_;
        return it->second.result;
      }

      // Loading in the background requires someone to be told about the
      // result, in order to draw a new frame.
      if (async && has_listener_.load()) {
        Entry& entry = entries_[source];
        entry.result.status = ImageStatus::Loading;
        entry.last_use = ++clock_;
        queue_.push_back(source);
        StartWorker();
        wake_.notify_one();
        return entry.result;
      }
    }

    Result result = Load(source);
    const std::lock_guard<std::mutex> lock(mutex_);
    Entry& entry = entries_[source];
    entry.result = result;
    entry.last_use = ++clock_;
    Evict();
    return result;
  }

  Result Peek(const std::string& source) {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(source);
    return it == entries_.end() ? Result() : it->second.result;
  }

  void Forget(const std::string& source) {
    const std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(source);
  }

  void Clear() {
    const std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
  }

  void SetMaxEntries(size_t max_entries) {
    const std::lock_guard<std::mutex> lock(mutex_);
    max_entries_ = std::max<size_t>(1, max_entries);
    Evict();
  }

  void SetMaxWorkers(int max_workers) {
    const std::lock_guard<std::mutex> lock(mutex_);
    max_workers_ = std::max(1, max_workers);
  }

  void SetUserCallback(std::function<void()> callback) {
    const std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
    user_callback_ = std::move(callback);
    has_listener_ = bool(user_callback_) || bool(refresh_hook_);
  }

  void SetRefreshHook(std::function<void()> hook) {
    const std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
    refresh_hook_ = std::move(hook);
    has_listener_ = bool(user_callback_) || bool(refresh_hook_);
  }

  // Sixel palettes are computed ahead, by the loading threads, once it is
  // known they are going to be used.
  void SetPrecomputePalette() { precompute_palette_ = true; }

 private:
  struct Entry {
    Result result;
    uint64_t last_use = 0;
  };

  Result Load(const std::string& source) {
    Result result;
    result.bitmap = LoadSource(source, &result.error);
    result.status =
        result.bitmap.empty() ? ImageStatus::Failed : ImageStatus::Ready;
    if (result.status == ImageStatus::Failed && result.error.empty()) {
      result.error = "cannot load the image";
    }
    if (result.status == ImageStatus::Ready && precompute_palette_.load()) {
      constexpr int default_colors = 256;
      SixelPalette::For(result.bitmap, default_colors);
    }
    return result;
  }

  // Must be called with `mutex_` held.
  void StartWorker() {
    if (idle_workers_ > 0 || workers_ >= max_workers_) {
      return;
    }
    workers_++;
    std::thread([this] { Work(); }).detach();
  }

  void Work() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
      if (queue_.empty()) {
        idle_workers_++;
        constexpr auto patience = std::chrono::seconds(10);
        const bool work =
            wake_.wait_for(lock, patience, [&] { return !queue_.empty(); });
        idle_workers_--;
        if (!work) {
          workers_--;
          return;
        }
      }
      // More workers than what is allowed: retire.
      if (workers_ > max_workers_) {
        workers_--;
        return;
      }

      const std::string source = std::move(queue_.front());
      queue_.pop_front();

      lock.unlock();
      Result result = Load(source);
      lock.lock();

      // The entry may have been forgotten in the meantime.
      auto it = entries_.find(source);
      if (it != entries_.end() &&
          it->second.result.status == ImageStatus::Loading) {
        it->second.result = std::move(result);
        Evict();
      }

      lock.unlock();
      Notify();
      lock.lock();
    }
  }

  void Notify() {
    const std::lock_guard<std::recursive_mutex> lock(listener_mutex_);
    if (refresh_hook_) {
      refresh_hook_();
    }
    if (user_callback_) {
      user_callback_();
    }
  }

  // Must be called with `mutex_` held.
  void Evict() {
    while (entries_.size() > max_entries_) {
      auto oldest = entries_.end();
      for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->second.result.status == ImageStatus::Loading) {
          continue;
        }
        if (oldest == entries_.end() ||
            it->second.last_use < oldest->second.last_use) {
          oldest = it;
        }
      }
      if (oldest == entries_.end()) {
        return;
      }
      entries_.erase(oldest);
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::unordered_map<std::string, Entry> entries_;
  std::deque<std::string> queue_;
  uint64_t clock_ = 0;
  size_t max_entries_ = 64;
  int max_workers_ = 4;
  int workers_ = 0;
  int idle_workers_ = 0;

  std::recursive_mutex listener_mutex_;
  std::function<void()> user_callback_;
  std::function<void()> refresh_hook_;
  std::atomic<bool> has_listener_{false};
  std::atomic<bool> precompute_palette_{false};
};

// -----------------------------------------------------------------------------
// Scaling
// -----------------------------------------------------------------------------

// The images, once scaled to the size they are displayed at. Scaling is done
// once per size, not once per frame.
class ScaledCache {
 public:
  static ScaledCache& Get() {
    static auto* cache = new ScaledCache();  // NOLINT
    return *cache;
  }

  Bitmap Scale(const Bitmap& bitmap,
               double source_x,
               double source_y,
               double source_width,
               double source_height,
               int width,
               int height,
               BitmapFilter filter) {
    // Fixed point, so that the key doesn't depend on rounding noise.
    constexpr double precision = 256.0;
    const Key key = {
        bitmap.id(),
        std::llround(source_x * precision),
        std::llround(source_y * precision),
        std::llround(source_width * precision),
        std::llround(source_height * precision),
        width,
        height,
        static_cast<int>(filter),
    };

    const std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(key);
    if (it != index_.end()) {
      entries_.splice(entries_.begin(), entries_, it->second);
      return it->second->second;
    }

    Bitmap scaled = bitmap.Resampled(source_x, source_y, source_width,
                                     source_height, width, height, filter);
    if (scaled.empty()) {
      return scaled;
    }
    entries_.emplace_front(key, scaled);
    index_[key] = entries_.begin();
    bytes_ += Bytes(scaled);
    Evict();
    return scaled;
  }

  void SetMaxBytes(size_t max_bytes) {
    const std::lock_guard<std::mutex> lock(mutex_);
    max_bytes_ = max_bytes;
    Evict();
  }

  void SetMaxEntries(size_t max_entries) {
    const std::lock_guard<std::mutex> lock(mutex_);
    max_entries_ = std::max<size_t>(1, max_entries);
    Evict();
  }

 private:
  struct Key {
    uint64_t id;
    long long source_x;
    long long source_y;
    long long source_width;
    long long source_height;
    int width;
    int height;
    int filter;

    bool operator==(const Key& other) const {
      return id == other.id && source_x == other.source_x &&
             source_y == other.source_y && source_width == other.source_width &&
             source_height == other.source_height && width == other.width &&
             height == other.height && filter == other.filter;
    }
  };

  struct Hash {
    size_t operator()(const Key& key) const {
      uint64_t hash = 14695981039346656037ULL;  // FNV-1a
      auto combine = [&](uint64_t value) {
        hash = (hash ^ value) * 1099511628211ULL;
      };
      combine(key.id);
      combine(static_cast<uint64_t>(key.source_x));
      combine(static_cast<uint64_t>(key.source_y));
      combine(static_cast<uint64_t>(key.source_width));
      combine(static_cast<uint64_t>(key.source_height));
      combine(static_cast<uint64_t>(key.width));
      combine(static_cast<uint64_t>(key.height));
      combine(static_cast<uint64_t>(key.filter));
      return static_cast<size_t>(hash);
    }
  };

  static size_t Bytes(const Bitmap& bitmap) {
    return static_cast<size_t>(bitmap.width()) *
           static_cast<size_t>(bitmap.height()) * 4;
  }

  // Must be called with `mutex_` held. The most recent entry is always kept:
  // it is the one being displayed.
  void Evict() {
    while (entries_.size() > 1 &&
           (bytes_ > max_bytes_ || entries_.size() > max_entries_)) {
      bytes_ -= Bytes(entries_.back().second);
      index_.erase(entries_.back().first);
      entries_.pop_back();
    }
  }

  using Entry = std::pair<Key, Bitmap>;
  std::mutex mutex_;
  std::list<Entry> entries_;  // Most recently used first.
  std::unordered_map<Key, std::list<Entry>::iterator, Hash> index_;
  size_t bytes_ = 0;
  size_t max_bytes_ = size_t{128} << 20;  // 128 MiB.
  size_t max_entries_ = 512;
};

// -----------------------------------------------------------------------------
// Geometry
// -----------------------------------------------------------------------------

// A size, in cells.
struct Extent {
  int width = 0;
  int height = 0;

  bool operator==(const Extent& other) const {
    return width == other.width && height == other.height;
  }
  bool operator!=(const Extent& other) const { return !(*this == other); }
};

// Round up, ignoring what is only due to the floating point arithmetic.
int Ceil(double value) {
  constexpr double epsilon = 1e-6;
  return std::max(1, static_cast<int>(std::ceil(value - epsilon)));
}

double AlignFactor(ImageAlign align) {
  switch (align) {
    case ImageAlign::Start:
      return 0.0;
    case ImageAlign::Center:
      return 0.5;
    case ImageAlign::End:
      return 1.0;
  }
  return 0.5;
}

// Where an image lands in the area it is given.
struct Placement {
  bool visible = false;

  // The part of the area which is covered by the image: [x0, x1[ x [y0, y1[.
  // This is expressed in "units", relative to the top-left of the area. A unit
  // is whatever the renderer draws: a pixel, or half a cell.
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;

  // The part of the image which is displayed there, in pixels of the image.
  double source_x = 0.0;
  double source_y = 0.0;
  double source_width = 0.0;
  double source_height = 0.0;
};

// The zoom factors applied to an image of `image` pixels, displayed in an area
// of `area` pixels.
void ComputeScale(const ImageOptions& options,
                  double area_width,
                  double area_height,
                  double image_width,
                  double image_height,
                  double* scale_x,
                  double* scale_y) {
  const double fit_x = area_width / image_width;
  const double fit_y = area_height / image_height;
  auto clamp = [&](double scale) {
    if (options.max_scale > 0.F) {
      scale = std::min(scale, double(options.max_scale));
    }
    if (options.min_scale > 0.F) {
      scale = std::max(scale, double(options.min_scale));
    }
    return scale;
  };

  switch (options.fit) {
    case ImageFit::Contain:
      *scale_x = *scale_y = clamp(std::min(fit_x, fit_y));
      return;
    case ImageFit::ScaleDown:
      *scale_x = *scale_y = clamp(std::min({fit_x, fit_y, 1.0}));
      return;
    case ImageFit::Cover:
      *scale_x = *scale_y = clamp(std::max(fit_x, fit_y));
      return;
    case ImageFit::None:
      *scale_x = *scale_y = clamp(1.0);
      return;
    case ImageFit::Fill:
      *scale_x = clamp(fit_x);
      *scale_y = clamp(fit_y);
      return;
  }
}

// - `columns`, `rows`: the size of the area, in cells.
// - `cell`: the size of a cell, in pixels.
// - `units_x`, `units_y`: the number of units per cell.
Placement Place(const ImageOptions& options,
                const Bitmap& bitmap,
                int columns,
                int rows,
                Dimensions cell,
                int units_x,
                int units_y) {
  Placement out;
  if (bitmap.empty() || columns <= 0 || rows <= 0) {
    return out;
  }
  const double image_width = bitmap.width();
  const double image_height = bitmap.height();
  const double area_width = double(columns) * cell.dimx;
  const double area_height = double(rows) * cell.dimy;

  // In pixels:
  double scale_x = 1.0;
  double scale_y = 1.0;
  ComputeScale(options, area_width, area_height, image_width, image_height,
               &scale_x, &scale_y);
  const double width = image_width * scale_x;
  const double height = image_height * scale_y;
  const double left = (area_width - width) * AlignFactor(options.align_x);
  const double top = (area_height - height) * AlignFactor(options.align_y);

  // In units:
  const double unit_x = double(units_x) / cell.dimx;
  const double unit_y = double(units_y) / cell.dimy;
  const auto x0 = static_cast<int>(std::lround(left * unit_x));
  const auto y0 = static_cast<int>(std::lround(top * unit_y));
  const int x1 =
      std::max(x0 + 1, static_cast<int>(std::lround((left + width) * unit_x)));
  const int y1 =
      std::max(y0 + 1, static_cast<int>(std::lround((top + height) * unit_y)));

  // What overflows from the area is cropped.
  out.x0 = std::max(x0, 0);
  out.y0 = std::max(y0, 0);
  out.x1 = std::min(x1, columns * units_x);
  out.y1 = std::min(y1, rows * units_y);
  if (out.x0 >= out.x1 || out.y0 >= out.y1) {
    return out;
  }
  out.source_x = image_width * (out.x0 - x0) / (x1 - x0);
  out.source_y = image_height * (out.y0 - y0) / (y1 - y0);
  out.source_width = image_width * (out.x1 - out.x0) / (x1 - x0);
  out.source_height = image_height * (out.y1 - out.y0) / (y1 - y0);
  out.visible = true;
  return out;
}

// -----------------------------------------------------------------------------
// Element
// -----------------------------------------------------------------------------

class ImageNode : public Node {
 public:
  ImageNode(std::string source, ImageOptions options)
      : source_(std::move(source)), options_(std::move(options)) {
    Sanitize();
  }

  ImageNode(Bitmap bitmap, ImageOptions options)
      : options_(std::move(options)),
        resolved_(true),
        status_(bitmap.empty() ? ImageStatus::Failed : ImageStatus::Ready),
        bitmap_(std::move(bitmap)) {
    Sanitize();
  }

  void ComputeRequirement() override {
    Resolve();
    preferred_ = has_hint_ ? hint_ : Preferred();

    requirement_ = Requirement();
    requirement_.min_x =
        preferred_.width + options_.margin.left + options_.margin.right;
    requirement_.min_y =
        preferred_.height + options_.margin.top + options_.margin.bottom;
    requirement_.flex_grow_x = options_.grow ? 1 : 0;
    requirement_.flex_grow_y = options_.grow ? 1 : 0;
    requirement_.flex_shrink_x = options_.shrink ? 1 : 0;
    requirement_.flex_shrink_y = options_.shrink ? 1 : 0;
  }

  void SetBox(Box box) override {
    Node::SetBox(box);

    // The area of the image: the box, minus the margin.
    area_ = box;
    area_.x_min += options_.margin.left;
    area_.y_min += options_.margin.top;
    area_.x_max -= options_.margin.right;
    area_.y_max -= options_.margin.bottom;

    // No larger than the maximal size. Whatever is left is distributed
    // around, following the alignment.
    Bound(area_.x_min, area_.x_max, options_.max_width, options_.align_x);
    Bound(area_.y_min, area_.y_max, options_.max_height, options_.align_y);

    UpdateHint();
  }

  // The layout is done in one pass: an element tells what size it wants, then
  // it is given one. An image wants to keep its aspect ratio. When it is given
  // less than it asked along one axis, it needs less along the other one too.
  // Asking for one more pass lets the container reclaim it, instead of
  // leaving a blank.
  void Check(Status* status) override {
    Node::Check(status);
    constexpr int max_passes = 3;
    if (hint_changed_ && passes_ < max_passes) {
      passes_++;
      status->need_iteration = true;
    }
    hint_changed_ = false;
  }

  void Render(Screen& screen) override {
    Resolve();
    if (area_.x_min > area_.x_max || area_.y_min > area_.y_max) {
      return;
    }
    // Entirely hidden. For instance scrolled out of a frame.
    if (Box::Intersection(area_, screen.stencil).IsEmpty()) {
      return;
    }
    if (bitmap_.empty()) {
      RenderText(screen);
      return;
    }

    const bool sixel =
        options_.renderer == ImageRenderer::Sixel ||
        (options_.renderer == ImageRenderer::Auto && Terminal::SixelSupport());
    if (sixel) {
      RenderSixel(screen);
    } else {
      RenderBlocks(screen);
    }
  }

 private:
  void Sanitize() {
    auto positive = [](int& value) { value = std::max(0, value); };
    positive(options_.margin.left);
    positive(options_.margin.top);
    positive(options_.margin.right);
    positive(options_.margin.bottom);
    positive(options_.width);
    positive(options_.height);
    positive(options_.min_width);
    positive(options_.min_height);
    positive(options_.max_width);
    positive(options_.max_height);
  }

  // Get the image from its source.
  void Resolve() {
    if (resolved_) {
      return;
    }
    resolved_ = true;
    Loader::Result result = Loader::Get().Request(source_, options_.async);
    status_ = result.status;
    bitmap_ = std::move(result.bitmap);
  }

  int Columns() const { return area_.x_max - area_.x_min + 1; }
  int Rows() const { return area_.y_max - area_.y_min + 1; }

  // Shrink [min, max] to `limit` cells, if it is larger.
  static void Bound(int& min, int& max, int limit, ImageAlign align) {
    const int size = max - min + 1;
    if (limit <= 0 || size <= limit) {
      return;
    }
    min += static_cast<int>(std::lround((size - limit) * AlignFactor(align)));
    max = min + limit - 1;
  }

  // The size of the image this element asks for, in cells.
  Extent Preferred() const {
    Extent out = {options_.width, options_.height};
    if (bitmap_.empty()) {
      // Not loaded. Its size is unknown.
      out.width = std::max(out.width, options_.min_width);
      out.height = std::max(out.height, options_.min_height);
      return out;
    }

    const Dimensions cell = Terminal::CellPixelSize();
    // The original size of the image, in cells.
    const double width = double(bitmap_.width()) / cell.dimx;
    const double height = double(bitmap_.height()) / cell.dimy;
    const bool auto_width = (options_.width == 0);
    const bool auto_height = (options_.height == 0);

    if (auto_width && auto_height) {
      out = {Ceil(width), Ceil(height)};
    } else if (auto_width) {
      out.width = Ceil(out.height * width / height);
    } else if (auto_height) {
      out.height = Ceil(out.width * height / width);
    }

    // Apply the bounds. The dimension which was left automatic follows.
    if (options_.max_width > 0 && out.width > options_.max_width) {
      out.width = options_.max_width;
      if (auto_height) {
        out.height = Ceil(out.width * height / width);
      }
    }
    if (options_.max_height > 0 && out.height > options_.max_height) {
      out.height = options_.max_height;
      if (auto_width) {
        out.width = Ceil(out.height * width / height);
      }
    }
    if (out.width < options_.min_width) {
      out.width = options_.min_width;
      if (auto_height) {
        out.height = Ceil(out.width * height / width);
        if (options_.max_height > 0) {
          out.height = std::min(out.height, options_.max_height);
        }
      }
    }
    if (out.height < options_.min_height) {
      out.height = options_.min_height;
      if (auto_width) {
        out.width = Ceil(out.height * width / height);
        if (options_.max_width > 0) {
          out.width = std::min(out.width, options_.max_width);
        }
      }
    }
    return out;
  }

  // See Check().
  void UpdateHint() {
    if (bitmap_.empty()) {
      return;
    }
    if (options_.fit != ImageFit::Contain &&
        options_.fit != ImageFit::ScaleDown) {
      return;
    }
    const int columns = Columns();
    const int rows = Rows();
    if (columns <= 0 || rows <= 0) {
      return;
    }
    // Given what it asked for, or more.
    if (columns >= preferred_.width && rows >= preferred_.height) {
      return;
    }

    // The size the image is displayed at, in cells.
    const Dimensions cell = Terminal::CellPixelSize();
    double scale_x = 1.0;
    double scale_y = 1.0;
    ComputeScale(options_, double(columns) * cell.dimx,
                 double(rows) * cell.dimy, bitmap_.width(), bitmap_.height(),
                 &scale_x, &scale_y);
    const Extent displayed = {
        Ceil(bitmap_.width() * scale_x / cell.dimx),
        Ceil(bitmap_.height() * scale_y / cell.dimy),
    };

    const Extent hint = {
        std::max(options_.min_width,
                 std::min(preferred_.width, displayed.width)),
        std::max(options_.min_height,
                 std::min(preferred_.height, displayed.height)),
    };
    if (hint != preferred_) {
      hint_ = hint;
      has_hint_ = true;
      hint_changed_ = true;
    }
  }

  void RenderSixel(Screen& screen) {
    const Dimensions cell = Terminal::CellPixelSize();
    const Placement placement =
        Place(options_, bitmap_, Columns(), Rows(), cell, cell.dimx, cell.dimy);
    if (!placement.visible) {
      return;
    }

    Loader::Get().SetPrecomputePalette();

    Screen::Graphic graphic;
    graphic.bitmap = ScaledCache::Get().Scale(
        bitmap_, placement.source_x, placement.source_y, placement.source_width,
        placement.source_height, placement.x1 - placement.x0,
        placement.y1 - placement.y0, options_.filter);
    if (graphic.bitmap.empty()) {
      return;
    }

    // The cells touched by the image.
    const int column_min = placement.x0 / cell.dimx;
    const int column_max = (placement.x1 - 1) / cell.dimx;
    const int row_min = placement.y0 / cell.dimy;
    const int row_max = (placement.y1 - 1) / cell.dimy;

    graphic.x = area_.x_min + column_min;
    graphic.y = area_.y_min + row_min;
    graphic.width = column_max - column_min + 1;
    graphic.height = row_max - row_min + 1;
    graphic.cell_width = cell.dimx;
    graphic.cell_height = cell.dimy;
    graphic.offset_x = placement.x0 - column_min * cell.dimx;
    graphic.offset_y = placement.y0 - row_min * cell.dimy;
    // The palette is the one of the original image: it doesn't depend on the
    // size it is displayed at.
    graphic.palette =
        SixelPalette::For(bitmap_, options_.colors, options_.background);
    graphic.colors = options_.colors;
    graphic.dither = options_.dither;
    graphic.background = options_.background;
    screen.DrawGraphic(std::move(graphic));
  }

  // Draw using two "pixels" per cell: its upper half, and its lower half.
  void RenderBlocks(Screen& screen) {
    const Dimensions cell = Terminal::CellPixelSize();
    const Placement placement =
        Place(options_, bitmap_, Columns(), Rows(), cell, 1, 2);
    if (!placement.visible) {
      return;
    }

    const int width = placement.x1 - placement.x0;
    const int height = placement.y1 - placement.y0;
    const Bitmap scaled = ScaledCache::Get().Scale(
        bitmap_, placement.source_x, placement.source_y, placement.source_width,
        placement.source_height, width, height, options_.filter);
    if (scaled.empty()) {
      return;
    }

    const bool has_background = options_.background.GetAlpha() != 0;
    const int background_red = options_.background.GetRed();
    const int background_green = options_.background.GetGreen();
    const int background_blue = options_.background.GetBlue();
    const bool blocks = Terminal::GetQuirks().BlockCharacters();

    struct Half {
      bool drawn = false;
      int red = 0;
      int green = 0;
      int blue = 0;
    };
    auto half = [&](int x, int y) {
      Half out;
      if (y < 0 || y >= height) {
        return out;
      }
      const uint8_t* pixel =
          scaled.data() + (static_cast<size_t>(y) * static_cast<size_t>(width) +
                           static_cast<size_t>(x)) *
                              4;
      const int alpha = pixel[3];  // NOLINT
      out.red = pixel[0];          // NOLINT
      out.green = pixel[1];        // NOLINT
      out.blue = pixel[2];         // NOLINT
      if (has_background) {
        out.red = (out.red * alpha + background_red * (255 - alpha)) / 255;
        out.green =
            (out.green * alpha + background_green * (255 - alpha)) / 255;
        out.blue = (out.blue * alpha + background_blue * (255 - alpha)) / 255;
        out.drawn = true;
      } else {
        constexpr int alpha_threshold = 128;
        out.drawn = (alpha >= alpha_threshold);
      }
      return out;
    };
    auto color = [](const Half& value) {
      return Color(static_cast<uint8_t>(value.red),
                   static_cast<uint8_t>(value.green),
                   static_cast<uint8_t>(value.blue));
    };

    const int row_min = placement.y0 / 2;
    const int row_max = (placement.y1 - 1) / 2;
    for (int row = row_min; row <= row_max; ++row) {
      for (int x = 0; x < width; ++x) {
        const Half up = half(x, row * 2 - placement.y0);
        const Half down = half(x, row * 2 + 1 - placement.y0);
        if (!up.drawn && !down.drawn) {
          continue;
        }
        Cell& out =
            screen.CellAt(area_.x_min + placement.x0 + x, area_.y_min + row);
        out.graphic = 0;
        if (!blocks) {
          // No block characters: one color per cell.
          Half mean = up.drawn ? up : down;
          if (up.drawn && down.drawn) {
            mean.red = (up.red + down.red) / 2;
            mean.green = (up.green + down.green) / 2;
            mean.blue = (up.blue + down.blue) / 2;
          }
          out.character = " ";
          out.background_color = color(mean);
          continue;
        }
        if (up.drawn && down.drawn) {
          out.character = "▀";
          out.foreground_color = color(up);
          out.background_color = color(down);
        } else if (up.drawn) {
          out.character = "▀";
          out.foreground_color = color(up);
        } else {
          out.character = "▄";
          out.foreground_color = color(down);
        }
      }
    }
  }

  // What is displayed in place of an image which can't be.
  void RenderText(Screen& screen) {
    if (status_ != ImageStatus::Failed) {
      return;
    }
    std::string text = options_.alt;
    if (text.empty()) {
      // The name of the file.
      const size_t slash = source_.find_last_of("/\\");
      text = (slash == std::string::npos) ? source_ : source_.substr(slash + 1);
    }
    const std::vector<std::string> glyphs = Utf8ToGlyphs(text);
    const int columns = Columns();
    const int count = std::min(static_cast<int>(glyphs.size()), columns);
    int x = area_.x_min + (columns - count) / 2;
    const int y = area_.y_min + (Rows() - 1) / 2;
    for (int i = 0; i < count; ++i) {
      Cell& cell = screen.CellAt(x++, y);
      cell.character = glyphs[static_cast<size_t>(i)];
      cell.dim = true;
    }
  }

  std::string source_;
  ImageOptions options_;

  bool resolved_ = false;
  ImageStatus status_ = ImageStatus::Unknown;
  Bitmap bitmap_;

  // Layout:
  Box area_;
  Extent preferred_;
  Extent hint_;
  bool has_hint_ = false;
  bool hint_changed_ = false;
  int passes_ = 0;
};

}  // namespace

// static
ImageOptions ImageOptions::ImageView() {
  ImageOptions options;
  options.grow = false;
  options.align_x = ImageAlign::Start;
  options.align_y = ImageAlign::Start;
  return options;
}

Element image(std::string_view path_or_url, ImageOptions options) {
  return std::make_shared<ImageNode>(std::string(path_or_url),
                                     std::move(options));
}

Element image(Bitmap bitmap, ImageOptions options) {
  return std::make_shared<ImageNode>(std::move(bitmap), std::move(options));
}

ImageStatus GetImageStatus(std::string_view path_or_url, std::string* error) {
  Loader::Result result = Loader::Get().Peek(std::string(path_or_url));
  if (error) {
    *error = std::move(result.error);
  }
  return result.status;
}

void SetImageLoadedCallback(std::function<void()> callback) {
  Loader::Get().SetUserCallback(std::move(callback));
}

void ReloadImage(std::string_view path_or_url) {
  Loader::Get().Forget(std::string(path_or_url));
}

void ClearImageCache() {
  Loader::Get().Clear();
}

void SetImageCacheSize(size_t max_images) {
  Loader::Get().SetMaxEntries(max_images);
}

void SetScaledImageCacheSize(size_t max_bytes) {
  ScaledCache::Get().SetMaxBytes(max_bytes);
}

void SetMaxConcurrentImageLoads(int max_concurrent) {
  Loader::Get().SetMaxWorkers(max_concurrent);
}

// --- ftxui-image-view ---

Element image_view(std::string_view url) {
  return image(url, ImageOptions::ImageView());
}

Element image_view(std::string_view url, ImageOptions options) {
  return image(url, std::move(options));
}

void setOnImageLoadedCallback(std::function<void()> callback) {
  SetImageLoadedCallback(std::move(callback));
}

void setImageCacheMaxSize(size_t max_entries) {
  SetImageCacheSize(max_entries);
}

void setImageResizeCacheMaxSize(size_t max_entries) {
  ScaledCache::Get().SetMaxEntries(max_entries);
}

void setImageCharCacheMaxSize(size_t max_entries) {
  // Images are no longer converted to characters.
  (void)max_entries;
}

void setMaxConcurrentImageLoads(int max_concurrent) {
  SetMaxConcurrentImageLoads(max_concurrent);
}

namespace internal {
void SetImageRefreshHook(std::function<void()> hook) {
  Loader::Get().SetRefreshHook(std::move(hook));
}
}  // namespace internal

}  // namespace ftxui
