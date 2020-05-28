// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_MUS_WINDOW_MANAGER_CONNECTION_H_
#define UI_VIEWS_MUS_WINDOW_MANAGER_CONNECTION_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/observer_list.h"
#include "services/shell/public/cpp/identity.h"
#include "services/ui/public/cpp/window_tree_client_delegate.h"
#include "ui/base/dragdrop/os_exchange_data_provider_factory.h"
#include "ui/views/mus/mus_export.h"
#include "ui/views/mus/screen_mus_delegate.h"
#include "ui/views/widget/widget.h"

namespace shell {
class Connector;
}

namespace ui {
class GpuService;
}

namespace views {
class ClipboardMus;
class NativeWidget;
class PointerWatcher;
class TouchEventWatcher;
class ScreenMus;
namespace internal {
class NativeWidgetDelegate;
}

// Provides configuration to mus in views. This consists of the following:
// . Provides a Screen implementation backed by mus.
// . Provides a Clipboard implementation backed by mus.
// . Creates and owns a WindowTreeClient.
// . Registers itself as the factory for creating NativeWidgets so that a
//   NativeWidgetMus is created.
// WindowManagerConnection is a singleton and should be created early on.
//
// TODO(sky): this name is now totally confusing. Come up with a better one.
class VIEWS_MUS_EXPORT WindowManagerConnection
    : public NON_EXPORTED_BASE(ui::WindowTreeClientDelegate),
      public ScreenMusDelegate,
      public ui::OSExchangeDataProviderFactory::Factory {
 public:
  ~WindowManagerConnection() override;

  static std::unique_ptr<WindowManagerConnection> Create(
      shell::Connector* connector,
      const shell::Identity& identity);
  static WindowManagerConnection* Get();
  static bool Exists();

  shell::Connector* connector() { return connector_; }

  ui::Window* NewWindow(
      const std::map<std::string, std::vector<uint8_t>>& properties);

  NativeWidget* CreateNativeWidgetMus(
      const std::map<std::string, std::vector<uint8_t>>& properties,
      const Widget::InitParams& init_params,
      internal::NativeWidgetDelegate* delegate);

  void AddPointerWatcher(PointerWatcher* watcher);
  void RemovePointerWatcher(PointerWatcher* watcher);

  void AddTouchEventWatcher(TouchEventWatcher* watcher);
  void RemoveTouchEventWatcher(TouchEventWatcher* watcher);

  const std::set<ui::Window*>& GetRoots() const;

 private:
  friend class WindowManagerConnectionTest;

  WindowManagerConnection(shell::Connector* connector,
                          const shell::Identity& identity);

  // Returns true if there is one or more watchers for this client.
  bool HasPointerWatcher();
  bool HasTouchEventWatcher();

  // ui::WindowTreeClientDelegate:
  void OnEmbed(ui::Window* root) override;
  void OnDidDestroyClient(ui::WindowTreeClient* client) override;
  void OnEventObserved(const ui::Event& event, ui::Window* target) override;

  // ScreenMusDelegate:
  void OnWindowManagerFrameValuesChanged() override;
  gfx::Point GetCursorScreenPoint() override;

  // ui:OSExchangeDataProviderFactory::Factory:
  std::unique_ptr<OSExchangeData::Provider> BuildProvider() override;

  shell::Connector* connector_;
  shell::Identity identity_;
  std::unique_ptr<ScreenMus> screen_;
  std::unique_ptr<ui::WindowTreeClient> client_;
  std::unique_ptr<ui::GpuService> gpu_service_;
  // Must be empty on destruction.
  base::ObserverList<PointerWatcher, true> pointer_watchers_;
  base::ObserverList<TouchEventWatcher, true> touch_event_watchers_;

  DISALLOW_COPY_AND_ASSIGN(WindowManagerConnection);
};

}  // namespace views

#endif  // UI_VIEWS_MUS_WINDOW_MANAGER_CONNECTION_H_
