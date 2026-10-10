// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#include <algorithm>  // for std::search
#include <cctype>     // for std::tolower
#include <chrono>     // for steady_clock, milliseconds
#include <cstdlib>    // for strtol
#include <initializer_list>
#include <string>
#include <string_view>  // for string_view

#include "ftxui/screen/terminal.hpp"
#include "ftxui/screen/util.hpp"  // for util::GetEnv

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#else
#include <fcntl.h>      // for open, O_RDWR, O_NOCTTY
#include <poll.h>       // for poll, pollfd, POLLIN
#include <sys/ioctl.h>  // for winsize, ioctl, TIOCGWINSZ
#include <termios.h>    // for termios, tcgetattr, tcsetattr
#include <unistd.h>     // for STDOUT_FILENO, read, write, close
#endif
#if defined(__sun) || defined(__illumos__)
#include <sys/termios.h>  // for winsize on illumos
#endif

namespace ftxui {

namespace {

std::unique_ptr<Terminal::Quirks> g_quirks;
bool g_color_support_detected = false;

bool& ColorSupportDetected() {
  return g_color_support_detected;
}

Terminal::Quirks& GetQuirksInternal() {
  if (!g_quirks) {
    g_quirks = std::make_unique<Terminal::Quirks>();
#if defined(_WIN32)
    g_quirks->SetBlockCharacters(false);
    g_quirks->SetCursorHiding(false);
    g_quirks->SetComponentAscii(true);
#endif
  }
  return *g_quirks;
}

Dimensions& FallbackSize() {
#if defined(__EMSCRIPTEN__)
  // This dimension was chosen arbitrarily to be able to display:
  // https://arthursonzogni.com/FTXUI/examples
  // This will have to be improved when someone has time to implement and need
  // it.
  constexpr int fallback_width = 140;
  constexpr int fallback_height = 43;
#else
  // The terminal size in VT100 was 80x24. It is still used nowadays by
  // default in many terminal emulator. That's a good choice for a fallback
  // value.
  constexpr int fallback_width = 80;
  constexpr int fallback_height = 24;
#endif
  static Dimensions g_fallback_size{
      fallback_width,
      fallback_height,
  };
  return g_fallback_size;
}

bool Contains(std::string_view s, std::string_view key) {
  if (key.empty()) {
    return true;
  }
  const auto it = std::search(  // NOLINT
      s.begin(), s.end(), key.begin(), key.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
      });
  return it != s.end();
}

bool ContainsAny(std::string_view s,
                 std::initializer_list<std::string_view> keys) {
  for (const std::string_view key : keys) {
    if (Contains(s, key)) {
      return true;
    }
  }
  return false;
}

// --- Graphics ---------------------------------------------------------------

// Whether something was explicitly set, or left to the detection.
enum class Tristate : uint8_t { Unset, False, True };

struct Graphics {
  // Sixel support:
  Tristate sixel_forced = Tristate::Unset;
  Tristate sixel_detected = Tristate::Unset;

  // Whether an image may reach the last row of a screen.
  Tristate last_row_forced = Tristate::Unset;
  Tristate last_row_detected = Tristate::Unset;

  // Cell size, in pixels. Zero means unknown.
  Dimensions cell_forced = {0, 0};
  Dimensions cell_reported = {0, 0};
};

Graphics& GetGraphics() {
  static Graphics g_graphics;
  return g_graphics;
}

// Parse an environment variable holding a boolean.
Tristate TristateFromEnvironment(const char* name) {
  const std::string_view value = util::GetEnv(name);
  if (value.empty()) {
    return Tristate::Unset;
  }
  if (value == "0" || ContainsAny(value, {"false", "off", "no"})) {
    return Tristate::False;
  }
  return Tristate::True;
}

// Parse the FTXUI_CELL_SIZE environment variable. Example: "10x20".
Dimensions CellSizeFromEnvironment() {
  const char* value = util::GetEnv("FTXUI_CELL_SIZE");
  char* end = nullptr;
  const long width = std::strtol(value, &end, 10);  // NOLINT
  if (end == value || (*end != 'x' && *end != 'X')) {
    return {0, 0};
  }
  const long height = std::strtol(end + 1, nullptr, 10);  // NOLINT
  const long max = 1024;
  if (width <= 0 || height <= 0 || width > max || height > max) {
    return {0, 0};
  }
  return {static_cast<int>(width), static_cast<int>(height)};
}

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
// What the operating system knows about the size of a cell, given a file
// descriptor of the terminal.
Dimensions CellSizeFromFileDescriptor(int fd) {
  winsize w{};
  const int status = ioctl(fd, TIOCGWINSZ, &w);  // NOLINT
  if (status < 0 || w.ws_col == 0 || w.ws_row == 0 || w.ws_xpixel == 0 ||
      w.ws_ypixel == 0) {
    return {0, 0};
  }
  // Rounded down: an image must never overflow from its cells.
  const Dimensions out = {w.ws_xpixel / w.ws_col, w.ws_ypixel / w.ws_row};
  if (out.dimx <= 0 || out.dimy <= 0) {
    return {0, 0};
  }
  return out;
}
#endif

// What the operating system knows about the size of a cell.
Dimensions CellSizeFromSystem() {
#if defined(__EMSCRIPTEN__) || defined(_WIN32)
  return {0, 0};
#else
  // The output is the terminal, unless it is redirected. Then, the other
  // standard streams are likely to be.
  for (const int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
    const Dimensions out = CellSizeFromFileDescriptor(fd);
    if (out.dimx > 0) {
      return out;
    }
  }
  return {0, 0};
#endif
}

Terminal::Color ComputeColorSupportInternal() {
  static const std::vector<int> empty_capabilities;
  return Terminal::ComputeColorSupport(
      util::GetEnv("TERM"), util::GetEnv("COLORTERM"),
      util::GetEnv("TERM_PROGRAM"), "unknown", "unknown", empty_capabilities);
}

}  // namespace

