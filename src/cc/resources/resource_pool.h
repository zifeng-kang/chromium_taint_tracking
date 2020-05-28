// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_RESOURCES_RESOURCE_POOL_H_
#define CC_RESOURCES_RESOURCE_POOL_H_

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <map>
#include <memory>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/trace_event/memory_dump_provider.h"
#include "cc/base/cc_export.h"
#include "cc/output/renderer.h"
#include "cc/resources/resource.h"
#include "cc/resources/resource_format.h"
#include "cc/resources/scoped_resource.h"

namespace cc {

class CC_EXPORT ResourcePool : public base::trace_event::MemoryDumpProvider {
 public:
  static std::unique_ptr<ResourcePool> CreateForGpuMemoryBufferResources(
      ResourceProvider* resource_provider,
      base::SingleThreadTaskRunner* task_runner,
      gfx::BufferUsage usage) {
    std::unique_ptr<ResourcePool> pool(
        new ResourcePool(resource_provider, task_runner, true));
    pool->usage_ = usage;
    return pool;
  }

  static std::unique_ptr<ResourcePool> Create(
      ResourceProvider* resource_provider,
      base::SingleThreadTaskRunner* task_runner) {
    return base::WrapUnique(
        new ResourcePool(resource_provider, task_runner, false));
  }

  ~ResourcePool() override;

  Resource* AcquireResource(const gfx::Size& size, ResourceFormat format);

  // Tries to acquire the resource with |previous_content_id| for us in partial
  // raster. If successful, this function will retun the invalidated rect which
  // must be re-rastered in |total_invalidated_rect|.
  Resource* TryAcquireResourceForPartialRaster(
      uint64_t new_content_id,
      const gfx::Rect& new_invalidated_rect,
      uint64_t previous_content_id,
      gfx::Rect* total_invalidated_rect);

  // Called when a resource's content has been fully replaced (and is completely
  // valid). Updates the resource's content ID to its new value.
  void OnContentReplaced(ResourceId resource_id, uint64_t content_id);
  void ReleaseResource(Resource* resource);

  void SetResourceUsageLimits(size_t max_memory_usage_bytes,
                              size_t max_resource_count);

  void ReduceResourceUsage();
  void CheckBusyResources();

  size_t memory_usage_bytes() const { return in_use_memory_usage_bytes_; }
  size_t resource_count() const { return in_use_resources_.size(); }

  // Overridden from base::trace_event::MemoryDumpProvider:
  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override;

  size_t GetTotalMemoryUsageForTesting() const {
    return total_memory_usage_bytes_;
  }
  size_t GetTotalResourceCountForTesting() const {
    return total_resource_count_;
  }
  size_t GetBusyResourceCountForTesting() const {
    return busy_resources_.size();
  }
  void SetResourceExpirationDelayForTesting(base::TimeDelta delay) {
    resource_expiration_delay_ = delay;
  }

 protected:
  ResourcePool(ResourceProvider* resource_provider,
               base::SingleThreadTaskRunner* task_runner,
               bool use_gpu_memory_buffers);

  bool ResourceUsageTooHigh();

 private:
  class PoolResource : public ScopedResource {
   public:
    static std::unique_ptr<PoolResource> Create(
        ResourceProvider* resource_provider) {
      return base::WrapUnique(new PoolResource(resource_provider));
    }
    void OnMemoryDump(base::trace_event::ProcessMemoryDump* pmd,
                      const ResourceProvider* resource_provider,
                      bool is_free) const;

    uint64_t content_id() const { return content_id_; }
    void set_content_id(uint64_t content_id) { content_id_ = content_id; }

    base::TimeTicks last_usage() const { return last_usage_; }
    void set_last_usage(base::TimeTicks time) { last_usage_ = time; }

    gfx::Rect invalidated_rect() const { return invalidated_rect_; }
    void set_invalidated_rect(const gfx::Rect& invalidated_rect) {
      invalidated_rect_ = invalidated_rect;
    }

   private:
    explicit PoolResource(ResourceProvider* resource_provider)
        : ScopedResource(resource_provider), content_id_(0) {}
    uint64_t content_id_;
    base::TimeTicks last_usage_;
    gfx::Rect invalidated_rect_;
  };

  void DidFinishUsingResource(std::unique_ptr<PoolResource> resource);
  void DeleteResource(std::unique_ptr<PoolResource> resource);
  static void UpdateResourceContentIdAndInvalidation(
      PoolResource* resource,
      uint64_t new_content_id,
      const gfx::Rect& new_invalidated_rect);

  // Functions which manage periodic eviction of expired resources.
  void ScheduleEvictExpiredResourcesIn(base::TimeDelta time_from_now);
  void EvictExpiredResources();
  void EvictResourcesNotUsedSince(base::TimeTicks time_limit);
  bool HasEvictableResources() const;
  base::TimeTicks GetUsageTimeForLRUResource() const;

  ResourceProvider* resource_provider_;
  bool use_gpu_memory_buffers_;
  gfx::BufferUsage usage_;
  size_t max_memory_usage_bytes_;
  size_t max_resource_count_;
  size_t in_use_memory_usage_bytes_;
  size_t total_memory_usage_bytes_;
  size_t total_resource_count_;

  // Holds most recently used resources at the front of the queue.
  using ResourceDeque = std::deque<std::unique_ptr<PoolResource>>;
  ResourceDeque unused_resources_;
  ResourceDeque busy_resources_;

  using InUseResourceMap = std::map<ResourceId, std::unique_ptr<PoolResource>>;
  InUseResourceMap in_use_resources_;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  bool evict_expired_resources_pending_;
  base::TimeDelta resource_expiration_delay_;

  base::WeakPtrFactory<ResourcePool> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ResourcePool);
};

}  // namespace cc

#endif  // CC_RESOURCES_RESOURCE_POOL_H_