// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_COMMON_GPU_SERVICE_H_
#define SERVICES_UI_COMMON_GPU_SERVICE_H_

#include <stdint.h>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "services/ui/common/mojo_gpu_memory_buffer_manager.h"
#include "services/ui/common/mus_common_export.h"
#include "services/ui/public/interfaces/gpu_service.mojom.h"

namespace shell {
class Connector;
}

namespace ui {

class MUS_COMMON_EXPORT GpuService : public gpu::GpuChannelHostFactory {
 public:
  ~GpuService() override;

  void EstablishGpuChannel(const base::Closure& callback);
  scoped_refptr<gpu::GpuChannelHost> EstablishGpuChannelSync();
  scoped_refptr<gpu::GpuChannelHost> GetGpuChannel();
  gpu::GpuMemoryBufferManager* gpu_memory_buffer_manager() const {
    return gpu_memory_buffer_manager_.get();
  }

  // The GpuService has to be initialized in the main thread before establishing
  // the gpu channel.
  static std::unique_ptr<GpuService> Initialize(shell::Connector* connector);
  static GpuService* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<GpuService>;

  explicit GpuService(shell::Connector* connector);

  scoped_refptr<gpu::GpuChannelHost> GetGpuChannelLocked();
  void EstablishGpuChannelOnMainThread();
  void EstablishGpuChannelOnMainThreadSyncLocked();
  void EstablishGpuChannelOnMainThreadDone(
      bool locked,
      int client_id,
      mojom::ChannelHandlePtr channel_handle,
      mojom::GpuInfoPtr gpu_info);

  // gpu::GpuChannelHostFactory overrides:
  bool IsMainThread() override;
  scoped_refptr<base::SingleThreadTaskRunner> GetIOThreadTaskRunner() override;
  std::unique_ptr<base::SharedMemory> AllocateSharedMemory(
      size_t size) override;

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  shell::Connector* connector_;
  base::WaitableEvent shutdown_event_;
  base::Thread io_thread_;
  std::unique_ptr<MojoGpuMemoryBufferManager> gpu_memory_buffer_manager_;

  // Lock for |gpu_channel_|, |establish_callbacks_| & |is_establishing_|.
  base::Lock lock_;
  bool is_establishing_;
  ui::mojom::GpuServicePtr gpu_service_;
  scoped_refptr<gpu::GpuChannelHost> gpu_channel_;
  std::vector<base::Closure> establish_callbacks_;
  base::ConditionVariable establishing_condition_;

  DISALLOW_COPY_AND_ASSIGN(GpuService);
};

}  // namespace ui

#endif  // COMPONENTS_MUS_PUBLIC_CPP_LIB_GPU_SERVICE_CONNECTION_H_