namespace Terminal {

struct Quirks::Impl {
  bool block_characters = true;
  bool cursor_hiding = true;
  bool component_ascii = false;
  Color color_support = Palette256;
};

Quirks::Quirks() : impl_(std::make_unique<Impl>()) {}
Quirks::~Quirks() = default;
Quirks::Quirks(const Quirks& other)
    : impl_(std::make_unique<Impl>(*other.impl_)) {}
Quirks& Quirks::operator=(const Quirks& other) {
  if (this != &other) {
    *impl_ = *other.impl_;
  }
  return *this;
}
Quirks::Quirks(Quirks&&) noexcept = default;
Quirks& Quirks::operator=(Quirks&&) noexcept = default;

bool Quirks::BlockCharacters() const {
  return impl_->block_characters;
}
void Quirks::SetBlockCharacters(bool v) {
  impl_->block_characters = v;
}

bool Quirks::CursorHiding() const {
  return impl_->cursor_hiding;
}
void Quirks::SetCursorHiding(bool v) {
  impl_->cursor_hiding = v;
}

bool Quirks::ComponentAscii() const {
  return impl_->component_ascii;
}
void Quirks::SetComponentAscii(bool v) {
  impl_->component_ascii = v;
}

Color Quirks::ColorSupport() const {
  return impl_->color_support;
}
void Quirks::SetColorSupport(Color v) {
  impl_->color_support = v;
}

struct TerminalInfo::Impl {
  std::string term;
  std::string colorterm;
  std::string term_program;
  std::string terminal_name;
  std::string terminal_emulator_name;
  std::vector<int> capabilities;
};

TerminalInfo::TerminalInfo() : impl_(std::make_unique<Impl>()) {}
TerminalInfo::~TerminalInfo() = default;
TerminalInfo::TerminalInfo(TerminalInfo&&) noexcept = default;
TerminalInfo& TerminalInfo::operator=(TerminalInfo&&) noexcept = default;

void TerminalInfo::SetTerm(std::string_view term) {
  impl_->term = term;
}
void TerminalInfo::SetColorterm(std::string_view colorterm) {
  impl_->colorterm = colorterm;
}
void TerminalInfo::SetTermProgram(std::string_view term_program) {
  impl_->term_program = term_program;
}
void TerminalInfo::SetTerminalName(std::string_view terminal_name) {
  impl_->terminal_name = terminal_name;
}
void TerminalInfo::SetTerminalEmulatorName(
    std::string_view terminal_emulator_name) {
  impl_->terminal_emulator_name = terminal_emulator_name;
}
void TerminalInfo::SetCapabilities(std::vector<int> capabilities) {
  impl_->capabilities = std::move(capabilities);
}

/// @brief Compute the color support based on environment variables and terminal
/// identification.
/// @param term The TERM environment variable.
/// @param colorterm The COLORTERM environment variable.
/// @param term_program The TERM_PROGRAM environment variable.
/// @param terminal_name The terminal name (from DA2).
/// @param terminal_emulator_name The terminal emulator name (from XTVERSION).
/// @param capabilities The terminal capabilities (from DA1).
Color ComputeColorSupport(std::string_view term,
                          std::string_view colorterm,
                          std::string_view term_program,
                          std::string_view terminal_name,
                          std::string_view terminal_emulator_name,
                          const std::vector<int>& capabilities) {
  TerminalInfo info;
  info.SetTerm(term);
  info.SetColorterm(colorterm);
  info.SetTermProgram(term_program);
  info.SetTerminalName(terminal_name);
  info.SetTerminalEmulatorName(terminal_emulator_name);
  info.SetCapabilities(capabilities);
  return info.ComputeColorSupport();
}

Color TerminalInfo::ComputeColorSupport() const {
  // TODO(v8.0.0): Read NO_COLOR and WT_SESSION from
  // ComputeColorSupportInternal() and pass them in as parameters, so that this
  // function remains a pure function of its inputs. This requires extending the
  // public Terminal::ComputeColorSupport() signature, i.e. an API-breaking
  // change.

  // 0. User preference. See https://no-color.org.
  if (util::GetEnv("NO_COLOR")[0] != '\0') {
    return Terminal::Color::Palette1;
  }

  // 1. Platform specific overrides.
#if defined(__EMSCRIPTEN__)
  return Terminal::Color::TrueColor;
#endif
#if defined(_WIN32)
  // Check if we are running in a console, and if that console supports VT processing.
  auto stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD out_mode = 0;
  if (GetConsoleMode(stdout_handle, &out_mode)) {
    const int enable_virtual_terminal_processing = 0x0004;
    const int disable_newline_auto_return = 0x0008;
    out_mode |= enable_virtual_terminal_processing;
    out_mode |= disable_newline_auto_return;
    if (!SetConsoleMode(stdout_handle, out_mode)) {
      return Terminal::Color::Palette16;
    }
  }
  return Terminal::Color::TrueColor;
#endif

  // Check WT_SESSION for Windows Terminal (e.g. when running under WSL).
  if (util::GetEnv("WT_SESSION")[0] != '\0') {
    return Terminal::Color::TrueColor;
  }

  // 2. term / colorterm environment variables explicitly advertising 24bit
  // colors.
  if (ContainsAny(impl_->colorterm, {"24bit", "truecolor"})) {
    return Terminal::Color::TrueColor;
  }
  if (ContainsAny(impl_->term,
                  {"direct", "truecolor", "kitty", "alacritty", "foot"})) {
    return Terminal::Color::TrueColor;
  }

  // 3. term_program
  if (ContainsAny(impl_->term_program, {
                                           "iterm",
                                           "vscode",
                                           "warp",
                                           "ghostty",
                                           "wezterm",
                                       })) {
    return Terminal::Color::TrueColor;
  }
  // Apple's Terminal.app (TERM_PROGRAM=Apple_Terminal) supports 256 colors,
  // but not 24bit ones.
  if (Contains(impl_->term_program, "apple_terminal")) {
    return Terminal::Color::Palette256;
  }

  // 4. terminal identification.
  // An empty name means the terminal was not identified, the same as
  // "unknown".
  if (!impl_->terminal_emulator_name.empty() &&
      impl_->terminal_emulator_name != "unknown") {
    return Terminal::Color::TrueColor;
  }
  if (impl_->terminal_name == "xterm") {
    return Terminal::Color::TrueColor;
  }

  // 5. Generic term / colorterm environment variables. They are checked after
  // the terminal identification, because they are weak evidence: most
  // terminals set TERM=xterm-256color, whatever their capabilities.
  if (ContainsAny(impl_->colorterm, {"256"}) ||
      ContainsAny(impl_->term, {"256", "xterm", "screen", "tmux"})) {
    return Terminal::Color::Palette256;
  }

  // 6. Terminal capabilities.
  for (const int x : impl_->capabilities) {
    // The value 22 is the SGR capability for 256 colors. If the terminal
    // supports it, it is a strong indication that the terminal supports 256
    // colors. This is not a perfect detection method, but it is a reasonable
    // heuristic in the absence of more specific information.
    if (x == 22) {
      return Terminal::Color::Palette256;
    }
  }

  return Terminal::Color::Palette16;
}

/// @brief Get the terminal size.
/// @return The terminal size.
/// @ingroup screen
Dimensions Size() {
#if defined(__EMSCRIPTEN__)
  // This dimension was chosen arbitrarily to be able to display:
  // https://arthursonzogni.com/FTXUI/examples
  // This will have to be improved when someone has time to implement and need
  // it.
  return FallbackSize();
#elif defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    return Dimensions{csbi.srWindow.Right - csbi.srWindow.Left + 1,
                      csbi.srWindow.Bottom - csbi.srWindow.Top + 1};
  }

  return FallbackSize();
