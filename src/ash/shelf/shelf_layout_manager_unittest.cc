// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/shelf/shelf_layout_manager.h"

#include "ash/aura/wm_window_aura.h"
#include "ash/common/accelerators/accelerator_controller.h"
#include "ash/common/accelerators/accelerator_table.h"
#include "ash/common/ash_switches.h"
#include "ash/common/focus_cycler.h"
#include "ash/common/material_design/material_design_controller.h"
#include "ash/common/session/session_state_delegate.h"
#include "ash/common/shelf/shelf_constants.h"
#include "ash/common/shell_window_ids.h"
#include "ash/common/system/status_area_widget.h"
#include "ash/common/system/tray/system_tray.h"
#include "ash/common/system/tray/system_tray_item.h"
#include "ash/common/wm/window_state.h"
#include "ash/common/wm_shell.h"
#include "ash/display/display_manager.h"
#include "ash/root_window_controller.h"
#include "ash/shelf/shelf.h"
#include "ash/shelf/shelf_layout_manager_observer.h"
#include "ash/shelf/shelf_view.h"
#include "ash/shelf/shelf_widget.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/test/display_manager_test_api.h"
#include "ash/test/shelf_test_api.h"
#include "ash/test/test_system_tray_item.h"
#include "ash/wm/window_state_aura.h"
#include "ash/wm/window_util.h"
#include "base/command_line.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/window_tree_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/display/display.h"
#include "ui/display/manager/display_layout.h"
#include "ui/display/screen.h"
#include "ui/events/gesture_detection/gesture_configuration.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

namespace ash {
namespace {

void StepWidgetLayerAnimatorToEnd(views::Widget* widget) {
  widget->GetNativeView()->layer()->GetAnimator()->Step(
      base::TimeTicks::Now() + base::TimeDelta::FromSeconds(1));
}

Shelf* GetShelf() {
  return Shelf::ForPrimaryDisplay();
}

ShelfWidget* GetShelfWidget() {
  return GetShelf()->shelf_widget();
}

ShelfLayoutManager* GetShelfLayoutManager() {
  return GetShelfWidget()->shelf_layout_manager();
}

// Class which waits till the shelf finishes animating to the target size and
// counts the number of animation steps.
class ShelfAnimationWaiter : views::WidgetObserver {
 public:
  explicit ShelfAnimationWaiter(const gfx::Rect& target_bounds)
      : target_bounds_(target_bounds),
        animation_steps_(0),
        done_waiting_(false) {
    GetShelfWidget()->AddObserver(this);
  }

  ~ShelfAnimationWaiter() override { GetShelfWidget()->RemoveObserver(this); }

  // Wait till the shelf finishes animating to its expected bounds.
  void WaitTillDoneAnimating() {
    if (IsDoneAnimating())
      done_waiting_ = true;
    else
      base::MessageLoop::current()->Run();
  }

  // Returns true if the animation has completed and it was valid.
  bool WasValidAnimation() const {
    return done_waiting_ && animation_steps_ > 0;
  }

 private:
  // Returns true if shelf has finished animating to the target size.
  bool IsDoneAnimating() const {
    ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
    gfx::Rect current_bounds = GetShelfWidget()->GetWindowBoundsInScreen();
    int size = layout_manager->PrimaryAxisValue(current_bounds.height(),
                                                current_bounds.width());
    int desired_size = layout_manager->PrimaryAxisValue(target_bounds_.height(),
                                                        target_bounds_.width());
    return (size == desired_size);
  }

  // views::WidgetObserver override.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override {
    if (done_waiting_)
      return;

    ++animation_steps_;
    if (IsDoneAnimating()) {
      done_waiting_ = true;
      base::MessageLoop::current()->QuitWhenIdle();
    }
  }

  gfx::Rect target_bounds_;
  int animation_steps_;
  bool done_waiting_;

  DISALLOW_COPY_AND_ASSIGN(ShelfAnimationWaiter);
};

class ShelfDragCallback {
 public:
  ShelfDragCallback(const gfx::Rect& not_visible, const gfx::Rect& visible)
      : auto_hidden_shelf_widget_bounds_(not_visible),
        shelf_widget_bounds_(visible),
        was_visible_on_drag_start_(false) {
    EXPECT_EQ(auto_hidden_shelf_widget_bounds_.bottom(),
              shelf_widget_bounds_.bottom());
  }

  virtual ~ShelfDragCallback() {}

  void ProcessScroll(ui::EventType type, const gfx::Vector2dF& delta) {
    if (GetShelfLayoutManager()->visibility_state() == SHELF_HIDDEN)
      return;

    if (type == ui::ET_GESTURE_SCROLL_BEGIN) {
      scroll_ = gfx::Vector2dF();
      was_visible_on_drag_start_ = GetShelfLayoutManager()->IsVisible();
      return;
    }

    // The state of the shelf at the end of the gesture is tested separately.
    if (type == ui::ET_GESTURE_SCROLL_END)
      return;

    if (type == ui::ET_GESTURE_SCROLL_UPDATE)
      scroll_.Add(delta);

    gfx::Rect shelf_bounds = GetShelfWidget()->GetWindowBoundsInScreen();
    if (GetShelfLayoutManager()->IsHorizontalAlignment()) {
      EXPECT_EQ(auto_hidden_shelf_widget_bounds_.bottom(),
                shelf_bounds.bottom());
      EXPECT_EQ(shelf_widget_bounds_.bottom(), shelf_bounds.bottom());
    } else if (SHELF_ALIGNMENT_RIGHT ==
               GetShelfLayoutManager()->GetAlignment()) {
      EXPECT_EQ(auto_hidden_shelf_widget_bounds_.right(), shelf_bounds.right());
      EXPECT_EQ(shelf_widget_bounds_.right(), shelf_bounds.right());
    } else if (SHELF_ALIGNMENT_LEFT ==
               GetShelfLayoutManager()->GetAlignment()) {
      EXPECT_EQ(auto_hidden_shelf_widget_bounds_.x(), shelf_bounds.x());
      EXPECT_EQ(shelf_widget_bounds_.x(), shelf_bounds.x());
    }

    // if the shelf is being dimmed test dimmer bounds as well.
    if (GetShelfWidget()->GetDimsShelf())
      EXPECT_EQ(GetShelfWidget()->GetWindowBoundsInScreen(),
                GetShelfWidget()->GetDimmerBoundsForTest());

    // Auto hidden shelf has a visible height of 0 in MD (where this inequality
    // does not apply); whereas auto hidden shelf has a visible height of 3 in
    // non-MD.
    if (!ash::MaterialDesignController::IsShelfMaterial() ||
        GetShelf()->GetAutoHideState() != ash::SHELF_AUTO_HIDE_HIDDEN) {
      EXPECT_GE(shelf_bounds.height(),
                auto_hidden_shelf_widget_bounds_.height());
    }

    float scroll_delta =
        GetShelfLayoutManager()->PrimaryAxisValue(scroll_.y(), scroll_.x());
    bool increasing_drag =
        GetShelfWidget()->shelf()->SelectValueForShelfAlignment(
            scroll_delta<0, scroll_delta> 0, scroll_delta < 0);
    int shelf_size = GetShelfLayoutManager()->PrimaryAxisValue(
        shelf_bounds.height(), shelf_bounds.width());
    int visible_bounds_size = GetShelfLayoutManager()->PrimaryAxisValue(
        shelf_widget_bounds_.height(), shelf_widget_bounds_.width());
    int not_visible_bounds_size = GetShelfLayoutManager()->PrimaryAxisValue(
        auto_hidden_shelf_widget_bounds_.height(),
        auto_hidden_shelf_widget_bounds_.width());
    if (was_visible_on_drag_start_) {
      if (increasing_drag) {
        // If dragging inwards from the visible state, then the shelf should
        // increase in size, but not more than the scroll delta.
        EXPECT_LE(visible_bounds_size, shelf_size);
        EXPECT_LE(std::abs(shelf_size - visible_bounds_size),
                  std::abs(scroll_delta));
      } else {
        if (shelf_size > not_visible_bounds_size) {
          // If dragging outwards from the visible state, then the shelf
          // should decrease in size, until it reaches the minimum size.
          EXPECT_EQ(shelf_size, visible_bounds_size - std::abs(scroll_delta));
        }
      }
    } else {
      if (std::abs(scroll_delta) <
          visible_bounds_size - not_visible_bounds_size) {
        // Tests that the shelf sticks with the touch point during the drag
        // until the shelf is completely visible.
        EXPECT_EQ(shelf_size, not_visible_bounds_size + std::abs(scroll_delta));
      } else {
        // Tests that after the shelf is completely visible, the shelf starts
        // resisting the drag.
        EXPECT_LT(shelf_size, not_visible_bounds_size + std::abs(scroll_delta));
      }
    }
  }

 private:
  const gfx::Rect auto_hidden_shelf_widget_bounds_;
  const gfx::Rect shelf_widget_bounds_;
  gfx::Vector2dF scroll_;
  bool was_visible_on_drag_start_;

  DISALLOW_COPY_AND_ASSIGN(ShelfDragCallback);
};

class ShelfLayoutObserverTest : public ShelfLayoutManagerObserver {
 public:
  ShelfLayoutObserverTest() : changed_auto_hide_state_(false) {}

  ~ShelfLayoutObserverTest() override {}

  bool changed_auto_hide_state() const { return changed_auto_hide_state_; }

