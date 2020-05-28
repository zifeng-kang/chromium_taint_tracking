// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/engine/mojo/blob_channel_service.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/memory/ptr_util.h"
#include "blimp/net/blob_channel/blob_channel_sender.h"
#include "mojo/public/cpp/system/buffer.h"

namespace blimp {
namespace engine {

BlobChannelService::BlobChannelService(BlobChannelSender* blob_channel_sender,
                                       mojom::BlobChannelRequest request)
    : binding_(this, std::move(request)),
      blob_channel_sender_(blob_channel_sender) {
  DCHECK(blob_channel_sender_);
}

BlobChannelService::~BlobChannelService() {}

void BlobChannelService::GetCachedBlobIds(
    const BlobChannelService::GetCachedBlobIdsCallback& response_callback) {
  VLOG(1) << "BlobChannel::GetCachedBlobIds called.";
  std::unordered_map<std::string, bool> cache_state;
  for (const auto& next_entry : blob_channel_sender_->GetCachedBlobIds()) {
    cache_state[next_entry.id] = next_entry.was_delivered;
  }
  response_callback.Run(std::move(cache_state));
}

void BlobChannelService::PutBlob(const std::string& id,
                                 mojo::ScopedSharedBufferHandle data,
                                 uint32_t size) {
  // Map |data| into the address space and copy out its contents.
  if (!data.is_valid()) {
    LOG(ERROR) << "Invalid data handle received from renderer process.";
    return;
  }

  if (size > kMaxBlobSizeBytes) {
    LOG(ERROR) << "Blob size too big: " << size;
    return;
  }

  mojo::ScopedSharedBufferMapping mapping = data->Map(size);
  CHECK(mapping) << "Failed to mmap region of " << size << " bytes.";

  scoped_refptr<BlobData> new_blob(new BlobData);
  new_blob->data.assign(reinterpret_cast<const char*>(mapping.get()), size);
  blob_channel_sender_->PutBlob(id, std::move(new_blob));
}

void BlobChannelService::DeliverBlob(const std::string& id) {
  blob_channel_sender_->DeliverBlob(id);
}

// static
void BlobChannelService::Create(
    BlobChannelSender* blob_channel_sender,
    mojo::InterfaceRequest<mojom::BlobChannel> request) {
  // Object lifetime is managed by BlobChannelService's StrongBinding
  // |binding_|.
  new BlobChannelService(blob_channel_sender, std::move(request));
}

}  // namespace engine
}  // namespace blimp