#else
  winsize w{};
  const int status = ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);  // NOLINT
  // The ioctl return value result should be checked. Some operating systems
  // don't support TIOCGWINSZ.
  if (w.ws_col == 0 || w.ws_row == 0 || status < 0) {
    return FallbackSize();
  }
  return Dimensions{w.ws_col, w.ws_row};
#endif
}

/// @brief Override terminal size in case auto-detection fails
/// @param fallbackSize Terminal dimensions to fallback to
void SetFallbackSize(const Dimensions& fallbackSize) {
  FallbackSize() = fallbackSize;
}

/// @brief Get the color support of the terminal.
/// @ingroup screen
Color ColorSupport() {
  if (!ColorSupportDetected()) {
    GetQuirksInternal().SetColorSupport(ComputeColorSupportInternal());
    ColorSupportDetected() = true;
  }
  return GetQuirksInternal().ColorSupport();
}

/// @brief Override terminal color support in case auto-detection fails
/// @ingroup dom
void SetColorSupport(Color color) {
  GetQuirksInternal().SetColorSupport(color);
  ColorSupportDetected() = true;
}

/// @brief Get the size of a terminal cell, in pixels.
/// @ingroup screen
Dimensions CellPixelSize() {
  const Graphics& graphics = GetGraphics();
  if (graphics.cell_forced.dimx > 0 && graphics.cell_forced.dimy > 0) {
    return graphics.cell_forced;
  }

  static const Dimensions environment = CellSizeFromEnvironment();
  if (environment.dimx > 0) {
    return environment;
  }

  const Dimensions system = CellSizeFromSystem();
  if (system.dimx > 0) {
    return system;
  }

  if (graphics.cell_reported.dimx > 0 && graphics.cell_reported.dimy > 0) {
    return graphics.cell_reported;
  }

  // The cell size of the VT340.
  constexpr int default_width = 10;
  constexpr int default_height = 20;
  return {default_width, default_height};
}

