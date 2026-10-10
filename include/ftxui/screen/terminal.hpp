// Copyright 2020 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_SCREEN_TERMINAL_HPP
#define FTXUI_SCREEN_TERMINAL_HPP

#include <cstdint>

#include <memory>
#include <string_view>
#include <vector>

#include "ftxui/util/export.hpp"

namespace ftxui {

/// @brief Dimensions is a structure that represents the size of the terminal
/// @ingroup screen
struct FTXUI_EXPORT(SCREEN) Dimensions {
  int dimx;
  int dimy;
};

namespace Terminal {
FTXUI_EXPORT(SCREEN) Dimensions Size();
FTXUI_EXPORT(SCREEN) void SetFallbackSize(const Dimensions& fallbackSize);

/// @brief Color is an enumeration that represents the color support of the
/// terminal.
/// @ingroup screen
enum Color : uint8_t {
  Palette1,
  Palette16,
  Palette256,
  TrueColor,
};
FTXUI_EXPORT(SCREEN) Color ColorSupport();
FTXUI_EXPORT(SCREEN) void SetColorSupport(Color color);

// --- Graphics ---------------------------------------------------------------
//
// A terminal draws text on a grid of cells. Some terminals are also able to
// draw pixels, using the sixel protocol. This is what the ftxui::image element
// uses. Drawing pixels requires knowing two things about the terminal: whether
// it supports sixel, and how many pixels a cell is made of.

/// @brief The size of one terminal cell, in pixels.
///
/// By order of priority, this is:
/// 1. The value given to SetCellPixelSize().
/// 2. The `FTXUI_CELL_SIZE` environment variable. Example: `10x20`.
/// 3. What the operating system knows about the terminal window.
/// 4. What the terminal replied when it was asked. An ftxui::App asks.
/// 5. 10x20, the cell size of the VT340, the original sixel terminal.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) Dimensions CellPixelSize();

/// @brief Override the size of a terminal cell, in pixels. Use {0, 0} to get
/// back to the automatic detection.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) void SetCellPixelSize(const Dimensions& size);

/// @brief Record the cell size replied by the terminal to the query
/// `CSI 16 t`. This is called by ftxui::App.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) void SetReportedCellPixelSize(const Dimensions& size);

/// @brief Whether images are drawn using the sixel protocol.
///
/// By order of priority, this is:
/// 1. The value given to SetSixelSupport().
/// 2. The `FTXUI_SIXEL` environment variable: `1` or `0`.
/// 3. What the terminal replied when it was asked. An ftxui::App asks. For a
///    program printing a ftxui::Screen without an App, see QueryGraphics().
/// 4. false.
///
/// When sixel isn't supported, images are drawn using unicode half blocks.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) bool SixelSupport();

/// @brief Force enabling or disabling the sixel protocol, whatever the
/// terminal says.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) void SetSixelSupport(bool supported);

/// @brief Record whether the terminal advertised supporting sixel, in its
/// reply to the "Device Attributes" query. This is called by ftxui::App.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) void SetDetectedSixelSupport(bool supported);

/// @brief Whether a sixel image may reach the last row of a ftxui::Screen.
///
/// After drawing an image, many terminals move the cursor to the line below
/// it. When the image reaches the last row of the terminal, there is no such
/// line: the terminal scrolls, and everything displayed is shifted. Some
/// terminals are able to leave the cursor next to the image instead: this is
/// the DEC private mode 8452, used when available.
///
/// When this is false, the last row of an image reaching the last row of the
/// screen is left out. A screen might very well be at the bottom of the
/// terminal.
///
/// By order of priority, this is:
/// 1. The value given to SetSixelLastRowSupport().
/// 2. The `FTXUI_SIXEL_LAST_ROW` environment variable: `1` or `0`.
/// 3. Whether the terminal replied knowing about the mode 8452, when it was
///    asked. An ftxui::App asks. See also QueryGraphics().
/// 4. false.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) bool SixelLastRowSupport();

/// @brief Force allowing, or not, sixel images on the last row of a screen.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) void SetSixelLastRowSupport(bool supported);

/// @brief Record whether the terminal knows about the DEC private mode 8452.
/// This is called by ftxui::App.
/// @ingroup screen
FTXUI_EXPORT(SCREEN) void SetDetectedSixelLastRowSupport(bool supported);

/// @brief Ask the terminal whether it supports sixel, and what the size of
/// its cells is. Then wait for its reply.
///
/// This is meant for programs printing a ftxui::Screen and then exiting.
/// Interactive programs don't need it: ftxui::App does it on its own.
///
/// The terminal is reached via /dev/tty, so it works when the standard input
/// or output are redirected. It does nothing on Windows.
///
/// @param timeout_ms How long to wait for the terminal, in milliseconds.
/// @return SixelSupport().
/// @ingroup screen
FTXUI_EXPORT(SCREEN) bool QueryGraphics(int timeout_ms = 200);

/// @brief Quirks is a structure that represents various terminal-specific
/// behaviors that may require fallbacks.
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) Quirks {
 public:
  Quirks();
  ~Quirks();
  Quirks(const Quirks&);
  Quirks& operator=(const Quirks&);
  Quirks(Quirks&&) noexcept;
  Quirks& operator=(Quirks&&) noexcept;

  /// @brief Whether the terminal font supports the 8 Unicode block characters.
  bool BlockCharacters() const;
  void SetBlockCharacters(bool v);

  /// @brief Whether the terminal correctly handles hiding the cursor.
  bool CursorHiding() const;
  void SetCursorHiding(bool v);

  /// @brief Whether the terminal should use ASCII characters for components.
  bool ComponentAscii() const;
  void SetComponentAscii(bool v);

  /// @brief The level of color support of the terminal.
  Color ColorSupport() const;
  void SetColorSupport(Color v);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
FTXUI_EXPORT(SCREEN) Quirks GetQuirks();
FTXUI_EXPORT(SCREEN) void SetQuirks(const Quirks& quirks);

/// @brief TerminalInfo is a structure that contains information about the
/// terminal.
/// @ingroup screen
class FTXUI_EXPORT(SCREEN) TerminalInfo {
 public:
  TerminalInfo();
  ~TerminalInfo();
  TerminalInfo(const TerminalInfo&) = delete;
  TerminalInfo& operator=(const TerminalInfo&) = delete;
  TerminalInfo(TerminalInfo&&) noexcept;
  TerminalInfo& operator=(TerminalInfo&&) noexcept;

  void SetTerm(std::string_view term);
  void SetColorterm(std::string_view colorterm);
  void SetTermProgram(std::string_view term_program);
  void SetTerminalName(std::string_view terminal_name);
  void SetTerminalEmulatorName(std::string_view terminal_emulator_name);
  void SetCapabilities(std::vector<int> capabilities);

  Color ComputeColorSupport() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// @brief Compute the color support based on environment variables and terminal
/// identification.
/// @param term The TERM environment variable.
/// @param colorterm The COLORTERM environment variable.
/// @param term_program The TERM_PROGRAM environment variable.
/// @param terminal_name The terminal name (from DA2).
/// @param terminal_emulator_name The terminal emulator name (from XTVERSION).
/// @param capabilities The terminal capabilities (from DA1).
FTXUI_EXPORT(SCREEN)
Color ComputeColorSupport(std::string_view term,
                          std::string_view colorterm,
                          std::string_view term_program,
                          std::string_view terminal_name,
                          std::string_view terminal_emulator_name,
                          const std::vector<int>& capabilities);

}  // namespace Terminal

}  // namespace ftxui

#endif  // FTXUI_SCREEN_TERMINAL_HPP
