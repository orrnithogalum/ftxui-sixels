// Copyright 2026 Arthur Sonzogni. All rights reserved.
// Use of this source code is governed by the MIT license that can be found in
// the LICENSE file.
#ifndef FTXUI_IMAGE_VIEW_HPP
#define FTXUI_IMAGE_VIEW_HPP

// This header makes this version of FTXUI a replacement of the libraries:
// - ftxui-image-view:       https://github.com/ljrrjl/ftxui-image-view
// - ftxui-image-view-async:
// https://github.com/orrnithogalum/ftxui-image-view-async
//
// They are both used via:
//
//   #include "image_view.hpp"
//
// and they provide, in the namespace ftxui:
//
//   Element image_view(std::string_view url);
//   void setOnImageLoadedCallback(std::function<void()> callback);
//   void setImageCacheMaxSize(size_t max_entries);
//   void setImageResizeCacheMaxSize(size_t max_entries);
//   void setImageCharCacheMaxSize(size_t max_entries);
//   void setMaxConcurrentImageLoads(int max_concurrent);
//
// All of them are declared by "ftxui/dom/image.hpp", along with the rest of
// the image API: ftxui::image and ftxui::ImageOptions.

#include "ftxui/dom/image.hpp"

#endif  // FTXUI_IMAGE_VIEW_HPP