/// @brief Override the size of a terminal cell, in pixels.
/// @ingroup screen
void SetCellPixelSize(const Dimensions& size) {
  GetGraphics().cell_forced = size;
}

/// @brief Record the size of a terminal cell reported by the terminal.
/// @ingroup screen
void SetReportedCellPixelSize(const Dimensions& size) {
  GetGraphics().cell_reported = size;
}

/// @brief Whether images are drawn using the sixel protocol.
/// @ingroup screen
bool SixelSupport() {
  const Graphics& graphics = GetGraphics();
  if (graphics.sixel_forced != Tristate::Unset) {
    return graphics.sixel_forced == Tristate::True;
  }
  static const Tristate environment = TristateFromEnvironment("FTXUI_SIXEL");
  if (environment != Tristate::Unset) {
    return environment == Tristate::True;
  }
  return graphics.sixel_detected == Tristate::True;
}

/// @brief Whether a sixel image may reach the last row of a screen.
/// @ingroup screen
bool SixelLastRowSupport() {
  const Graphics& graphics = GetGraphics();
  if (graphics.last_row_forced != Tristate::Unset) {
    return graphics.last_row_forced == Tristate::True;
  }
  static const Tristate environment =
      TristateFromEnvironment("FTXUI_SIXEL_LAST_ROW");
  if (environment != Tristate::Unset) {
    return environment == Tristate::True;
  }
  return graphics.last_row_detected == Tristate::True;
}

/// @brief Force allowing, or not, sixel images on the last row of a screen.
/// @ingroup screen
void SetSixelLastRowSupport(bool supported) {
  GetGraphics().last_row_forced = supported ? Tristate::True : Tristate::False;
}

/// @brief Record whether the terminal knows about the DEC private mode 8452.
/// @ingroup screen
void SetDetectedSixelLastRowSupport(bool supported) {
  GetGraphics().last_row_detected =
      supported ? Tristate::True : Tristate::False;
}

/// @brief Force enabling or disabling the sixel protocol.
/// @ingroup screen
void SetSixelSupport(bool supported) {
  GetGraphics().sixel_forced = supported ? Tristate::True : Tristate::False;
}

/// @brief Record whether the terminal advertised supporting sixel.
/// @ingroup screen
void SetDetectedSixelSupport(bool supported) {
  GetGraphics().sixel_detected = supported ? Tristate::True : Tristate::False;
}

