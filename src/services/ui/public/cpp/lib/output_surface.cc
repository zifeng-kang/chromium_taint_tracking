// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/public/cpp/output_surface.h"

#include "base/bind.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/output_surface_client.h"
#include "services/ui/public/cpp/window_surface.h"

namespace ui {

OutputSurface::OutputSurface(
    const scoped_refptr<cc::ContextProvider>& context_provider,
    std::unique_ptr<ui::WindowSurface> surface)
    : cc::OutputSurface(context_provider, nullptr, nullptr),
      surface_(std::move(surface)) {
  capabilities_.delegated_rendering = true;
}

OutputSurface::~OutputSurface() {}

bool OutputSurface::BindToClient(cc::OutputSurfaceClient* client) {
  surface_->BindToThread();
  surface_->set_client(this);
  return cc::OutputSurface::BindToClient(client);
}

void OutputSurface::DetachFromClient() {
  surface_.reset();
  cc::OutputSurface::DetachFromClient();
}

void OutputSurface::BindFramebuffer() {
  // This is a delegating output surface, no framebuffer/direct drawing support.
  NOTREACHED();
}

uint32_t OutputSurface::GetFramebufferCopyTextureFormat() {
  // This is a delegating output surface, no framebuffer/direct drawing support.
  NOTREACHED();
  return 0;
}

void OutputSurface::SwapBuffers(cc::CompositorFrame frame) {
  // OutputSurface owns WindowSurface, and so if OutputSurface is
  // destroyed then SubmitCompositorFrame's callback will never get called.
  // Thus, base::Unretained is safe here.
  surface_->SubmitCompositorFrame(
      std::move(frame),
      base::Bind(&OutputSurface::SwapBuffersComplete, base::Unretained(this)));
}

void OutputSurface::OnResourcesReturned(
    ui::WindowSurface* surface,
    mojo::Array<cc::ReturnedResource> resources) {
  ReclaimResources(resources.To<cc::ReturnedResourceArray>());
}

void OutputSurface::SwapBuffersComplete() {
  client_->DidSwapBuffersComplete();
}

}  // namespace ui
