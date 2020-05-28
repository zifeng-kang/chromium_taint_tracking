// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/chromeos/palette/palette_tray.h"

#include "ash/common/ash_switches.h"
#include "ash/common/material_design/material_design_controller.h"
#include "ash/common/shelf/shelf_constants.h"
#include "ash/common/shelf/wm_shelf.h"
#include "ash/common/shelf/wm_shelf_util.h"
#include "ash/common/shell_window_ids.h"
#include "ash/common/system/chromeos/palette/palette_tool.h"
#include "ash/common/system/chromeos/palette/palette_tool_manager.h"
#include "ash/common/system/chromeos/palette/palette_utils.h"
#include "ash/common/system/tray/system_tray_delegate.h"
#include "ash/common/system/tray/tray_bubble_wrapper.h"
#include "ash/common/system/tray/tray_constants.h"
#include "ash/common/system/tray/tray_popup_header_button.h"
#include "ash/common/wm_lookup.h"
#include "ash/common/wm_root_window_controller.h"
#include "ash/common/wm_shell.h"
#include "ash/common/wm_window.h"
#include "base/command_line.h"
#include "base/sys_info.h"
#include "grit/ash_resources.h"
#include "grit/ash_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/separator.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"

namespace ash {

namespace {

// Predefined padding for the icon used in this tray. These are to be set to the
// border of the icon, depending on the current |shelf_alignment()|.
const int kHorizontalShelfHorizontalPadding = 8;
const int kHorizontalShelfVerticalPadding = 4;
const int kVerticalShelfHorizontalPadding = 2;
const int kVerticalShelfVerticalPadding = 5;

// Width of the palette itself (dp).
const int kPaletteWidth = 360;

// Creates a separator.
views::Separator* CreateSeparator(views::Separator::Orientation orientation) {
  const int kSeparatorInset = 10;

  views::Separator* separator =
      new views::Separator(views::Separator::HORIZONTAL);
  separator->SetColor(ash::kBorderDarkColor);
  separator->SetBorder(
      views::Border::CreateEmptyBorder(kSeparatorInset, 0, kSeparatorInset, 0));
  return separator;
}

// Creates the title-bar view.
views::View* CreateTitleView() {
  auto& rb = ui::ResourceBundle::GetSharedInstance();

  auto* root = new views::View();
  auto* box_layout =
      new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 0);
  root->SetLayoutManager(box_layout);
  root->SetBorder(views::Border::CreateEmptyBorder(
      0, ash::kTrayPopupPaddingHorizontal, 0, 0));

  views::Label* text_label =
      new views::Label(l10n_util::GetStringUTF16(IDS_ASH_PALETTE_TITLE));
  text_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  text_label->SetFontList(rb.GetFontList(ui::ResourceBundle::BoldFont));
  root->AddChildView(text_label);
  box_layout->SetFlexForView(text_label, 1);

  // TODO(jdufault): Use proper icons.
  ash::TrayPopupHeaderButton* help_button = new ash::TrayPopupHeaderButton(
      nullptr, IDR_AURA_UBER_TRAY_SHUTDOWN, IDR_AURA_UBER_TRAY_SHUTDOWN,
      IDR_AURA_UBER_TRAY_SHUTDOWN_HOVER, IDR_AURA_UBER_TRAY_SHUTDOWN_HOVER,
      IDS_ASH_STATUS_TRAY_SHUTDOWN);
  help_button->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_SHUTDOWN));
  root->AddChildView(help_button);

  root->AddChildView(CreateSeparator(views::Separator::VERTICAL));

  // TODO(jdufault): Use proper icons.
  ash::TrayPopupHeaderButton* settings_button = new ash::TrayPopupHeaderButton(
      nullptr, IDR_AURA_UBER_TRAY_SHUTDOWN, IDR_AURA_UBER_TRAY_SHUTDOWN,
      IDR_AURA_UBER_TRAY_SHUTDOWN_HOVER, IDR_AURA_UBER_TRAY_SHUTDOWN_HOVER,
      IDS_ASH_STATUS_TRAY_SHUTDOWN);
  settings_button->SetTooltipText(
      l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_SHUTDOWN));
  root->AddChildView(settings_button);

  return root;
}

}  // namespace

PaletteTray::PaletteTray(WmShelf* wm_shelf)
    : TrayBackgroundView(wm_shelf),
      palette_tool_manager_(new PaletteToolManager(this)) {
  PaletteTool::RegisterToolInstances(palette_tool_manager_.get());

  SetContentsBackground();

  SetLayoutManager(new views::FillLayout());
  icon_ = new views::ImageView();
  UpdateTrayIcon();

  SetIconBorderForShelfAlignment();
  tray_container()->AddChildView(icon_);

  WmShell::Get()->AddShellObserver(this);
  WmShell::Get()->GetSessionStateDelegate()->AddSessionStateObserver(this);

  UpdateIconVisibility();
}

PaletteTray::~PaletteTray() {
  if (bubble_)
    bubble_->bubble_view()->reset_delegate();

  WmShell::Get()->RemoveShellObserver(this);
  WmShell::Get()->GetSessionStateDelegate()->RemoveSessionStateObserver(this);
}

bool PaletteTray::PerformAction(const ui::Event& event) {
  if (bubble_) {
    bubble_.reset();
    return true;
  }

  return OpenBubble();
}

