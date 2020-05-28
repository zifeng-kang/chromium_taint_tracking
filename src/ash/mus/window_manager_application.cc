// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/mus/window_manager_application.h"

#include <utility>

#include "ash/common/material_design/material_design_controller.h"
#include "ash/mus/accelerators/accelerator_registrar_impl.h"
#include "ash/mus/root_window_controller.h"
#include "ash/mus/shelf_layout_impl.h"
#include "ash/mus/user_window_controller_impl.h"
#include "ash/mus/window_manager.h"
#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "services/shell/public/cpp/connection.h"
#include "services/shell/public/cpp/connector.h"
#include "services/tracing/public/cpp/provider.h"
#include "services/ui/common/event_matcher_util.h"
#include "services/ui/common/gpu_service.h"
#include "services/ui/public/cpp/window.h"
#include "services/ui/public/cpp/window_tree_client.h"
#include "ui/events/event.h"
#include "ui/message_center/message_center.h"
#include "ui/views/mus/aura_init.h"

#if defined(OS_CHROMEOS)
#include "ash/common/system/chromeos/power/power_status.h"
#include "chromeos/audio/cras_audio_handler.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "device/bluetooth/dbus/bluez_dbus_manager.h"  // nogncheck
#endif

namespace ash {
namespace mus {
namespace {

void InitializeComponents() {
  message_center::MessageCenter::Initialize();
#if defined(OS_CHROMEOS)
  // Must occur after mojo::ApplicationRunner has initialized AtExitManager, but
  // before WindowManager::Init().
  chromeos::DBusThreadManager::Initialize();
  bluez::BluezDBusManager::Initialize(
      chromeos::DBusThreadManager::Get()->GetSystemBus(),
      chromeos::DBusThreadManager::Get()->IsUsingStub(
          chromeos::DBusClientBundle::BLUETOOTH));
  // TODO(jamescook): Initialize real audio handler.
  chromeos::CrasAudioHandler::InitializeForTesting();
  PowerStatus::Initialize();
#endif
}

void ShutdownComponents() {
#if defined(OS_CHROMEOS)
  PowerStatus::Shutdown();
  chromeos::CrasAudioHandler::Shutdown();
  bluez::BluezDBusManager::Shutdown();
  chromeos::DBusThreadManager::Shutdown();
#endif
  message_center::MessageCenter::Shutdown();
}

}  // namespace

WindowManagerApplication::WindowManagerApplication()
    : screenlock_state_listener_binding_(this) {}

WindowManagerApplication::~WindowManagerApplication() {
  // AcceleratorRegistrarImpl removes an observer in its destructor. Destroy
  // it early on.
  std::set<AcceleratorRegistrarImpl*> accelerator_registrars(
      accelerator_registrars_);
  for (AcceleratorRegistrarImpl* registrar : accelerator_registrars)
    registrar->Destroy();

  // Destroy the WindowManager while still valid. This way we ensure
  // OnWillDestroyRootWindowController() is called (if it hasn't been already).
  window_manager_.reset();
  gpu_service_.reset();
  ShutdownComponents();
}

void WindowManagerApplication::OnAcceleratorRegistrarDestroyed(
    AcceleratorRegistrarImpl* registrar) {
  accelerator_registrars_.erase(registrar);
}

void WindowManagerApplication::InitWindowManager(
    ui::WindowTreeClient* window_tree_client) {
  InitializeComponents();

  window_manager_->Init(window_tree_client);
  window_manager_->AddObserver(this);
}

void WindowManagerApplication::OnStart(const shell::Identity& identity) {
  gpu_service_ = ui::GpuService::Initialize(connector());
  window_manager_.reset(new WindowManager(connector()));

  aura_init_.reset(new views::AuraInit(connector(), "ash_mus_resources.pak"));
  MaterialDesignController::Initialize();

  tracing_.Initialize(connector(), identity.name());

  ui::WindowTreeClient* window_tree_client = new ui::WindowTreeClient(
      window_manager_.get(), window_manager_.get(), nullptr);
  window_tree_client->ConnectAsWindowManager(connector());

  InitWindowManager(window_tree_client);
}

bool WindowManagerApplication::OnConnect(shell::Connection* connection) {
  connection->AddInterface<mojom::ShelfLayout>(this);
  connection->AddInterface<mojom::UserWindowController>(this);
  connection->AddInterface<ui::mojom::AcceleratorRegistrar>(this);
  if (connection->GetRemoteIdentity().name() == "mojo:mash_session") {
    connector()->ConnectToInterface(connection->GetRemoteIdentity(), &session_);
    session_->AddScreenlockStateListener(
        screenlock_state_listener_binding_.CreateInterfacePtrAndBind());
  }
  return true;
}

void WindowManagerApplication::Create(
    const shell::Identity& remote_identity,
    mojo::InterfaceRequest<mojom::ShelfLayout> request) {
  // TODO(msw): Handle multiple shelves (one per display).
  if (!window_manager_->GetRootWindowControllers().empty()) {
    shelf_layout_bindings_.AddBinding(shelf_layout_.get(), std::move(request));
  } else {
    shelf_layout_requests_.push_back(std::move(request));
  }
}

void WindowManagerApplication::Create(
    const shell::Identity& remote_identity,
    mojo::InterfaceRequest<mojom::UserWindowController> request) {
  if (!window_manager_->GetRootWindowControllers().empty()) {
    user_window_controller_bindings_.AddBinding(user_window_controller_.get(),
                                                std::move(request));
  } else {
    user_window_controller_requests_.push_back(std::move(request));
  }
}

void WindowManagerApplication::Create(
    const shell::Identity& remote_identity,
    mojo::InterfaceRequest<ui::mojom::AcceleratorRegistrar> request) {
  if (!window_manager_->window_manager_client())
    return;  // Can happen during shutdown.

  uint16_t accelerator_namespace_id;
  if (!window_manager_->GetNextAcceleratorNamespaceId(
          &accelerator_namespace_id)) {
    DVLOG(1) << "Max number of accelerators registered, ignoring request.";
    // All ids are used. Normally shouldn't happen, so we close the connection.
    return;
  }
  accelerator_registrars_.insert(new AcceleratorRegistrarImpl(
      window_manager_.get(), accelerator_namespace_id, std::move(request),
      base::Bind(&WindowManagerApplication::OnAcceleratorRegistrarDestroyed,
                 base::Unretained(this))));
}

void WindowManagerApplication::ScreenlockStateChanged(bool locked) {
  window_manager_->SetScreenLocked(locked);
}

void WindowManagerApplication::OnRootWindowControllerAdded(
    RootWindowController* controller) {
  if (user_window_controller_)
    return;

  // TODO(sky): |shelf_layout_| and |user_window_controller_| should really
  // be owned by WindowManager and/or RootWindowController. But this code is
  // temporary while migrating away from sysui.

  shelf_layout_.reset(new ShelfLayoutImpl);
  user_window_controller_.reset(new UserWindowControllerImpl());

  // TODO(msw): figure out if this should be per display, or global.
  user_window_controller_->Initialize(controller);
  for (auto& request : user_window_controller_requests_)
    user_window_controller_bindings_.AddBinding(user_window_controller_.get(),
                                                std::move(request));
  user_window_controller_requests_.clear();

  // TODO(msw): figure out if this should be per display, or global.
  shelf_layout_->Initialize(controller);
  for (auto& request : shelf_layout_requests_)
    shelf_layout_bindings_.AddBinding(shelf_layout_.get(), std::move(request));
  shelf_layout_requests_.clear();
}

void WindowManagerApplication::OnWillDestroyRootWindowController(
    RootWindowController* controller) {
  // TODO(msw): this isn't right, ownership should belong in WindowManager
  // and/or RootWindowController. But this is temporary until we get rid of
  // sysui.
  shelf_layout_.reset();
  user_window_controller_.reset();
}

}  // namespace mus
}  // namespace ash