 private:
  // ShelfLayoutManagerObserver:
  void OnAutoHideStateChanged(ShelfAutoHideState new_state) override {
    changed_auto_hide_state_ = true;
  }

  bool changed_auto_hide_state_;

  DISALLOW_COPY_AND_ASSIGN(ShelfLayoutObserverTest);
};

}  // namespace

class ShelfLayoutManagerTest : public test::AshTestBase {
 public:
  ShelfLayoutManagerTest() {}

  // Calls the private SetState() function.
  void SetState(ShelfLayoutManager* layout_manager,
                ShelfVisibilityState state) {
    layout_manager->SetState(state);
  }

  void UpdateAutoHideStateNow() {
    GetShelfLayoutManager()->UpdateAutoHideStateNow();
  }

  aura::Window* CreateTestWindow() {
    aura::Window* window = new aura::Window(nullptr);
    window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
    window->SetType(ui::wm::WINDOW_TYPE_NORMAL);
    window->Init(ui::LAYER_TEXTURED);
    ParentWindowInPrimaryRootWindow(window);
    return window;
  }

  aura::Window* CreateTestWindowInParent(aura::Window* root_window) {
    aura::Window* window = new aura::Window(nullptr);
    window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
    window->SetType(ui::wm::WINDOW_TYPE_NORMAL);
    window->Init(ui::LAYER_TEXTURED);
    aura::client::ParentWindowWithContext(window, root_window, gfx::Rect());
    return window;
  }

  views::Widget* CreateTestWidgetWithParams(
      const views::Widget::InitParams& params) {
    views::Widget* out = new views::Widget;
    out->Init(params);
    out->Show();
    return out;
  }

  // Create a simple widget in the current context (will delete on TearDown).
  views::Widget* CreateTestWidget() {
    views::Widget::InitParams params(views::Widget::InitParams::TYPE_WINDOW);
    params.bounds = gfx::Rect(0, 0, 200, 200);
    params.context = CurrentContext();
    return CreateTestWidgetWithParams(params);
  }

  void RunGestureDragTests(gfx::Vector2d);

  // Turn on the lock screen.
  void LockScreen() {
    WmShell::Get()->GetSessionStateDelegate()->LockScreen();
    // The test session state delegate does not fire the lock state change.
    Shell::GetInstance()->OnLockStateChanged(true);
  }

  // Turn off the lock screen.
  void UnlockScreen() {
    WmShell::Get()->GetSessionStateDelegate()->UnlockScreen();
    // The test session state delegate does not fire the lock state change.
    Shell::GetInstance()->OnLockStateChanged(false);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ShelfLayoutManagerTest);
};