bool PaletteTray::OpenBubble() {
  views::TrayBubbleView::InitParams init_params(
      views::TrayBubbleView::ANCHOR_TYPE_TRAY, GetAnchorAlignment(),
      kPaletteWidth, kPaletteWidth);
  init_params.first_item_has_no_margin = true;
  init_params.can_activate = true;
  init_params.close_on_deactivate = true;

  DCHECK(tray_container());

  // Create view, customize it.
  views::TrayBubbleView* bubble_view =
      views::TrayBubbleView::Create(tray_container(), this, &init_params);
  bubble_view->SetArrowPaintType(views::BubbleBorder::PAINT_NONE);
  bubble_view->UseCompactMargins();
  bubble_view->set_margins(gfx::Insets(bubble_view->margins().top(), 0, 0, 0));

  // Add child views.
  bubble_view->AddChildView(CreateTitleView());
  bubble_view->AddChildView(CreateSeparator(views::Separator::HORIZONTAL));
  AddToolsToView(bubble_view);

  // Show the bubble.
  bubble_.reset(new ash::TrayBubbleWrapper(this, bubble_view));

  SetDrawBackgroundAsActive(true);

  return true;
}

void PaletteTray::AddToolsToView(views::View* host) {
  std::vector<PaletteToolView> views = palette_tool_manager_->CreateViews();

  // There may not be any registered tools.
  if (!views.size())
    return;

  PaletteGroup group = views[0].group;
  for (const PaletteToolView& view : views) {
    // If the group changes, add a separator.
    if (group != view.group) {
      group = view.group;
      host->AddChildView(CreateSeparator(views::Separator::HORIZONTAL));
    }

    host->AddChildView(view.view);
  }
}

void PaletteTray::SessionStateChanged(
    SessionStateDelegate::SessionState state) {
  UpdateIconVisibility();
}

void PaletteTray::ClickedOutsideBubble() {
  HidePalette();
}

base::string16 PaletteTray::GetAccessibleNameForTray() {
  return l10n_util::GetStringUTF16(IDS_ASH_PALETTE_TITLE);
}

void PaletteTray::HideBubbleWithView(const views::TrayBubbleView* bubble_view) {
  if (bubble_->bubble_view() == bubble_view)
    bubble_.reset();
}

void PaletteTray::BubbleViewDestroyed() {
  palette_tool_manager_->NotifyViewsDestroyed();
  SetDrawBackgroundAsActive(false);
}

void PaletteTray::OnMouseEnteredView() {}

void PaletteTray::OnMouseExitedView() {}

base::string16 PaletteTray::GetAccessibleNameForBubble() {
  return GetAccessibleNameForTray();
}

gfx::Rect PaletteTray::GetAnchorRect(
    views::Widget* anchor_widget,
    views::TrayBubbleView::AnchorType anchor_type,
    views::TrayBubbleView::AnchorAlignment anchor_alignment) const {
  gfx::Rect r =
      GetBubbleAnchorRect(anchor_widget, anchor_type, anchor_alignment);

  // Move the palette to the left so the right edge of the palette aligns with
  // the right edge of the tray button.
  if (IsHorizontalAlignment(shelf_alignment()))
    r.Offset(-r.width() + tray_container()->width(), 0);
  else
    r.Offset(0, -r.height() + tray_container()->height());

  return r;
}

void PaletteTray::OnBeforeBubbleWidgetInit(
    views::Widget* anchor_widget,
    views::Widget* bubble_widget,
    views::Widget::InitParams* params) const {
  // Place the bubble in the same root window as |anchor_widget|.
  WmLookup::Get()
      ->GetWindowForWidget(anchor_widget)
      ->GetRootWindowController()
      ->ConfigureWidgetInitParamsForContainer(
          bubble_widget, kShellWindowId_SettingBubbleContainer, params);
}

void PaletteTray::HideBubble(const views::TrayBubbleView* bubble_view) {
  HideBubbleWithView(bubble_view);
}

void PaletteTray::HidePalette() {
  bubble_.reset();
}

void PaletteTray::OnActiveToolChanged() {
  UpdateTrayIcon();
}

WmWindow* PaletteTray::GetWindow() {
  return shelf()->GetWindow();
}

void PaletteTray::SetShelfAlignment(ShelfAlignment alignment) {
  if (alignment == shelf_alignment())
    return;

  TrayBackgroundView::SetShelfAlignment(alignment);
  SetIconBorderForShelfAlignment();
}

void PaletteTray::AnchorUpdated() {
  if (bubble_)
    bubble_->bubble_view()->UpdateBubble();
}

void PaletteTray::SetIconBorderForShelfAlignment() {
  // TODO(tdanderson): Ensure PaletteTray follows material design specs. See
  // crbug.com/630464.
  if (IsHorizontalAlignment(shelf_alignment())) {
    icon_->SetBorder(views::Border::CreateEmptyBorder(gfx::Insets(
        kHorizontalShelfVerticalPadding, kHorizontalShelfHorizontalPadding)));
  } else {
    icon_->SetBorder(views::Border::CreateEmptyBorder(gfx::Insets(
        kVerticalShelfVerticalPadding, kVerticalShelfHorizontalPadding)));
  }
}

void PaletteTray::UpdateTrayIcon() {
  gfx::VectorIconId icon = palette_tool_manager_->GetActiveTrayIcon(
      palette_tool_manager_->GetActiveTool(ash::PaletteGroup::MODE));
  icon_->SetImage(CreateVectorIcon(icon, kShelfIconColor));
}

void PaletteTray::UpdateIconVisibility() {
  if (!IsPaletteEnabled())
    return;

  SessionStateDelegate* session_state_delegate =
      WmShell::Get()->GetSessionStateDelegate();

  SetVisible(!session_state_delegate->IsScreenLocked() &&
             session_state_delegate->GetSessionState() ==
                 SessionStateDelegate::SESSION_STATE_ACTIVE &&
             WmShell::Get()->system_tray_delegate()->GetUserLoginStatus() !=
                 LoginStatus::KIOSK_APP);
}

}  // namespace ash
