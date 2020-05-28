// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMMON_SYSTEM_CHROMEOS_PALETTE_PALETTE_BUTTON_TRAY_H_
#define ASH_COMMON_SYSTEM_CHROMEOS_PALETTE_PALETTE_BUTTON_TRAY_H_

#include <map>
#include <memory>

#include "ash/ash_export.h"
#include "ash/common/session/session_state_observer.h"
#include "ash/common/shell_observer.h"
#include "ash/common/system/chromeos/palette/palette_tool_manager.h"
#include "ash/common/system/tray/tray_background_view.h"
#include "base/macros.h"

namespace views {
class ImageView;
class Widget;
}

namespace ash {

class TrayBubbleWrapper;
class PaletteToolManager;

// The PaletteTray shows the palette in the bottom area of the screen. This
// class also controls the lifetime for all of the tools available in the
// palette.
class ASH_EXPORT PaletteTray : public TrayBackgroundView,
                               public SessionStateObserver,
                               public ShellObserver,
                               public PaletteToolManager::Delegate,
                               public views::TrayBubbleView::Delegate {
 public:
  explicit PaletteTray(WmShelf* wm_shelf);
  ~PaletteTray() override;

  // ActionableView:
  bool PerformAction(const ui::Event& event) override;

  // SessionStateObserver:
  void SessionStateChanged(SessionStateDelegate::SessionState state) override;

  // TrayBackgroundView:
  void ClickedOutsideBubble() override;
  base::string16 GetAccessibleNameForTray() override;
  void HideBubbleWithView(const views::TrayBubbleView* bubble_view) override;
  void SetShelfAlignment(ShelfAlignment alignment) override;
  void AnchorUpdated() override;

 private:
  // views::TrayBubbleView::Delegate:
  void BubbleViewDestroyed() override;
  void OnMouseEnteredView() override;
  void OnMouseExitedView() override;
  base::string16 GetAccessibleNameForBubble() override;
  gfx::Rect GetAnchorRect(views::Widget* anchor_widget,
                          AnchorType anchor_type,
                          AnchorAlignment anchor_alignment) const override;
  void OnBeforeBubbleWidgetInit(
      views::Widget* anchor_widget,
      views::Widget* bubble_widget,
      views::Widget::InitParams* params) const override;
  void HideBubble(const views::TrayBubbleView* bubble_view) override;

  // PaletteToolManager::Delegate:
  void HidePalette() override;
  void OnActiveToolChanged() override;
  WmWindow* GetWindow() override;

  // Creates a new border for the icon. The padding is determined based on the
  // alignment of the shelf.
  void SetIconBorderForShelfAlignment();

  // Updates the tray icon from the palette tool manager.
  void UpdateTrayIcon();

  // Sets the icon to visible if the palette can be used.
  void UpdateIconVisibility();

  bool OpenBubble();
  void AddToolsToView(views::View* host);

  std::unique_ptr<PaletteToolManager> palette_tool_manager_;
  std::unique_ptr<TrayBubbleWrapper> bubble_;

  // Weak pointer, will be parented by TrayContainer for its lifetime.
  views::ImageView* icon_;

  DISALLOW_COPY_AND_ASSIGN(PaletteTray);
};

}  // namespace ash

#endif  // ASH_COMMON_SYSTEM_CHROMEOS_PALETTE_PALETTE_BUTTON_TRAY_H_
