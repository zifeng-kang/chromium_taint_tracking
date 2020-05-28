// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/shell/public/cpp/interface_registry.h"

#include "services/shell/public/cpp/connection.h"

namespace shell {

InterfaceRegistry::InterfaceRegistry(Connection* connection)
    : binding_(this), connection_(connection), weak_factory_(this) {}
InterfaceRegistry::~InterfaceRegistry() {}

void InterfaceRegistry::Bind(
    mojom::InterfaceProviderRequest local_interfaces_request) {
  DCHECK(!binding_.is_bound());
  binding_.Bind(std::move(local_interfaces_request));
}

base::WeakPtr<InterfaceRegistry> InterfaceRegistry::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

bool InterfaceRegistry::AddInterface(
    const std::string& name,
    const base::Callback<void(mojo::ScopedMessagePipeHandle)>& callback,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner) {
  return SetInterfaceBinderForName(
      base::WrapUnique(
          new internal::GenericCallbackBinder(callback, task_runner)),
      name);
}

void InterfaceRegistry::RemoveInterface(const std::string& name) {
  auto it = name_to_binder_.find(name);
  if (it != name_to_binder_.end())
    name_to_binder_.erase(it);
}

void InterfaceRegistry::PauseBinding() {
  DCHECK(!is_paused_);
  is_paused_ = true;
}

void InterfaceRegistry::ResumeBinding() {
  DCHECK(is_paused_);
  is_paused_ = false;

  while (!pending_interface_requests_.empty()) {
    auto& request = pending_interface_requests_.front();
    GetInterface(request.first, std::move(request.second));
    pending_interface_requests_.pop();
  }
}

void InterfaceRegistry::GetInterfaceNames(
    std::set<std::string>* interface_names) {
  DCHECK(interface_names);
  for (auto& entry : name_to_binder_)
    interface_names->insert(entry.first);
}

void InterfaceRegistry::SetConnectionLostClosure(
    const base::Closure& connection_lost_closure) {
  binding_.set_connection_error_handler(connection_lost_closure);
}

// mojom::InterfaceProvider:
void InterfaceRegistry::GetInterface(const mojo::String& interface_name,
                                     mojo::ScopedMessagePipeHandle handle) {
  if (is_paused_) {
    pending_interface_requests_.emplace(interface_name, std::move(handle));
    return;
  }

  auto iter = name_to_binder_.find(interface_name);
  if (iter != name_to_binder_.end()) {
    Identity remote_identity =
        connection_ ? connection_->GetRemoteIdentity() : Identity();
    iter->second->BindInterface(remote_identity,
                                interface_name,
                                std::move(handle));
  } else if (connection_ && !connection_->AllowsInterface(interface_name)) {
    LOG(ERROR) << "Capability spec prevented service: "
               << connection_->GetRemoteIdentity().name()
               << " from binding interface: " << interface_name;
  } else if (!default_binder_.is_null()) {
    default_binder_.Run(interface_name, std::move(handle));
  } else {
    LOG(ERROR) << "Failed to locate a binder for interface: " << interface_name;
  }
}

bool InterfaceRegistry::SetInterfaceBinderForName(
    std::unique_ptr<InterfaceBinder> binder,
    const std::string& interface_name) {
  if (!connection_ ||
      (connection_ && connection_->AllowsInterface(interface_name))) {
    RemoveInterface(interface_name);
    name_to_binder_[interface_name] = std::move(binder);
    return true;
  }
  return false;
}

}  // namespace shell