void ShelfLayoutManagerTest::RunGestureDragTests(gfx::Vector2d delta) {
  Shelf* shelf = GetShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);

  views::Widget* widget = CreateTestWidget();
  widget->Maximize();

  // The time delta should be large enough to prevent accidental fling creation.
  const base::TimeDelta kTimeDelta = base::TimeDelta::FromMilliseconds(100);

  aura::Window* window = widget->GetNativeWindow();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  layout_manager->LayoutShelf();

  gfx::Rect shelf_shown = GetShelfWidget()->GetWindowBoundsInScreen();
  gfx::Rect bounds_shelf = window->bounds();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  layout_manager->LayoutShelf();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  gfx::Rect bounds_noshelf = window->bounds();
  gfx::Rect shelf_hidden = GetShelfWidget()->GetWindowBoundsInScreen();

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  layout_manager->LayoutShelf();

  ui::test::EventGenerator& generator(GetEventGenerator());
  const int kNumScrollSteps = 4;
  ShelfDragCallback handler(shelf_hidden, shelf_shown);

  // Swipe up on the shelf. This should not change any state.
  gfx::Point start = GetShelfWidget()->GetWindowBoundsInScreen().CenterPoint();
  gfx::Point end = start + delta;

  // Swipe down on the shelf to hide it.
  generator.GestureScrollSequenceWithCallback(
      start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_NE(bounds_shelf.ToString(), window->bounds().ToString());
  EXPECT_NE(shelf_shown.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe up to show the shelf.
  generator.GestureScrollSequenceWithCallback(
      end, start, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_NEVER, shelf->auto_hide_behavior());
  EXPECT_EQ(bounds_shelf.ToString(), window->bounds().ToString());
  EXPECT_EQ(GetShelfWidget()->GetDimmerBoundsForTest(),
            GetShelfWidget()->GetWindowBoundsInScreen());
  EXPECT_EQ(shelf_shown.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe up again. The shelf should hide.
  end = start - delta;
  generator.GestureScrollSequenceWithCallback(
      start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_EQ(shelf_hidden.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe up yet again to show it.
  end = start + delta;
  generator.GestureScrollSequenceWithCallback(
      end, start, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));

  // Swipe down very little. It shouldn't change any state.
  if (GetShelfLayoutManager()->IsHorizontalAlignment())
    end.set_y(start.y() + shelf_shown.height() * 3 / 10);
  else if (SHELF_ALIGNMENT_LEFT == GetShelfLayoutManager()->GetAlignment())
    end.set_x(start.x() - shelf_shown.width() * 3 / 10);
  else if (SHELF_ALIGNMENT_RIGHT == GetShelfLayoutManager()->GetAlignment())
    end.set_x(start.x() + shelf_shown.width() * 3 / 10);
  generator.GestureScrollSequence(start, end, kTimeDelta, 5);
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_NEVER, shelf->auto_hide_behavior());
  EXPECT_EQ(bounds_shelf.ToString(), window->bounds().ToString());
  EXPECT_EQ(shelf_shown.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe down again to hide.
  end = start + delta;
  generator.GestureScrollSequenceWithCallback(
      start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_EQ(GetShelfWidget()->GetDimmerBoundsForTest(), gfx::Rect());
  EXPECT_EQ(bounds_noshelf.ToString(), window->bounds().ToString());
  EXPECT_EQ(shelf_hidden.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe up in extended hit region to show it.
  gfx::Point extended_start = start;
  if (GetShelfLayoutManager()->IsHorizontalAlignment())
    extended_start.set_y(GetShelfWidget()->GetWindowBoundsInScreen().y() - 1);
  else if (SHELF_ALIGNMENT_LEFT == GetShelfLayoutManager()->GetAlignment())
    extended_start.set_x(GetShelfWidget()->GetWindowBoundsInScreen().right() +
                         1);
  else if (SHELF_ALIGNMENT_RIGHT == GetShelfLayoutManager()->GetAlignment())
    extended_start.set_x(GetShelfWidget()->GetWindowBoundsInScreen().x() - 1);
  end = extended_start - delta;
  generator.GestureScrollSequenceWithCallback(
      extended_start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_NEVER, shelf->auto_hide_behavior());
  EXPECT_EQ(bounds_shelf.ToString(), window->bounds().ToString());
  EXPECT_EQ(GetShelfWidget()->GetDimmerBoundsForTest(),
            GetShelfWidget()->GetWindowBoundsInScreen());
  EXPECT_EQ(shelf_shown.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe down again to hide.
  end = start + delta;
  generator.GestureScrollSequenceWithCallback(
      start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_EQ(GetShelfWidget()->GetDimmerBoundsForTest(), gfx::Rect());
  EXPECT_EQ(bounds_noshelf.ToString(), window->bounds().ToString());
  EXPECT_EQ(shelf_hidden.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe up outside the hit area. This should not change anything.
  gfx::Point outside_start =
      gfx::Point((GetShelfWidget()->GetWindowBoundsInScreen().x() +
                  GetShelfWidget()->GetWindowBoundsInScreen().right()) /
                     2,
                 GetShelfWidget()->GetWindowBoundsInScreen().y() - 50);
  end = outside_start + delta;
  generator.GestureScrollSequence(outside_start, end, kTimeDelta,
                                  kNumScrollSteps);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_EQ(shelf_hidden.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe up from below the shelf where a bezel would be, this should show the
  // shelf.
  gfx::Point below_start = start;
  if (GetShelfLayoutManager()->IsHorizontalAlignment())
    below_start.set_y(GetShelfWidget()->GetWindowBoundsInScreen().bottom() + 1);
  else if (SHELF_ALIGNMENT_LEFT == GetShelfLayoutManager()->GetAlignment())
    below_start.set_x(GetShelfWidget()->GetWindowBoundsInScreen().x() - 1);
  else if (SHELF_ALIGNMENT_RIGHT == GetShelfLayoutManager()->GetAlignment())
    below_start.set_x(GetShelfWidget()->GetWindowBoundsInScreen().right() + 1);
  end = below_start - delta;
  generator.GestureScrollSequence(below_start, end, kTimeDelta,
                                  kNumScrollSteps);
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_NEVER, shelf->auto_hide_behavior());
  EXPECT_EQ(bounds_shelf.ToString(), window->bounds().ToString());
  EXPECT_EQ(GetShelfWidget()->GetDimmerBoundsForTest(),
            GetShelfWidget()->GetWindowBoundsInScreen());
  EXPECT_EQ(shelf_shown.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Swipe down again to hide.
  end = start + delta;
  generator.GestureScrollSequenceWithCallback(
      start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_EQ(GetShelfWidget()->GetDimmerBoundsForTest(), gfx::Rect());
  EXPECT_EQ(bounds_noshelf.ToString(), window->bounds().ToString());
  EXPECT_EQ(shelf_hidden.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());

  // Put |widget| into fullscreen. Set the shelf to be auto hidden when |widget|
  // is fullscreen. (eg browser immersive fullscreen).
  widget->SetFullscreen(true);
  wm::GetWindowState(window)->set_hide_shelf_when_fullscreen(false);
  layout_manager->UpdateVisibilityState();

  gfx::Rect bounds_fullscreen = window->bounds();
  EXPECT_TRUE(widget->IsFullscreen());

  // Shelf hints are removed in immersive full screen mode in MD; and some shelf
  // hints are shown in non-MD mode.
  if (ash::MaterialDesignController::IsShelfMaterial())
    EXPECT_EQ(bounds_noshelf.ToString(), bounds_fullscreen.ToString());
  else
    EXPECT_NE(bounds_noshelf.ToString(), bounds_fullscreen.ToString());

  // Swipe up. This should show the shelf.
  end = below_start - delta;
  generator.GestureScrollSequenceWithCallback(
      below_start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_NEVER, shelf->auto_hide_behavior());
  EXPECT_EQ(shelf_shown.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());
  EXPECT_EQ(bounds_fullscreen.ToString(), window->bounds().ToString());

  // Swipe up again. This should hide the shelf.
  generator.GestureScrollSequenceWithCallback(
      below_start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_EQ(shelf_hidden.ToString(),
            GetShelfWidget()->GetWindowBoundsInScreen().ToString());
  EXPECT_EQ(bounds_fullscreen.ToString(), window->bounds().ToString());

  // Set the shelf to be hidden when |widget| is fullscreen. (eg tab fullscreen
  // with or without immersive browser fullscreen).
  wm::GetWindowState(window)->set_hide_shelf_when_fullscreen(true);
  layout_manager->UpdateVisibilityState();
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());

  // Swipe-up. This should not change anything.
  end = start - delta;
  generator.GestureScrollSequenceWithCallback(
      below_start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
  EXPECT_EQ(bounds_fullscreen.ToString(), window->bounds().ToString());

  // Close actually, otherwise further event may be affected since widget
  // is fullscreen status.
  widget->Close();
  RunAllPendingInMessageLoop();

  // The shelf should be shown because there are no more visible windows.
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());

  // Swipe-up to hide. This should have no effect because there are no visible
  // windows.
  end = below_start - delta;
  generator.GestureScrollSequenceWithCallback(
      below_start, end, kTimeDelta, kNumScrollSteps,
      base::Bind(&ShelfDragCallback::ProcessScroll,
                 base::Unretained(&handler)));
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS, shelf->auto_hide_behavior());
}

// Need to be implemented.  http://crbug.com/111279.
#if defined(OS_WIN)
#define MAYBE_SetVisible DISABLED_SetVisible
#else
#define MAYBE_SetVisible SetVisible
#endif
// Makes sure SetVisible updates work area and widget appropriately.
TEST_F(ShelfLayoutManagerTest, MAYBE_SetVisible) {
  ShelfWidget* shelf_widget = GetShelfWidget();
  ShelfLayoutManager* manager = shelf_widget->shelf_layout_manager();
  // Force an initial layout.
  manager->LayoutShelf();
  EXPECT_EQ(SHELF_VISIBLE, manager->visibility_state());

  gfx::Rect status_bounds(
      shelf_widget->status_area_widget()->GetWindowBoundsInScreen());
  gfx::Rect shelf_bounds(shelf_widget->GetWindowBoundsInScreen());
  int shelf_height = manager->GetIdealBounds().height();
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  ASSERT_NE(-1, display.id());
  // Bottom inset should be the max of widget heights.
  EXPECT_EQ(shelf_height, display.GetWorkAreaInsets().bottom());

  // Hide the shelf.
  SetState(manager, SHELF_HIDDEN);
  // Run the animation to completion.
  StepWidgetLayerAnimatorToEnd(shelf_widget);
  StepWidgetLayerAnimatorToEnd(shelf_widget->status_area_widget());
  EXPECT_EQ(SHELF_HIDDEN, manager->visibility_state());
  display = display::Screen::GetScreen()->GetPrimaryDisplay();
  EXPECT_EQ(0, display.GetWorkAreaInsets().bottom());

  // Make sure the bounds of the two widgets changed.
  EXPECT_GE(shelf_widget->GetNativeView()->bounds().y(),
            display.bounds().bottom());
  EXPECT_GE(shelf_widget->status_area_widget()->GetNativeView()->bounds().y(),
            display.bounds().bottom());

  // And show it again.
  SetState(manager, SHELF_VISIBLE);
  // Run the animation to completion.
  StepWidgetLayerAnimatorToEnd(shelf_widget);
  StepWidgetLayerAnimatorToEnd(shelf_widget->status_area_widget());
  EXPECT_EQ(SHELF_VISIBLE, manager->visibility_state());
  display = display::Screen::GetScreen()->GetPrimaryDisplay();
  EXPECT_EQ(shelf_height, display.GetWorkAreaInsets().bottom());

  // Make sure the bounds of the two widgets changed.
  shelf_bounds = shelf_widget->GetNativeView()->bounds();
  EXPECT_LT(shelf_bounds.y(), display.bounds().bottom());
  status_bounds = shelf_widget->status_area_widget()->GetNativeView()->bounds();
  EXPECT_LT(status_bounds.y(), display.bounds().bottom());
}

// Makes sure LayoutShelf invoked while animating cleans things up.
TEST_F(ShelfLayoutManagerTest, LayoutShelfWhileAnimating) {
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  // Force an initial layout.
  layout_manager->LayoutShelf();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  // Hide the shelf.
  SetState(layout_manager, SHELF_HIDDEN);
  layout_manager->LayoutShelf();
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  EXPECT_EQ(0, display.GetWorkAreaInsets().bottom());

  // Make sure the bounds of the two widgets changed.
  ShelfWidget* shelf_widget = GetShelfWidget();
  EXPECT_GE(shelf_widget->GetNativeView()->bounds().y(),
            display.bounds().bottom());
  EXPECT_GE(shelf_widget->status_area_widget()->GetNativeView()->bounds().y(),
            display.bounds().bottom());
}

// Test that switching to a different visibility state does not restart the
// shelf show / hide animation if it is already running. (crbug.com/250918)
TEST_F(ShelfLayoutManagerTest, SetStateWhileAnimating) {
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  SetState(layout_manager, SHELF_VISIBLE);
  ShelfWidget* shelf_widget = GetShelfWidget();
  gfx::Rect initial_shelf_bounds = shelf_widget->GetWindowBoundsInScreen();
  gfx::Rect initial_status_bounds =
      shelf_widget->status_area_widget()->GetWindowBoundsInScreen();

  ui::ScopedAnimationDurationScaleMode normal_animation_duration(
      ui::ScopedAnimationDurationScaleMode::SLOW_DURATION);
  SetState(layout_manager, SHELF_HIDDEN);
  SetState(layout_manager, SHELF_VISIBLE);

  gfx::Rect current_shelf_bounds = shelf_widget->GetWindowBoundsInScreen();
  gfx::Rect current_status_bounds =
      shelf_widget->status_area_widget()->GetWindowBoundsInScreen();

  const int small_change = initial_shelf_bounds.height() / 2;
  EXPECT_LE(
      std::abs(initial_shelf_bounds.height() - current_shelf_bounds.height()),
      small_change);
  EXPECT_LE(
      std::abs(initial_status_bounds.height() - current_status_bounds.height()),
      small_change);
}

// Makes sure the shelf is sized when the status area changes size.
TEST_F(ShelfLayoutManagerTest, ShelfUpdatedWhenStatusAreaChangesSize) {
  Shelf* shelf = GetShelf();
  ASSERT_TRUE(shelf);
  ShelfWidget* shelf_widget = GetShelfWidget();
  ASSERT_TRUE(shelf_widget);
  ASSERT_TRUE(shelf_widget->status_area_widget());
  shelf_widget->status_area_widget()->SetBounds(gfx::Rect(0, 0, 200, 200));
  EXPECT_EQ(200, shelf_widget->GetContentsView()->width() -
                     test::ShelfTestAPI(shelf).shelf_view()->width());
}

// Various assertions around auto-hide.
TEST_F(ShelfLayoutManagerTest, AutoHide) {
  ui::test::EventGenerator& generator(GetEventGenerator());

  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  views::Widget* widget = CreateTestWidget();
  widget->Maximize();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // LayoutShelf() forces the animation to completion, at which point the
  // shelf should go off the screen.
  layout_manager->LayoutShelf();
  const int shelf_insets = GetShelfConstant(SHELF_INSETS_FOR_AUTO_HIDE);

  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  const int display_bottom = display.bounds().bottom();
  EXPECT_EQ(display_bottom - kShelfAutoHideSize,
            GetShelfWidget()->GetWindowBoundsInScreen().y());
  EXPECT_EQ(display_bottom - shelf_insets, display.work_area().bottom());

  // Move the mouse to the bottom of the screen.
  generator.MoveMouseTo(0, display_bottom - 1);

  // Shelf should be shown again (but it shouldn't have changed the work area).
  SetState(layout_manager, SHELF_AUTO_HIDE);
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  layout_manager->LayoutShelf();
  EXPECT_EQ(display_bottom - layout_manager->GetIdealBounds().height(),
            GetShelfWidget()->GetWindowBoundsInScreen().y());
  EXPECT_EQ(display_bottom - shelf_insets, display.work_area().bottom());

  // Move mouse back up.
  generator.MoveMouseTo(0, 0);
  SetState(layout_manager, SHELF_AUTO_HIDE);
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  layout_manager->LayoutShelf();
  EXPECT_EQ(display_bottom - kShelfAutoHideSize,
            GetShelfWidget()->GetWindowBoundsInScreen().y());

  // Drag mouse to bottom of screen.
  generator.PressLeftButton();
  generator.MoveMouseTo(0, display_bottom - 1);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  generator.ReleaseLeftButton();
  generator.MoveMouseTo(1, display_bottom - 1);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  generator.PressLeftButton();
  generator.MoveMouseTo(1, display_bottom - 1);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
}

// Test the behavior of the shelf when it is auto hidden and it is on the
// boundary between the primary and the secondary display.
TEST_F(ShelfLayoutManagerTest, AutoHideShelfOnScreenBoundary) {
  if (!SupportsMultipleDisplays())
    return;

  UpdateDisplay("800x600,800x600");
  Shell::GetInstance()->display_manager()->SetLayoutForCurrentDisplays(
      test::CreateDisplayLayout(display::DisplayPlacement::RIGHT, 0));
  // Put the primary monitor's shelf on the display boundary.
  Shelf* shelf = GetShelf();
  shelf->SetAlignment(SHELF_ALIGNMENT_RIGHT);

  // Create a window because the shelf is always shown when no windows are
  // visible.
  CreateTestWidget();

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());

  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  const int right_edge = display.bounds().right() - 1;
  const int y = display.bounds().y();

  // Start off the mouse nowhere near the shelf; the shelf should be hidden.
  ui::test::EventGenerator& generator(GetEventGenerator());
  generator.MoveMouseTo(right_edge - 50, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Moving the mouse over the light bar (but not to the edge of the screen)
  // should show the shelf.
  generator.MoveMouseTo(right_edge - 1, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_EQ(right_edge - 1,
            display::Screen::GetScreen()->GetCursorScreenPoint().x());

  // Moving the mouse off the light bar should hide the shelf.
  generator.MoveMouseTo(right_edge - 50, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Moving the mouse to the right edge of the screen crossing the light bar
  // should show the shelf despite the mouse cursor getting warped to the
  // secondary display.
  generator.MoveMouseTo(right_edge - 1, y);
  generator.MoveMouseTo(right_edge, y);
  UpdateAutoHideStateNow();
  EXPECT_NE(right_edge - 1,
            display::Screen::GetScreen()->GetCursorScreenPoint().x());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  // Hide the shelf.
  generator.MoveMouseTo(right_edge - 50, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Moving the mouse to the right edge of the screen crossing the light bar and
  // overshooting by a lot should keep the shelf hidden.
  generator.MoveMouseTo(right_edge - 1, y);
  generator.MoveMouseTo(right_edge + 50, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Moving the mouse to the right edge of the screen crossing the light bar and
  // overshooting a bit should show the shelf.
  generator.MoveMouseTo(right_edge - 1, y);
  generator.MoveMouseTo(right_edge + 2, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  // Keeping the mouse close to the left edge of the secondary display after the
  // shelf is shown should keep the shelf shown.
  generator.MoveMouseTo(right_edge + 2, y + 1);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  // Moving the mouse far from the left edge of the secondary display should
  // hide the shelf.
  generator.MoveMouseTo(right_edge + 50, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Moving to the left edge of the secondary display without first crossing
  // the primary display's right aligned shelf first should not show the shelf.
  generator.MoveMouseTo(right_edge + 2, y);
  UpdateAutoHideStateNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
}

// Assertions around the lock screen showing.
TEST_F(ShelfLayoutManagerTest, VisibleWhenLockScreenShowing) {
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  views::Widget* widget = CreateTestWidget();
  widget->Maximize();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // LayoutShelf() forces the animation to completion, at which point the
  // shelf should go off the screen.
  layout_manager->LayoutShelf();
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  EXPECT_EQ(display.bounds().bottom() - kShelfAutoHideSize,
            GetShelfWidget()->GetWindowBoundsInScreen().y());

  std::unique_ptr<views::Widget> lock_widget(AshTestBase::CreateTestWidget(
      nullptr, kShellWindowId_LockScreenContainer, gfx::Rect(200, 200)));
  lock_widget->Maximize();

  // Lock the screen.
  LockScreen();
  // Showing a widget in the lock screen should force the shelf to be visibile.
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  UnlockScreen();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
}

// Assertions around SetAutoHideBehavior.
TEST_F(ShelfLayoutManagerTest, SetAutoHideBehavior) {
  Shelf* shelf = GetShelf();
  views::Widget* widget = CreateTestWidget();

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  widget->Maximize();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  display::Screen* screen = display::Screen::GetScreen();
  EXPECT_EQ(screen->GetPrimaryDisplay().work_area().bottom(),
            widget->GetWorkAreaBoundsInScreen().bottom());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(screen->GetPrimaryDisplay().work_area().bottom(),
            widget->GetWorkAreaBoundsInScreen().bottom());

  ui::ScopedAnimationDurationScaleMode animation_duration(
      ui::ScopedAnimationDurationScaleMode::SLOW_DURATION);

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  ShelfWidget* shelf_widget = GetShelfWidget();
  EXPECT_TRUE(shelf_widget->status_area_widget()->IsVisible());
  StepWidgetLayerAnimatorToEnd(shelf_widget);
  StepWidgetLayerAnimatorToEnd(shelf_widget->status_area_widget());
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  EXPECT_EQ(screen->GetPrimaryDisplay().work_area().bottom(),
            widget->GetWorkAreaBoundsInScreen().bottom());
}

// Basic assertions around the dimming of the shelf.
TEST_F(ShelfLayoutManagerTest, DimmingBehavior) {
  ui::test::EventGenerator& generator(GetEventGenerator());

  ShelfWidget* shelf_widget = GetShelfWidget();
  shelf_widget->DisableDimmingAnimationsForTest();
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  gfx::Point off_shelf = display.bounds().CenterPoint();
  gfx::Point on_shelf = shelf_widget->GetWindowBoundsInScreen().CenterPoint();
  views::Widget* widget = CreateTestWidget();

  // Test there is no dimming object active at this point.
  generator.MoveMouseTo(on_shelf.x(), on_shelf.y());
  EXPECT_EQ(-1, shelf_widget->GetDimmingAlphaForTest());
  generator.MoveMouseTo(off_shelf.x(), off_shelf.y());
  EXPECT_EQ(-1, shelf_widget->GetDimmingAlphaForTest());

  // After maximization, the shelf should be visible and the dimmer created.
  widget->Maximize();

  on_shelf = shelf_widget->GetWindowBoundsInScreen().CenterPoint();
  EXPECT_LT(0, shelf_widget->GetDimmingAlphaForTest());

  // Moving the mouse off the shelf should dim the bar.
  generator.MoveMouseTo(off_shelf.x(), off_shelf.y());
  EXPECT_LT(0, shelf_widget->GetDimmingAlphaForTest());

  // Adding touch events outside the shelf should still keep the shelf in
  // dimmed state.
  generator.PressTouch();
  generator.MoveTouch(off_shelf);
  EXPECT_LT(0, shelf_widget->GetDimmingAlphaForTest());
  // Move the touch into the shelf area should undim.
  generator.MoveTouch(on_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.ReleaseTouch();
  // And a release dims again.
  EXPECT_LT(0, shelf_widget->GetDimmingAlphaForTest());

  // Moving the mouse on the shelf should undim the bar.
  generator.MoveMouseTo(on_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());

  // No matter what the touch events do, the shelf should stay undimmed.
  generator.PressTouch();
  generator.MoveTouch(off_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.MoveTouch(on_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.MoveTouch(off_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.MoveTouch(on_shelf);
  generator.ReleaseTouch();

  // After restore, the dimming object should be deleted again.
  widget->Restore();
  EXPECT_EQ(-1, shelf_widget->GetDimmingAlphaForTest());
}

// Test that dimming works correctly with multiple displays.
TEST_F(ShelfLayoutManagerTest, DimmingBehaviorDualDisplay) {
  if (!SupportsMultipleDisplays())
    return;

  // Create two displays.
  UpdateDisplay("0+0-200x200,+200+0-100x100");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  EXPECT_EQ(root_windows.size(), 2U);

  std::vector<ShelfWidget*> shelf_widgets;
  for (auto* root_window : root_windows) {
    ShelfLayoutManager* shelf =
        GetRootWindowController(root_window)->GetShelfLayoutManager();
    shelf_widgets.push_back(shelf->shelf_widget());

    // For disabling the dimming animation to work, the animation must be
    // disabled prior to creating the dimmer.
    shelf_widgets.back()->DisableDimmingAnimationsForTest();

    // Create a maximized window to create the dimmer.
    views::Widget::InitParams params;
    params.context = root_window;
    params.bounds = root_window->GetBoundsInScreen();
    params.show_state = ui::SHOW_STATE_MAXIMIZED;
    CreateTestWidgetWithParams(params);
  }

  ui::test::EventGenerator& generator(GetEventGenerator());

  generator.MoveMouseTo(root_windows[0]->GetBoundsInScreen().CenterPoint());
  EXPECT_LT(0, shelf_widgets[0]->GetDimmingAlphaForTest());
  EXPECT_LT(0, shelf_widgets[1]->GetDimmingAlphaForTest());

  generator.MoveMouseTo(
      shelf_widgets[0]->GetWindowBoundsInScreen().CenterPoint());
  EXPECT_EQ(0, shelf_widgets[0]->GetDimmingAlphaForTest());
  EXPECT_LT(0, shelf_widgets[1]->GetDimmingAlphaForTest());

  generator.MoveMouseTo(
      shelf_widgets[1]->GetWindowBoundsInScreen().CenterPoint());
  EXPECT_LT(0, shelf_widgets[0]->GetDimmingAlphaForTest());
  EXPECT_EQ(0, shelf_widgets[1]->GetDimmingAlphaForTest());

  generator.MoveMouseTo(root_windows[1]->GetBoundsInScreen().CenterPoint());
  EXPECT_LT(0, shelf_widgets[0]->GetDimmingAlphaForTest());
  EXPECT_LT(0, shelf_widgets[1]->GetDimmingAlphaForTest());
}

// Assertions around the dimming of the shelf in conjunction with menus.
TEST_F(ShelfLayoutManagerTest, DimmingBehaviorWithMenus) {
  ui::test::EventGenerator& generator(GetEventGenerator());
  generator.MoveMouseTo(0, 0);

  ShelfWidget* shelf_widget = GetShelfWidget();
  shelf_widget->DisableDimmingAnimationsForTest();

  // After maximization, the shelf should be visible and the dimmer created.
  views::Widget* widget = CreateTestWidget();
  widget->Maximize();

  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  gfx::Point off_shelf = display.bounds().CenterPoint();
  gfx::Point on_shelf = shelf_widget->GetWindowBoundsInScreen().CenterPoint();

  // Moving the mouse on the shelf should undim the bar.
  generator.MoveMouseTo(on_shelf.x(), on_shelf.y());
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());

  // Simulate a menu opening.
  shelf_widget->ForceUndimming(true);

  // Moving the mouse off the shelf should not dim the bar.
  generator.MoveMouseTo(off_shelf.x(), off_shelf.y());
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());

  // No matter what the touch events do, the shelf should stay undimmed.
  generator.PressTouch();
  generator.MoveTouch(off_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.MoveTouch(on_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.MoveTouch(off_shelf);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.ReleaseTouch();
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());

  // "Closing the menu" should now turn off the menu since no event is inside
  // the shelf any longer.
  shelf_widget->ForceUndimming(false);
  EXPECT_LT(0, shelf_widget->GetDimmingAlphaForTest());

  // Moving the mouse again on the shelf which should undim the bar again.
  // This time we check that the bar stays undimmed when the mouse remains on
  // the bar and the "menu gets closed".
  generator.MoveMouseTo(on_shelf.x(), on_shelf.y());
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  shelf_widget->ForceUndimming(true);
  generator.MoveMouseTo(off_shelf.x(), off_shelf.y());
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  generator.MoveMouseTo(on_shelf.x(), on_shelf.y());
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
  shelf_widget->ForceUndimming(true);
  EXPECT_EQ(0, shelf_widget->GetDimmingAlphaForTest());
}

// Verifies the shelf is visible when status/shelf is focused.
TEST_F(ShelfLayoutManagerTest, VisibleWhenStatusOrShelfFocused) {
  Shelf* shelf = GetShelf();
  views::Widget* widget = CreateTestWidget();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Focus the shelf. Have to go through the focus cycler as normal focus
  // requests to it do nothing.
  GetShelfWidget()->GetFocusCycler()->RotateFocus(FocusCycler::FORWARD);
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  widget->Activate();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Trying to activate the status should fail, since we only allow activating
  // it when the user is using the keyboard (i.e. through FocusCycler).
  GetShelfWidget()->status_area_widget()->Activate();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  GetShelfWidget()->GetFocusCycler()->RotateFocus(FocusCycler::FORWARD);
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
}

// Makes sure shelf will be visible when app list opens as shelf is in
// SHELF_VISIBLE state,and toggling app list won't change shelf
// visibility state.
TEST_F(ShelfLayoutManagerTest, OpenAppListWithShelfVisibleState) {
  WmShell* shell = WmShell::Get();
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  layout_manager->LayoutShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);

  // Create a normal unmaximized windowm shelf should be visible.
  aura::Window* window = CreateTestWindow();
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  window->Show();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  // Show app list and the shelf stays visible.
  shell->ShowAppList();
  EXPECT_TRUE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  // Hide app list and the shelf stays visible.
  shell->DismissAppList();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
}

// Makes sure shelf will be shown with SHELF_AUTO_HIDE_SHOWN state
// when app list opens as shelf is in SHELF_AUTO_HIDE state, and
// toggling app list won't change shelf visibility state.
TEST_F(ShelfLayoutManagerTest, OpenAppListWithShelfAutoHideState) {
  WmShell* shell = WmShell::Get();
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  layout_manager->LayoutShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);

  // Create a window and show it in maximized state.
  aura::Window* window = CreateTestWindow();
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window->Show();
  wm::ActivateWindow(window);

  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());

  // Show app list.
  shell->ShowAppList();
  // The shelf's auto hide state won't be changed until the timer fires, so
  // force it to update now.
  layout_manager->UpdateVisibilityState();
  EXPECT_TRUE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  // Hide app list.
  shell->DismissAppList();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
}

// Makes sure that when we have dual displays, with one or both shelves are set
// to AutoHide, viewing the AppList on one of them doesn't unhide the other
// hidden shelf.
TEST_F(ShelfLayoutManagerTest, DualDisplayOpenAppListWithShelfAutoHideState) {
  if (!SupportsMultipleDisplays())
    return;

  // Create two displays.
  UpdateDisplay("0+0-200x200,+200+0-100x100");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  EXPECT_EQ(root_windows.size(), 2U);

  // Get the shelves in both displays and set them to be 'AutoHide'.
  Shelf* shelf_1 = GetRootWindowController(root_windows[0])->GetShelf();
  Shelf* shelf_2 = GetRootWindowController(root_windows[1])->GetShelf();
  EXPECT_NE(shelf_1, shelf_2);
  EXPECT_NE(shelf_1->shelf_widget()->GetNativeWindow()->GetRootWindow(),
            shelf_2->shelf_widget()->GetNativeWindow()->GetRootWindow());
  shelf_1->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  shelf_1->shelf_layout_manager()->LayoutShelf();
  shelf_2->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  shelf_2->shelf_layout_manager()->LayoutShelf();

  // Create a window in each display and show them in maximized state.
  aura::Window* window_1 = CreateTestWindowInParent(root_windows[0]);
  window_1->SetBounds(gfx::Rect(0, 0, 100, 100));
  window_1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window_1->Show();
  aura::Window* window_2 = CreateTestWindowInParent(root_windows[1]);
  window_2->SetBounds(gfx::Rect(201, 0, 100, 100));
  window_2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window_2->Show();

  EXPECT_EQ(shelf_1->shelf_widget()->GetNativeWindow()->GetRootWindow(),
            window_1->GetRootWindow());
  EXPECT_EQ(shelf_2->shelf_widget()->GetNativeWindow()->GetRootWindow(),
            window_2->GetRootWindow());

  // Activate one window in one display and manually trigger the update of shelf
  // visibility.
  wm::ActivateWindow(window_1);
  Shell::GetInstance()->UpdateShelfVisibility();

  EXPECT_FALSE(WmShell::Get()->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_2->GetAutoHideState());

  // Show app list.
  WmShell::Get()->ShowAppList();
  Shell::GetInstance()->UpdateShelfVisibility();

  // Only the shelf in the active display should be shown, the other is hidden.
  EXPECT_TRUE(WmShell::Get()->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_2->GetAutoHideState());

  // Hide app list, both shelves should be hidden.
  WmShell::Get()->DismissAppList();
  EXPECT_FALSE(WmShell::Get()->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_2->GetAutoHideState());
}

// Makes sure the shelf will be hidden when we have a fullscreen window, and it
// will unhide when we open the app list.
TEST_F(ShelfLayoutManagerTest, OpenAppListWithShelfHiddenState) {
  WmShell* shell = WmShell::Get();
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  // For shelf to be visible, app list is not open in initial state.
  layout_manager->LayoutShelf();

  // Create a window and make it full screen.
  aura::Window* window = CreateTestWindow();
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  window->Show();
  wm::ActivateWindow(window);

  // App list and shelf is not shown.
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());

  // Show app list.
  shell->ShowAppList();
  EXPECT_TRUE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  // Hide app list.
  shell->DismissAppList();
  EXPECT_FALSE(shell->GetAppListTargetVisibility());
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());
}

// Tests the correct behavior of the shelf when there is a system modal window
// open when we have a single display.
TEST_F(ShelfLayoutManagerTest, ShelfWithSystemModalWindowSingleDisplay) {
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  layout_manager->LayoutShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);

  aura::Window* window = CreateTestWindow();
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  window->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window->Show();
  wm::ActivateWindow(window);

  // Enable system modal dialog, and make sure shelf is still hidden.
  WmShell* wm_shell = WmShell::Get();
  wm_shell->SimulateModalWindowOpenForTesting(true);
  EXPECT_TRUE(wm_shell->IsSystemModalWindowOpen());
  EXPECT_FALSE(wm::CanActivateWindow(window));
  Shell::GetInstance()->UpdateShelfVisibility();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
}

// Tests the correct behavior of the shelf when there is a system modal window
// open when we have dual display.
TEST_F(ShelfLayoutManagerTest, ShelfWithSystemModalWindowDualDisplay) {
  if (!SupportsMultipleDisplays())
    return;

  // Create two displays.
  UpdateDisplay("200x200,100x100");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  EXPECT_EQ(2U, root_windows.size());

  // Get the shelves in both displays and set them to be 'AutoHide'.
  Shelf* shelf_1 = GetRootWindowController(root_windows[0])->GetShelf();
  Shelf* shelf_2 = GetRootWindowController(root_windows[1])->GetShelf();
  EXPECT_NE(shelf_1, shelf_2);
  EXPECT_NE(shelf_1->shelf_widget()->GetNativeWindow()->GetRootWindow(),
            shelf_2->shelf_widget()->GetNativeWindow()->GetRootWindow());
  shelf_1->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  shelf_1->shelf_layout_manager()->LayoutShelf();
  shelf_2->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  shelf_2->shelf_layout_manager()->LayoutShelf();

  // Create a window in each display and show them in maximized state.
  aura::Window* window_1 = CreateTestWindowInParent(root_windows[0]);
  window_1->SetBounds(gfx::Rect(0, 0, 100, 100));
  window_1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window_1->Show();
  aura::Window* window_2 = CreateTestWindowInParent(root_windows[1]);
  window_2->SetBounds(gfx::Rect(201, 0, 100, 100));
  window_2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window_2->Show();

  EXPECT_EQ(shelf_1->shelf_widget()->GetNativeWindow()->GetRootWindow(),
            window_1->GetRootWindow());
  EXPECT_EQ(shelf_2->shelf_widget()->GetNativeWindow()->GetRootWindow(),
            window_2->GetRootWindow());
  EXPECT_TRUE(window_1->IsVisible());
  EXPECT_TRUE(window_2->IsVisible());

  // Enable system modal dialog, and make sure both shelves are still hidden.
  WmShell* wm_shell = WmShell::Get();
  wm_shell->SimulateModalWindowOpenForTesting(true);
  EXPECT_TRUE(wm_shell->IsSystemModalWindowOpen());
  EXPECT_FALSE(wm::CanActivateWindow(window_1));
  EXPECT_FALSE(wm::CanActivateWindow(window_2));
  Shell::GetInstance()->UpdateShelfVisibility();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_2->GetAutoHideState());
}

// Tests that the shelf is only hidden for a fullscreen window at the front and
// toggles visibility when another window is activated.
TEST_F(ShelfLayoutManagerTest, FullscreenWindowInFrontHidesShelf) {
  Shelf* shelf = GetShelf();

  // Create a window and make it full screen.
  aura::Window* window1 = CreateTestWindow();
  window1->SetBounds(gfx::Rect(0, 0, 100, 100));
  window1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  window1->Show();

  aura::Window* window2 = CreateTestWindow();
  window2->SetBounds(gfx::Rect(0, 0, 100, 100));
  window2->Show();

  wm::GetWindowState(window1)->Activate();
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());

  wm::GetWindowState(window2)->Activate();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  wm::GetWindowState(window1)->Activate();
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());
}

// Test the behavior of the shelf when a window on one display is fullscreen
// but the other display has the active window.
TEST_F(ShelfLayoutManagerTest, FullscreenWindowOnSecondDisplay) {
  if (!SupportsMultipleDisplays())
    return;

  UpdateDisplay("800x600,800x600");
  aura::Window::Windows root_windows = Shell::GetAllRootWindows();
  Shell::RootWindowControllerList root_window_controllers =
      Shell::GetAllRootWindowControllers();

  // Create windows on either display.
  aura::Window* window1 = CreateTestWindow();
  window1->SetBoundsInScreen(gfx::Rect(0, 0, 100, 100),
                             display::Screen::GetScreen()->GetAllDisplays()[0]);
  window1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);
  window1->Show();

  aura::Window* window2 = CreateTestWindow();
  window2->SetBoundsInScreen(gfx::Rect(800, 0, 100, 100),
                             display::Screen::GetScreen()->GetAllDisplays()[1]);
  window2->Show();

  EXPECT_EQ(root_windows[0], window1->GetRootWindow());
  EXPECT_EQ(root_windows[1], window2->GetRootWindow());

  wm::GetWindowState(window2)->Activate();
  EXPECT_EQ(
      SHELF_HIDDEN,
      root_window_controllers[0]->GetShelfLayoutManager()->visibility_state());
  EXPECT_EQ(
      SHELF_VISIBLE,
      root_window_controllers[1]->GetShelfLayoutManager()->visibility_state());
}

// Test for Pinned mode.
TEST_F(ShelfLayoutManagerTest, PinnedWindowHidesShelf) {
  Shelf* shelf = GetShelf();

  aura::Window* window1 = CreateTestWindow();
  window1->SetBounds(gfx::Rect(0, 0, 100, 100));
  window1->Show();

  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  wm::PinWindow(window1);
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());

  WmWindowAura::Get(window1)->GetWindowState()->Restore();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
}

// Tests SHELF_ALIGNMENT_(LEFT, RIGHT).
TEST_F(ShelfLayoutManagerTest, SetAlignment) {
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  // Force an initial layout.
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  layout_manager->LayoutShelf();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  shelf->SetAlignment(SHELF_ALIGNMENT_LEFT);
  gfx::Rect shelf_bounds(GetShelfWidget()->GetWindowBoundsInScreen());
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  ASSERT_NE(-1, display.id());
  EXPECT_EQ(layout_manager->GetIdealBounds().width(),
            display.GetWorkAreaInsets().left());
  EXPECT_GE(shelf_bounds.width(),
            GetShelfWidget()->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(SHELF_ALIGNMENT_LEFT, GetPrimarySystemTray()->shelf_alignment());
  StatusAreaWidget* status_area_widget = GetShelfWidget()->status_area_widget();
  gfx::Rect status_bounds(status_area_widget->GetWindowBoundsInScreen());
  EXPECT_GE(status_bounds.width(),
            status_area_widget->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(layout_manager->GetIdealBounds().width(),
            display.GetWorkAreaInsets().left());
  EXPECT_EQ(0, display.GetWorkAreaInsets().top());
  EXPECT_EQ(0, display.GetWorkAreaInsets().bottom());
  EXPECT_EQ(0, display.GetWorkAreaInsets().right());
  EXPECT_EQ(display.bounds().x(), shelf_bounds.x());
  EXPECT_EQ(display.bounds().y(), shelf_bounds.y());
  EXPECT_EQ(display.bounds().height(), shelf_bounds.height());
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  display = display::Screen::GetScreen()->GetPrimaryDisplay();
  EXPECT_EQ(GetShelfConstant(SHELF_INSETS_FOR_AUTO_HIDE),
            display.GetWorkAreaInsets().left());
  EXPECT_EQ(GetShelfConstant(SHELF_INSETS_FOR_AUTO_HIDE),
            display.work_area().x());

  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  shelf->SetAlignment(SHELF_ALIGNMENT_RIGHT);
  shelf_bounds = GetShelfWidget()->GetWindowBoundsInScreen();
  display = display::Screen::GetScreen()->GetPrimaryDisplay();
  ASSERT_NE(-1, display.id());
  EXPECT_EQ(layout_manager->GetIdealBounds().width(),
            display.GetWorkAreaInsets().right());
  EXPECT_GE(shelf_bounds.width(),
            GetShelfWidget()->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(SHELF_ALIGNMENT_RIGHT, GetPrimarySystemTray()->shelf_alignment());
  status_bounds = gfx::Rect(status_area_widget->GetWindowBoundsInScreen());
  EXPECT_GE(status_bounds.width(),
            status_area_widget->GetContentsView()->GetPreferredSize().width());
  EXPECT_EQ(layout_manager->GetIdealBounds().width(),
            display.GetWorkAreaInsets().right());
  EXPECT_EQ(0, display.GetWorkAreaInsets().top());
  EXPECT_EQ(0, display.GetWorkAreaInsets().bottom());
  EXPECT_EQ(0, display.GetWorkAreaInsets().left());
  EXPECT_EQ(display.work_area().right(), shelf_bounds.x());
  EXPECT_EQ(display.bounds().y(), shelf_bounds.y());
  EXPECT_EQ(display.bounds().height(), shelf_bounds.height());
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  display = display::Screen::GetScreen()->GetPrimaryDisplay();
  EXPECT_EQ(GetShelfConstant(SHELF_INSETS_FOR_AUTO_HIDE),
            display.GetWorkAreaInsets().right());
  EXPECT_EQ(GetShelfConstant(SHELF_INSETS_FOR_AUTO_HIDE),
            display.bounds().right() - display.work_area().right());
}

TEST_F(ShelfLayoutManagerTest, GestureEdgeSwipe) {
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  views::Widget* widget = CreateTestWidget();
  widget->Maximize();

  ui::test::EventGenerator& generator(GetEventGenerator());

  aura::Window* window = widget->GetNativeWindow();
  layout_manager->LayoutShelf();

  // Edge swipe when SHELF_VISIBLE should not change visibility state.
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  generator.GestureEdgeSwipe();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  // Edge swipe when AUTO_HIDE_HIDDEN should change to AUTO_HIDE_SHOWN.
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  layout_manager->LayoutShelf();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  generator.GestureEdgeSwipe();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  widget->SetFullscreen(true);
  wm::GetWindowState(window)->set_hide_shelf_when_fullscreen(false);
  layout_manager->UpdateVisibilityState();

  // Edge swipe in fullscreen + AUTO_HIDE_HIDDEN should show the shelf and
  // remain fullscreen.
  EXPECT_TRUE(widget->IsFullscreen());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  generator.GestureEdgeSwipe();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_TRUE(widget->IsFullscreen());
}

// Tests that gesture edge swipe events are forwarded to the right shelf on the
// right monitor (crbug.com/449851).
TEST_F(ShelfLayoutManagerTest, GestureEdgeSwipeMultiMonitor) {
  if (!SupportsMultipleDisplays())
    return;

  // Create two displays.
  UpdateDisplay("200x200,100x100");
  auto root_window_controllers = Shell::GetAllRootWindowControllers();
  ASSERT_EQ(2U, root_window_controllers.size());
  Shelf* shelf_1 = root_window_controllers[0]->GetShelf();
  Shelf* shelf_2 = root_window_controllers[1]->GetShelf();

  // Create two maximized windows, one in each display.
  aura::Window* window_1 =
      CreateTestWindowInParent(root_window_controllers[0]->GetRootWindow());
  window_1->SetBounds(gfx::Rect(0, 0, 100, 100));
  window_1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window_1->Show();
  aura::Window* window_2 =
      CreateTestWindowInParent(root_window_controllers[1]->GetRootWindow());
  window_2->SetBounds(gfx::Rect(201, 0, 100, 100));
  window_2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window_2->Show();

  // Make sure both are set to auto-hide and both are hidden.
  shelf_1->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  shelf_2->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  shelf_1->shelf_layout_manager()->LayoutShelf();
  shelf_2->shelf_layout_manager()->LayoutShelf();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_2->GetAutoHideState());

  ui::test::EventGenerator monitor_1_generator(
      root_window_controllers[0]->GetRootWindow());
  ui::test::EventGenerator monitor_2_generator(
      root_window_controllers[1]->GetRootWindow());

  // An edge swipe in one display should only affect the shelf in that display.
  monitor_1_generator.GestureEdgeSwipe();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_2->GetAutoHideState());

  // Back to normal after an update.
  Shell::GetInstance()->UpdateShelfVisibility();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_2->GetAutoHideState());

  monitor_2_generator.GestureEdgeSwipe();
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_1->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf_2->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf_1->GetAutoHideState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf_2->GetAutoHideState());
}

TEST_F(ShelfLayoutManagerTest, GestureDrag) {
  // Slop is an implementation detail of gesture recognition, and complicates
  // these tests. Ignore it.
  ui::GestureConfiguration::GetInstance()
      ->set_max_touch_move_in_pixels_for_click(0);
  Shelf* shelf = GetShelf();
  {
    SCOPED_TRACE("BOTTOM");
    shelf->SetAlignment(SHELF_ALIGNMENT_BOTTOM);
    RunGestureDragTests(gfx::Vector2d(0, 120));
  }

  {
    SCOPED_TRACE("LEFT");
    shelf->SetAlignment(SHELF_ALIGNMENT_LEFT);
    RunGestureDragTests(gfx::Vector2d(-120, 0));
  }

  {
    SCOPED_TRACE("RIGHT");
    shelf->SetAlignment(SHELF_ALIGNMENT_RIGHT);
    RunGestureDragTests(gfx::Vector2d(120, 0));
  }
}

TEST_F(ShelfLayoutManagerTest, WindowVisibilityDisablesAutoHide) {
  if (!SupportsMultipleDisplays())
    return;

  UpdateDisplay("800x600,800x600");
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  layout_manager->LayoutShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);

  // Create a visible window so auto-hide behavior is enforced
  views::Widget* dummy = CreateTestWidget();

  // Window visible => auto hide behaves normally.
  layout_manager->UpdateVisibilityState();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Window minimized => auto hide disabled.
  dummy->Minimize();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  // Window closed => auto hide disabled.
  dummy->CloseNow();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  // Multiple window test
  views::Widget* window1 = CreateTestWidget();
  views::Widget* window2 = CreateTestWidget();

  // both visible => normal autohide
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // either minimzed => normal autohide
  window2->Minimize();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  window2->Restore();
  window1->Minimize();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // both minimized => disable auto hide
  window2->Minimize();
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

  // Test moving windows to/from other display.
  window2->Restore();
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  // Move to second display.
  window2->SetBounds(gfx::Rect(850, 50, 50, 50));
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  // Move back to primary display.
  window2->SetBounds(gfx::Rect(50, 50, 50, 50));
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
}

// Test that the shelf animates back to its normal position upon a user
// completing a gesture drag.
TEST_F(ShelfLayoutManagerTest, ShelfAnimatesWhenGestureComplete) {
  // Test the shelf animates back to its original visible bounds when it is
  // dragged when there are no visible windows.
  Shelf* shelf = GetShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  gfx::Rect visible_bounds = GetShelfWidget()->GetWindowBoundsInScreen();
  {
    // Enable animations so that we can make sure that they occur.
    ui::ScopedAnimationDurationScaleMode regular_animations(
        ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);

    ui::test::EventGenerator& generator(GetEventGenerator());
    gfx::Rect shelf_bounds_in_screen =
        GetShelfWidget()->GetWindowBoundsInScreen();
    gfx::Point start(shelf_bounds_in_screen.CenterPoint());
    gfx::Point end(start.x(), shelf_bounds_in_screen.bottom());
    generator.GestureScrollSequence(start, end,
                                    base::TimeDelta::FromMilliseconds(10), 5);
    EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
    EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());

    ShelfAnimationWaiter waiter(visible_bounds);
    // Wait till the animation completes and check that it occurred.
    waiter.WaitTillDoneAnimating();
    EXPECT_TRUE(waiter.WasValidAnimation());
  }

  // Create a visible window so auto-hide behavior is enforced.
  CreateTestWidget();

  // Get the bounds of the shelf when it is hidden.
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
  gfx::Rect auto_hidden_bounds = GetShelfWidget()->GetWindowBoundsInScreen();

  {
    // Enable the animations so that we can make sure they do occur.
    ui::ScopedAnimationDurationScaleMode regular_animations(
        ui::ScopedAnimationDurationScaleMode::NON_ZERO_DURATION);

    gfx::Point start =
        GetShelfWidget()->GetWindowBoundsInScreen().CenterPoint();
    gfx::Point end(start.x(), start.y() - 100);
    ui::test::EventGenerator& generator(GetEventGenerator());

    // Test that the shelf animates to the visible bounds after a swipe up on
    // the auto hidden shelf.
    generator.GestureScrollSequence(start, end,
                                    base::TimeDelta::FromMilliseconds(10), 1);
    EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
    ShelfAnimationWaiter waiter1(visible_bounds);
    waiter1.WaitTillDoneAnimating();
    EXPECT_TRUE(waiter1.WasValidAnimation());

    // Test that the shelf animates to the auto hidden bounds after a swipe up
    // on the visible shelf.
    EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
    generator.GestureScrollSequence(start, end,
                                    base::TimeDelta::FromMilliseconds(10), 1);
    EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
    EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());
    ShelfAnimationWaiter waiter2(auto_hidden_bounds);
    waiter2.WaitTillDoneAnimating();
    EXPECT_TRUE(waiter2.WasValidAnimation());
  }
}

TEST_F(ShelfLayoutManagerTest, ShelfFlickerOnTrayActivation) {
  Shelf* shelf = GetShelf();

  // Create a visible window so auto-hide behavior is enforced.
  CreateTestWidget();

  // Turn on auto-hide for the shelf.
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  // Show the status menu. That should make the shelf visible again.
  WmShell::Get()->accelerator_controller()->PerformActionIfEnabled(
      SHOW_SYSTEM_TRAY_BUBBLE);
  EXPECT_EQ(SHELF_AUTO_HIDE, shelf->GetVisibilityState());
  EXPECT_EQ(SHELF_AUTO_HIDE_SHOWN, shelf->GetAutoHideState());
  EXPECT_TRUE(GetPrimarySystemTray()->HasSystemBubble());
}

TEST_F(ShelfLayoutManagerTest, WorkAreaChangeWorkspace) {
  // Make sure the shelf is always visible.
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  layout_manager->LayoutShelf();

  views::Widget* widget_one = CreateTestWidget();
  widget_one->Maximize();

  views::Widget* widget_two = CreateTestWidget();
  widget_two->Maximize();
  widget_two->Activate();

  // Both windows are maximized. They should be of the same size.
  EXPECT_EQ(widget_one->GetNativeWindow()->bounds().ToString(),
            widget_two->GetNativeWindow()->bounds().ToString());
  int area_when_shelf_shown =
      widget_one->GetNativeWindow()->bounds().size().GetArea();

  // Now hide the shelf.
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);

  // Both windows should be resized according to the shelf status.
  EXPECT_EQ(widget_one->GetNativeWindow()->bounds().ToString(),
            widget_two->GetNativeWindow()->bounds().ToString());
  // Resized to small.
  EXPECT_LT(area_when_shelf_shown,
            widget_one->GetNativeWindow()->bounds().size().GetArea());

  // Now show the shelf.
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);

  // Again both windows should be of the same size.
  EXPECT_EQ(widget_one->GetNativeWindow()->bounds().ToString(),
            widget_two->GetNativeWindow()->bounds().ToString());
  EXPECT_EQ(area_when_shelf_shown,
            widget_one->GetNativeWindow()->bounds().size().GetArea());
}

// Confirm that the shelf is dimmed only when content is maximized and
// shelf is not autohidden.
TEST_F(ShelfLayoutManagerTest, Dimming) {
  Shelf* shelf = GetShelf();
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  std::unique_ptr<aura::Window> w1(CreateTestWindow());
  w1->Show();
  wm::ActivateWindow(w1.get());

  // Normal window doesn't dim shelf.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  ShelfWidget* shelf_widget = GetShelfWidget();
  EXPECT_FALSE(shelf_widget->GetDimsShelf());

  // Maximized window does.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  EXPECT_TRUE(shelf_widget->GetDimsShelf());

  // Change back to normal stops dimming.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_NORMAL);
  EXPECT_FALSE(shelf_widget->GetDimsShelf());

  // Changing back to maximized dims again.
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  EXPECT_TRUE(shelf_widget->GetDimsShelf());

  // Changing shelf to autohide stops dimming.
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_FALSE(shelf_widget->GetDimsShelf());
}

// Make sure that the shelf will not hide if the mouse is between a bubble and
// the shelf.
TEST_F(ShelfLayoutManagerTest, BubbleEnlargesShelfMouseHitArea) {
  Shelf* shelf = GetShelf();
  ShelfLayoutManager* layout_manager = GetShelfLayoutManager();
  StatusAreaWidget* status_area_widget =
      shelf->shelf_widget()->status_area_widget();
  SystemTray* tray = GetPrimarySystemTray();

  // Create a visible window so auto-hide behavior is enforced.
  CreateTestWidget();

  layout_manager->LayoutShelf();
  ui::test::EventGenerator& generator(GetEventGenerator());

  // Make two iterations - first without a message bubble which should make
  // the shelf disappear and then with a message bubble which should keep it
  // visible.
  for (int i = 0; i < 2; i++) {
    // Make sure the shelf is visible and position the mouse over it. Then
    // allow auto hide.
    shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
    EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());
    gfx::Point center =
        status_area_widget->GetWindowBoundsInScreen().CenterPoint();
    generator.MoveMouseTo(center.x(), center.y());
    shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
    EXPECT_TRUE(layout_manager->IsVisible());
    if (!i) {
      // In our first iteration we make sure there is no bubble.
      tray->CloseSystemBubble();
      EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());
    } else {
      // In our second iteration we show a bubble.
      test::TestSystemTrayItem* item = new test::TestSystemTrayItem();
      tray->AddTrayItem(item);
      tray->ShowNotificationView(item);
      EXPECT_TRUE(status_area_widget->IsMessageBubbleShown());
    }
    // Move the pointer over the edge of the shelf.
    generator.MoveMouseTo(
        center.x(), status_area_widget->GetWindowBoundsInScreen().y() - 8);
    layout_manager->UpdateVisibilityState();
    if (i) {
      EXPECT_TRUE(layout_manager->IsVisible());
      EXPECT_TRUE(status_area_widget->IsMessageBubbleShown());
    } else {
      EXPECT_FALSE(layout_manager->IsVisible());
      EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());
    }
  }
}

