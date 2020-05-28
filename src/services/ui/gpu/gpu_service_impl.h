// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_GPU_GPU_SERVICE_IMPL_H_
#define SERVICES_UI_GPU_GPU_SERVICE_IMPL_H_

#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "services/ui/public/interfaces/gpu_memory_buffer.mojom.h"
#include "services/ui/public/interfaces/gpu_service.mojom.h"

namespace ui {

class GpuServiceImpl : public mojom::GpuService {
 public:
  GpuServiceImpl(mojo::InterfaceRequest<mojom::GpuService> request);
  ~GpuServiceImpl() override;

  // mojom::GpuService overrides:
  void EstablishGpuChannel(
      const mojom::GpuService::EstablishGpuChannelCallback& callback) override;

  void CreateGpuMemoryBuffer(
      mojom::GpuMemoryBufferIdPtr id,
      const gfx::Size& size,
      mojom::BufferFormat format,
      mojom::BufferUsage usage,
      uint64_t surface_id,
      const mojom::GpuService::CreateGpuMemoryBufferCallback& callback)
      override;

  void DestroyGpuMemoryBuffer(mojom::GpuMemoryBufferIdPtr id,
                              const gpu::SyncToken& sync_token) override;

 private:
  mojo::StrongBinding<GpuService> binding_;

  DISALLOW_COPY_AND_ASSIGN(GpuServiceImpl);
};

}  // namespace ui

#endif  // SERVICES_UI_GPU_GPU_SERVICE_IMPL_H_
