// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/public/cpp/context_provider.h"

#include <stdint.h>

#include "base/logging.h"
#include "services/ui/public/cpp/gles2_context.h"

namespace ui {

ContextProvider::ContextProvider() {}

bool ContextProvider::BindToCurrentThread() {
  context_ = GLES2Context::CreateOffscreenContext(std::vector<int32_t>());
  return !!context_;
}

gpu::gles2::GLES2Interface* ContextProvider::ContextGL() {
  return context_->interface();
}

gpu::ContextSupport* ContextProvider::ContextSupport() {
  if (!context_)
    return NULL;
  return context_->context_support();
}

class GrContext* ContextProvider::GrContext() {
  return NULL;
}

void ContextProvider::InvalidateGrContext(uint32_t state) {}

gpu::Capabilities ContextProvider::ContextCapabilities() {
  gpu::Capabilities capabilities;
  // Enabled the CHROMIUM_image extension to use GpuMemoryBuffers. The
  // implementation of which is used in CommandBufferDriver.
  capabilities.image = true;
  return capabilities;
}

base::Lock* ContextProvider::GetLock() {
  // This context provider is not used on multiple threads.
  NOTREACHED();
  return nullptr;
}

ContextProvider::~ContextProvider() {
}

}  // namespace ui
