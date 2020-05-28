// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_OPENGL_GL_CURSOR_H_
#define REMOTING_CLIENT_OPENGL_GL_CURSOR_H_

#include <array>
#include <memory>

#include "base/macros.h"

namespace remoting {

namespace protocol {
class CursorShapeInfo;
}  // namespace protocol

class GlCanvas;
class GlRenderLayer;

// This class draws the cursor on the canvas.
class GlCursor {
 public:
  GlCursor();
  ~GlCursor();

  void SetCursorShape(const protocol::CursorShapeInfo& cursor_shape);

  void SetCanvasSize(int width, int height);

  // Sets the cursor hotspot positions. Does nothing if the cursor shape or the
  // canvas size has not been set.
  void SetCursorPosition(int x, int y);

  // Draw() will do nothing if cursor is not visible.
  void SetCursorVisible(bool visible);

  // Sets the canvas on which the cursor will be drawn. Resumes the current
  // state of the cursor to the context of the new canvas.
  // If |canvas| is nullptr, nothing will happen when calling Draw().
  void SetCanvas(GlCanvas* canvas);

  void Draw();

 private:
  void SetCurrentCursorShape(bool size_changed);

  bool visible_ = true;

  std::unique_ptr<GlRenderLayer> layer_;

  std::unique_ptr<uint8_t[]> current_cursor_data_;
  int current_cursor_data_size_ = 0;
  int current_cursor_width_ = 0;
  int current_cursor_height_ = 0;
  int current_cursor_hotspot_x_ = 0;
  int current_cursor_hotspot_y_ = 0;

  int cursor_x_ = 0;
  int cursor_y_ = 0;
  int canvas_width_ = 0;
  int canvas_height_ = 0;

  DISALLOW_COPY_AND_ASSIGN(GlCursor);
};

}  // namespace remoting
#endif  // REMOTING_CLIENT_OPENGL_GL_CURSOR_H_
