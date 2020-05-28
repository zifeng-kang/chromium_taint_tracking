// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/chromeos/palette/palette_tool.h"

#include "ash/common/system/chromeos/palette/palette_tool_manager.h"
#include "ui/gfx/vector_icons_public.h"

namespace ash {

// static
void PaletteTool::RegisterToolInstances(PaletteToolManager* tool_manager) {}

PaletteTool::PaletteTool(Delegate* delegate) : delegate_(delegate) {}

PaletteTool::~PaletteTool() {}

void PaletteTool::OnEnable() {
  enabled_ = true;
}

void PaletteTool::OnDisable() {
  enabled_ = false;
}

gfx::VectorIconId PaletteTool::GetActiveTrayIcon() {
  return gfx::VectorIconId::VECTOR_ICON_NONE;
}

}  // namespace ash
