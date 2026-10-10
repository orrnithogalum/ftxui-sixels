// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_DOM_IMAGE_INTERNAL_HPP
#define FTXUI_DOM_IMAGE_INTERNAL_HPP

#include <functional>

#include "ftxui/util/export.hpp"

namespace ftxui::internal {

// Internal function exported for the 'component' library.
//
// Set the function called, from another thread, when an image was loaded in
// the background. This is how the active App gets to draw a new frame. Give
// an empty function to remove it: once this returns, the previous function is
// neither running, nor going to be called.
FTXUI_EXPORT(DOM) void SetImageRefreshHook(std::function<void()> hook);

}  // namespace ftxui::internal

#endif  // FTXUI_DOM_IMAGE_INTERNAL_HPP