TEST_F(ShelfLayoutManagerTest, ShelfBackgroundColor) {
  EXPECT_EQ(SHELF_BACKGROUND_DEFAULT, GetShelfWidget()->GetBackgroundType());

  std::unique_ptr<aura::Window> w1(CreateTestWindow());
  w1->Show();
  wm::ActivateWindow(w1.get());
  EXPECT_EQ(SHELF_BACKGROUND_DEFAULT, GetShelfWidget()->GetBackgroundType());
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  EXPECT_EQ(SHELF_BACKGROUND_MAXIMIZED, GetShelfWidget()->GetBackgroundType());

  std::unique_ptr<aura::Window> w2(CreateTestWindow());
  w2->Show();
  wm::ActivateWindow(w2.get());
  // Overlaps with shelf.
  w2->SetBounds(GetShelfLayoutManager()->GetIdealBounds());

  // Still background is 'maximized'.
  EXPECT_EQ(SHELF_BACKGROUND_MAXIMIZED, GetShelfWidget()->GetBackgroundType());

  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
  EXPECT_EQ(SHELF_BACKGROUND_OVERLAP, GetShelfWidget()->GetBackgroundType());
  w2->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
  EXPECT_EQ(SHELF_BACKGROUND_DEFAULT, GetShelfWidget()->GetBackgroundType());

  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  EXPECT_EQ(SHELF_BACKGROUND_MAXIMIZED, GetShelfWidget()->GetBackgroundType());
  w1.reset();
  EXPECT_EQ(SHELF_BACKGROUND_DEFAULT, GetShelfWidget()->GetBackgroundType());
}

