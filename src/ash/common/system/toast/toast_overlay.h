// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMMON_SYSTEM_TOAST_TOAST_OVERLAY_H_
#define ASH_COMMON_SYSTEM_TOAST_TOAST_OVERLAY_H_

#include <memory>
#include <string>

#include "ash/ash_export.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/gfx/geometry/size.h"

namespace gfx {
class Rect;
}

namespace views {
class Widget;
}

namespace ash {

class ToastManagerTest;
class ToastOverlayView;
class ToastOverlayButton;

class ASH_EXPORT ToastOverlay : public ui::ImplicitAnimationObserver {
 public:
  class ASH_EXPORT Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnClosed() = 0;
  };

  // Creates the Toast overlay UI. |text| is the message to be shown, and
  // |dismiss_text| is the message for the button to dismiss the toast message.
  // |dismiss_text| is optional. If empty, the default text is used.
  ToastOverlay(Delegate* delegate,
               const std::string& text,
               const std::string& dismiss_text);
  ~ToastOverlay() override;

  // Shows or hides the overlay.
  void Show(bool visible);

 private:
  friend class ToastManagerTest;

  // Returns the current bounds of the overlay, which is based on visibility.
  gfx::Rect CalculateOverlayBounds();

  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsScheduled() override;
  void OnImplicitAnimationsCompleted() override;

  views::Widget* widget_for_testing();
  void ClickDismissButtonForTesting(const ui::Event& event);

  Delegate* const delegate_;
  const std::string text_;
  const std::string dismiss_text_;
  std::unique_ptr<views::Widget> overlay_widget_;
  std::unique_ptr<ToastOverlayView> overlay_view_;
  gfx::Size widget_size_;

  DISALLOW_COPY_AND_ASSIGN(ToastOverlay);
};

}  // namespace ash

#endif  // ASH_COMMON_SYSTEM_TOAST_TOAST_OVERLAY_H_
