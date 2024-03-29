// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/mus/window_manager_connection.h"

#include <utility>

#include "base/lazy_instance.h"
#include "base/memory/ptr_util.h"
#include "base/threading/thread_local.h"
#include "services/shell/public/cpp/connection.h"
#include "services/shell/public/cpp/connector.h"
#include "services/ui/common/gpu_service.h"
#include "services/ui/public/cpp/property_type_converters.h"
#include "services/ui/public/cpp/window.h"
#include "services/ui/public/cpp/window_property.h"
#include "services/ui/public/cpp/window_tree_client.h"
#include "services/ui/public/interfaces/event_matcher.mojom.h"
#include "services/ui/public/interfaces/window_tree.mojom.h"
#include "ui/views/mus/clipboard_mus.h"
#include "ui/views/mus/native_widget_mus.h"
#include "ui/views/mus/os_exchange_data_provider_mus.h"
#include "ui/views/mus/screen_mus.h"
#include "ui/views/pointer_watcher.h"
#include "ui/views/touch_event_watcher.h"
#include "ui/views/views_delegate.h"

namespace views {
namespace {

using WindowManagerConnectionPtr =
    base::ThreadLocalPointer<views::WindowManagerConnection>;

// Env is thread local so that aura may be used on multiple threads.
base::LazyInstance<WindowManagerConnectionPtr>::Leaky lazy_tls_ptr =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

WindowManagerConnection::~WindowManagerConnection() {
  // ~WindowTreeClient calls back to us (we're its delegate), destroy it while
  // we are still valid.
  client_.reset();
  ui::OSExchangeDataProviderFactory::SetFactory(nullptr);
  ui::Clipboard::DestroyClipboardForCurrentThread();
  gpu_service_.reset();
  lazy_tls_ptr.Pointer()->Set(nullptr);

  if (ViewsDelegate::GetInstance()) {
    ViewsDelegate::GetInstance()->set_native_widget_factory(
        ViewsDelegate::NativeWidgetFactory());
  }
}

// static
std::unique_ptr<WindowManagerConnection> WindowManagerConnection::Create(
    shell::Connector* connector,
    const shell::Identity& identity) {
  DCHECK(!lazy_tls_ptr.Pointer()->Get());
  WindowManagerConnection* connection =
      new WindowManagerConnection(connector, identity);
  DCHECK(lazy_tls_ptr.Pointer()->Get());
  return base::WrapUnique(connection);
}

// static
WindowManagerConnection* WindowManagerConnection::Get() {
  WindowManagerConnection* connection = lazy_tls_ptr.Pointer()->Get();
  DCHECK(connection);
  return connection;
}

// static
bool WindowManagerConnection::Exists() {
  return !!lazy_tls_ptr.Pointer()->Get();
}

ui::Window* WindowManagerConnection::NewWindow(
    const std::map<std::string, std::vector<uint8_t>>& properties) {
  return client_->NewTopLevelWindow(&properties);
}

NativeWidget* WindowManagerConnection::CreateNativeWidgetMus(
    const std::map<std::string, std::vector<uint8_t>>& props,
    const Widget::InitParams& init_params,
    internal::NativeWidgetDelegate* delegate) {
  // TYPE_CONTROL widgets require a NativeWidgetAura. So we let this fall
  // through, so that the default NativeWidgetPrivate::CreateNativeWidget() is
  // used instead.
  if (init_params.type == Widget::InitParams::TYPE_CONTROL)
    return nullptr;
  std::map<std::string, std::vector<uint8_t>> properties = props;
  NativeWidgetMus::ConfigurePropertiesForNewWindow(init_params, &properties);
  properties[ui::mojom::WindowManager::kAppID_Property] =
      mojo::ConvertTo<std::vector<uint8_t>>(identity_.name());
  return new NativeWidgetMus(delegate, NewWindow(properties),
                             ui::mojom::SurfaceType::DEFAULT);
}

void WindowManagerConnection::AddPointerWatcher(PointerWatcher* watcher) {
  // TODO(riajiang): Support multiple event matchers (crbug.com/627146).
  DCHECK(!HasTouchEventWatcher());
  bool had_watcher = HasPointerWatcher();
  pointer_watchers_.AddObserver(watcher);
  if (!had_watcher) {
    // Start a watcher for pointer down.
    // TODO(jamescook): Extend event observers to handle multiple event types.
    ui::mojom::EventMatcherPtr matcher = ui::mojom::EventMatcher::New();
    matcher->type_matcher = ui::mojom::EventTypeMatcher::New();
    matcher->type_matcher->type = ui::mojom::EventType::POINTER_DOWN;
    client_->SetEventObserver(std::move(matcher));
  }
}

void WindowManagerConnection::RemovePointerWatcher(PointerWatcher* watcher) {
  pointer_watchers_.RemoveObserver(watcher);
  if (!HasPointerWatcher()) {
    // Last PointerWatcher removed, stop the event observer.
    client_->SetEventObserver(nullptr);
  }
}

void WindowManagerConnection::AddTouchEventWatcher(TouchEventWatcher* watcher) {
  // TODO(riajiang): Support multiple event matchers (crbug.com/627146).
  DCHECK(!HasPointerWatcher());
  bool had_watcher = HasTouchEventWatcher();
  touch_event_watchers_.AddObserver(watcher);
  if (!had_watcher) {
    ui::mojom::EventMatcherPtr matcher = ui::mojom::EventMatcher::New();
    matcher->pointer_kind_matcher = ui::mojom::PointerKindMatcher::New();
    matcher->pointer_kind_matcher->pointer_kind = ui::mojom::PointerKind::TOUCH;
    client_->SetEventObserver(std::move(matcher));
  }
}

void WindowManagerConnection::RemoveTouchEventWatcher(
    TouchEventWatcher* watcher) {
  touch_event_watchers_.RemoveObserver(watcher);
  if (!HasTouchEventWatcher()) {
    // Last TouchEventWatcher removed, stop the event observer.
    client_->SetEventObserver(nullptr);
  }
}

const std::set<ui::Window*>& WindowManagerConnection::GetRoots() const {
  return client_->GetRoots();
}

WindowManagerConnection::WindowManagerConnection(
    shell::Connector* connector,
    const shell::Identity& identity)
    : connector_(connector), identity_(identity) {
  lazy_tls_ptr.Pointer()->Set(this);

  gpu_service_ = ui::GpuService::Initialize(connector);
  client_.reset(new ui::WindowTreeClient(this, nullptr, nullptr));
  client_->ConnectViaWindowTreeFactory(connector_);

  screen_.reset(new ScreenMus(this));
  screen_->Init(connector);

  std::unique_ptr<ClipboardMus> clipboard(new ClipboardMus);
  clipboard->Init(connector);
  ui::Clipboard::SetClipboardForCurrentThread(std::move(clipboard));

  ui::OSExchangeDataProviderFactory::SetFactory(this);

  ViewsDelegate::GetInstance()->set_native_widget_factory(base::Bind(
      &WindowManagerConnection::CreateNativeWidgetMus,
      base::Unretained(this),
      std::map<std::string, std::vector<uint8_t>>()));
}

bool WindowManagerConnection::HasPointerWatcher() {
  // Check to see if we really have any observers left. This doesn't use
  // base::ObserverList<>::might_have_observers() because that returns true
  // during iteration over the list even when the last observer is removed.
  base::ObserverList<PointerWatcher>::Iterator iterator(&pointer_watchers_);
  return !!iterator.GetNext();
}

bool WindowManagerConnection::HasTouchEventWatcher() {
  // Check to see if we really have any observers left. This doesn't use
  // base::ObserverList<>::might_have_observers() because that returns true
  // during iteration over the list even when the last observer is removed.
  base::ObserverList<TouchEventWatcher>::Iterator iterator(
      &touch_event_watchers_);
  return !!iterator.GetNext();
}

void WindowManagerConnection::OnEmbed(ui::Window* root) {}

void WindowManagerConnection::OnDidDestroyClient(ui::WindowTreeClient* client) {
  if (client_.get() == client) {
    client_.release();
  } else {
    DCHECK(!client_);
  }
}

void WindowManagerConnection::OnEventObserved(const ui::Event& event,
                                              ui::Window* target) {
  if (!event.IsLocatedEvent())
    return;
  Widget* target_widget = nullptr;
  if (target) {
    ui::Window* root = target->GetRoot();
    target_widget = NativeWidgetMus::GetWidgetForWindow(root);
  }

  // The mojo input events type converter uses the event root_location field
  // to store screen coordinates. Screen coordinates really should be returned
  // separately. See http://crbug.com/608547
  gfx::Point location_in_screen = event.AsLocatedEvent()->root_location();
  if (HasPointerWatcher()) {
    if (event.type() == ui::ET_MOUSE_PRESSED) {
      FOR_EACH_OBSERVER(PointerWatcher, pointer_watchers_,
                        OnMousePressed(*event.AsMouseEvent(),
                                       location_in_screen, target_widget));
    } else if (event.type() == ui::ET_TOUCH_PRESSED) {
      FOR_EACH_OBSERVER(PointerWatcher, pointer_watchers_,
                        OnTouchPressed(*event.AsTouchEvent(),
                                       location_in_screen, target_widget));
    }
  } else if (HasTouchEventWatcher()) {
    if (event.IsTouchEvent() || event.IsTouchPointerEvent()) {
      FOR_EACH_OBSERVER(
          TouchEventWatcher, touch_event_watchers_,
          OnTouchEventObserved(*event.AsLocatedEvent(), target_widget));
    }
  }
}

void WindowManagerConnection::OnWindowManagerFrameValuesChanged() {
  if (client_)
    NativeWidgetMus::NotifyFrameChanged(client_.get());
}

gfx::Point WindowManagerConnection::GetCursorScreenPoint() {
  return client_->GetCursorScreenPoint();
}

std::unique_ptr<OSExchangeData::Provider>
WindowManagerConnection::BuildProvider() {
  return base::MakeUnique<OSExchangeDataProviderMus>();
}

}  // namespace views