// Verify that the shelf doesn't have the opaque background if it's auto-hide
// status.
TEST_F(ShelfLayoutManagerTest, ShelfBackgroundColorAutoHide) {
  EXPECT_EQ(SHELF_BACKGROUND_DEFAULT, GetShelfWidget()->GetBackgroundType());

  GetShelf()->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  std::unique_ptr<aura::Window> w1(CreateTestWindow());
  w1->Show();
  wm::ActivateWindow(w1.get());
  EXPECT_EQ(SHELF_BACKGROUND_OVERLAP, GetShelfWidget()->GetBackgroundType());
  w1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  EXPECT_EQ(SHELF_BACKGROUND_OVERLAP, GetShelfWidget()->GetBackgroundType());
}

// Verify the hit bounds of the status area extend to the edge of the shelf.
TEST_F(ShelfLayoutManagerTest, StatusAreaHitBoxCoversEdge) {
  StatusAreaWidget* status_area_widget = GetShelfWidget()->status_area_widget();
  ui::test::EventGenerator& generator(GetEventGenerator());
  display::Display display = display::Screen::GetScreen()->GetPrimaryDisplay();
  gfx::Rect inset_display_bounds = display.bounds();
  inset_display_bounds.Inset(0, 0, 1, 1);

  // Test bottom right pixel for bottom alignment.
  GetShelf()->SetAlignment(SHELF_ALIGNMENT_BOTTOM);
  generator.MoveMouseTo(inset_display_bounds.bottom_right());
  EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());