/// @brief Ask the terminal whether it supports sixel, and the size of its
/// cells.
/// @ingroup screen
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool QueryGraphics(int timeout_ms) {
#if defined(__EMSCRIPTEN__) || defined(_WIN32)
  (void)timeout_ms;
  return SixelSupport();
#else
  const int fd = open("/dev/tty", O_RDWR | O_NOCTTY);  // NOLINT
  if (fd < 0) {
    return SixelSupport();
  }

  // The reply must be read as it comes, without being echoed.
  termios original{};
  if (tcgetattr(fd, &original) != 0) {
    close(fd);
    return SixelSupport();
  }
  termios raw = original;
  raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(fd, TCSANOW, &raw);

  // 1. CSI 16 t: the size of a cell, in pixels. Reply: CSI 6 ; height ; width t
  // 2. CSI ? 8452 $ p: whether the mode 8452 is known.
  //    Reply: CSI ? 8452 ; state $ y
  // 3. CSI c: the device attributes. Reply: CSI ? ... c. It includes 4 when
  //    sixel is supported. Every terminal replies to this one. It is sent
  //    last, so its reply tells there is nothing more to wait for.
  const std::string_view query = "\x1B[16t\x1B[?8452$p\x1B[c";
  std::string reply;
  if (write(fd, query.data(), query.size()) ==
      static_cast<ssize_t>(query.size())) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (true) {
      // The reply to the device attributes is the only one ending with 'c'.
      if (reply.find('c') != std::string::npos) {
        break;
      }
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              deadline - std::chrono::steady_clock::now())
              .count();
      if (remaining <= 0) {
        break;
      }
      pollfd poller = {fd, POLLIN, 0};
      if (poll(&poller, 1, static_cast<int>(remaining)) <= 0) {
        break;
      }
      char buffer[256];                                       // NOLINT
      const ssize_t size = read(fd, buffer, sizeof(buffer));  // NOLINT
      if (size <= 0) {
        break;
      }
      reply.append(buffer, static_cast<size_t>(size));  // NOLINT
    }
  }

  tcsetattr(fd, TCSANOW, &original);

  // The size of a cell, in case the terminal doesn't reply about it, and none
  // of the standard streams is the terminal.
  const Dimensions cell_size = CellSizeFromFileDescriptor(fd);
  if (cell_size.dimx > 0) {
    SetReportedCellPixelSize(cell_size);
  }
  close(fd);

  // Parse the replies. Each one is: ESC [ <parameters> <final character>
  size_t position = 0;
  while ((position = reply.find("\x1B[", position)) != std::string::npos) {
    position += 2;
    const bool question = position < reply.size() && reply[position] == '?';
    if (question) {
      position++;
    }
    std::vector<int> arguments = {0};
    while (position < reply.size() &&
           ((reply[position] >= '0' && reply[position] <= '9') ||
            reply[position] == ';')) {
      if (reply[position] == ';') {
        arguments.push_back(0);
      } else if (arguments.back() < 100000) {
        arguments.back() = arguments.back() * 10 + (reply[position] - '0');
      }
      position++;
    }
    const bool dollar = position < reply.size() && reply[position] == '$';
    if (dollar) {
      position++;
    }
    if (position >= reply.size()) {
      break;
    }
    const char final_character = reply[position];
    constexpr int sixel_cursor_mode = 8452;
    if (final_character == 'y' && question && dollar && arguments.size() == 2 &&
        arguments[0] == sixel_cursor_mode) {
      // 0: unknown, 1: set, 2: reset, 3: always set, 4: always reset.
      SetDetectedSixelLastRowSupport(arguments[1] >= 1 && arguments[1] <= 3);
    }
    if (final_character == 't' && !question && arguments.size() == 3 &&
        arguments[0] == 6 && arguments[1] > 0 && arguments[2] > 0) {
      SetReportedCellPixelSize({arguments[2], arguments[1]});
    }
    if (final_character == 'c' && question) {
      SetDetectedSixelSupport(
          std::find(arguments.begin(), arguments.end(), 4) != arguments.end());
    }
  }
  return SixelSupport();
#endif
}

/// @brief Get the terminal quirks.
/// @ingroup screen
Quirks GetQuirks() {
  if (!ColorSupportDetected()) {
    GetQuirksInternal().SetColorSupport(ComputeColorSupportInternal());
    ColorSupportDetected() = true;
  }
  return GetQuirksInternal();
}

/// @brief Override terminal quirks.
/// @ingroup screen
void SetQuirks(const Quirks& quirks) {
  GetQuirksInternal() = quirks;
  ColorSupportDetected() = true;
}

}  // namespace Terminal
}  // namespace ftxui