#if !defined(OS_WIN)
  generator.ClickLeftButton();
  // The bottom right pixel doesn't work on Windows; see crbug.com/633434
  EXPECT_TRUE(status_area_widget->IsMessageBubbleShown());
#endif
  generator.ClickLeftButton();
  EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());

  // Test bottom right pixel for right alignment.
  GetShelf()->SetAlignment(SHELF_ALIGNMENT_RIGHT);
  generator.MoveMouseTo(inset_display_bounds.bottom_right());
  EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());
#if !defined(OS_WIN)
  generator.ClickLeftButton();
  // The bottom right pixel doesn't work on Windows; see crbug.com/633434
  EXPECT_TRUE(status_area_widget->IsMessageBubbleShown());
#endif
  generator.ClickLeftButton();
  EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());

  // Test bottom left pixel for left alignment.
  generator.MoveMouseTo(inset_display_bounds.bottom_left());
  GetShelf()->SetAlignment(SHELF_ALIGNMENT_LEFT);
  EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());
  generator.ClickLeftButton();
  EXPECT_TRUE(status_area_widget->IsMessageBubbleShown());
  generator.ClickLeftButton();
  EXPECT_FALSE(status_area_widget->IsMessageBubbleShown());
}

// Tests that when the auto-hide behaviour is changed during an animation the
// target bounds are updated to reflect the new state.
TEST_F(ShelfLayoutManagerTest,
       ShelfAutoHideToggleDuringAnimationUpdatesBounds) {
  aura::Window* status_window =
      GetShelfWidget()->status_area_widget()->GetNativeView();
  gfx::Rect initial_bounds = status_window->bounds();

  ui::ScopedAnimationDurationScaleMode regular_animations(
      ui::ScopedAnimationDurationScaleMode::SLOW_DURATION);
  GetShelf()->SetAutoHideBehavior(SHELF_AUTO_HIDE_ALWAYS_HIDDEN);
  gfx::Rect hide_target_bounds = status_window->GetTargetBounds();
  EXPECT_GT(hide_target_bounds.y(), initial_bounds.y());

  GetShelf()->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_NEVER);
  gfx::Rect reshow_target_bounds = status_window->GetTargetBounds();
  EXPECT_EQ(initial_bounds, reshow_target_bounds);
}

// Tests that during shutdown, that window activation changes are properly
// handled, and do not crash (crbug.com/458768)
TEST_F(ShelfLayoutManagerTest, ShutdownHandlesWindowActivation) {
  GetShelf()->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);

  aura::Window* window1 = CreateTestWindowInShellWithId(0);
  window1->SetBounds(gfx::Rect(0, 0, 100, 100));
  window1->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
  window1->Show();
  std::unique_ptr<aura::Window> window2(CreateTestWindowInShellWithId(0));
  window2->SetBounds(gfx::Rect(0, 0, 100, 100));
  window2->Show();
  wm::ActivateWindow(window1);

  GetShelfWidget()->Shutdown();

  // Deleting a focused maximized window will switch focus to |window2|. This
  // would normally cause the ShelfLayoutManager to update its state. However
  // during shutdown we want to handle this without crashing.
  delete window1;
}

TEST_F(ShelfLayoutManagerTest, ShelfLayoutInUnifiedDesktop) {
  if (!SupportsMultipleDisplays())
    return;

  Shell::GetInstance()->display_manager()->SetUnifiedDesktopEnabled(true);
  UpdateDisplay("500x400, 500x400");

  StatusAreaWidget* status_area_widget = GetShelfWidget()->status_area_widget();
  EXPECT_TRUE(status_area_widget->IsVisible());
  // Shelf should be in the first display's area.
  gfx::Rect status_area_bounds(status_area_widget->GetWindowBoundsInScreen());
  EXPECT_TRUE(gfx::Rect(0, 0, 500, 400).Contains(status_area_bounds));
  EXPECT_EQ(gfx::Point(500, 400), status_area_bounds.bottom_right());
}

}  // namespace ash