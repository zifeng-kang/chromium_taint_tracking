// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/plugins/chrome_plugin_service_filter.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/plugins/plugin_finder.h"
#include "chrome/browser/plugins/plugin_metadata.h"
#include "chrome/browser/plugins/plugin_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/render_messages.h"
#include "chrome/grit/generated_resources.h"
#include "components/content_settings/content/common/content_settings_messages.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_constants.h"
#include "grit/components_strings.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/window_open_disposition.h"

using base::UserMetricsAction;
using content::BrowserThread;
using content::PluginService;

namespace {

void AuthorizeRenderer(content::RenderFrameHost* render_frame_host) {
  ChromePluginServiceFilter::GetInstance()->AuthorizePlugin(
      render_frame_host->GetProcess()->GetID(), base::FilePath());
}

}  // namespace

// static
ChromePluginServiceFilter* ChromePluginServiceFilter::GetInstance() {
  return base::Singleton<ChromePluginServiceFilter>::get();
}

void ChromePluginServiceFilter::RegisterResourceContext(
    scoped_refptr<PluginPrefs> plugin_prefs,
    scoped_refptr<HostContentSettingsMap> host_content_settings_map,
    const void* context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  base::AutoLock lock(lock_);
  plugin_prefs_[context] = std::move(plugin_prefs);
  host_content_settings_maps_[context] = std::move(host_content_settings_map);
}

void ChromePluginServiceFilter::UnregisterResourceContext(
    const void* context) {
  base::AutoLock lock(lock_);
  plugin_prefs_.erase(context);
  host_content_settings_maps_.erase(context);
}

void ChromePluginServiceFilter::OverridePluginForFrame(
    int render_process_id,
    int render_frame_id,
    const GURL& url,
    const content::WebPluginInfo& plugin) {
  base::AutoLock auto_lock(lock_);
  ProcessDetails* details = GetOrRegisterProcess(render_process_id);
  OverriddenPlugin overridden_plugin;
  overridden_plugin.render_frame_id = render_frame_id;
  overridden_plugin.url = url;
  overridden_plugin.plugin = plugin;
  details->overridden_plugins.push_back(overridden_plugin);
}

bool ChromePluginServiceFilter::IsPluginAvailable(
    int render_process_id,
    int render_frame_id,
    const void* context,
    const GURL& url,
    const GURL& policy_url,
    content::WebPluginInfo* plugin) {
  base::AutoLock auto_lock(lock_);
  const ProcessDetails* details = GetProcess(render_process_id);

  // Check whether the plugin is overridden.
  if (details) {
    for (const auto& plugin_override : details->overridden_plugins) {
      if (plugin_override.render_frame_id == render_frame_id &&
          (plugin_override.url.is_empty() || plugin_override.url == url)) {
        bool use = plugin_override.plugin.path == plugin->path;
        if (use)
          *plugin = plugin_override.plugin;
        return use;
      }
    }
  }

  // Check whether the plugin is disabled.
  ResourceContextMap::iterator prefs_it = plugin_prefs_.find(context);
  // The context might not be found because RenderFrameMessageFilter might
  // outlive the Profile (the context is unregistered during the Profile
  // destructor).
  if (prefs_it == plugin_prefs_.end())
    return false;

  if (!prefs_it->second.get()->IsPluginEnabled(*plugin))
    return false;

  // Check whether PreferHtmlOverPlugins feature is enabled.
  if (plugin->name == base::ASCIIToUTF16(content::kFlashPluginName) &&
      base::FeatureList::IsEnabled(features::kPreferHtmlOverPlugins)) {
    auto host_content_settings_map_it =
        host_content_settings_maps_.find(context);
    DCHECK(host_content_settings_map_it != host_content_settings_maps_.end());

    // TODO(trizzofo): Implement site-origin exceptions once we have the
    // top-level frame origin.
    ContentSetting content_setting =
        host_content_settings_map_it->second->GetDefaultContentSetting(
            CONTENT_SETTINGS_TYPE_PLUGINS, nullptr);
    if (content_setting == CONTENT_SETTING_BLOCK)
      return false;
  }

  return true;
}

bool ChromePluginServiceFilter::CanLoadPlugin(int render_process_id,
                                              const base::FilePath& path) {
  // The browser itself sometimes loads plugins to e.g. clear plugin data.
  // We always grant the browser permission.
  if (!render_process_id)
    return true;

  base::AutoLock auto_lock(lock_);
  const ProcessDetails* details = GetProcess(render_process_id);
  if (!details)
    return false;

  return (ContainsKey(details->authorized_plugins, path) ||
          ContainsKey(details->authorized_plugins, base::FilePath()));
}

void ChromePluginServiceFilter::AuthorizePlugin(
    int render_process_id,
    const base::FilePath& plugin_path) {
  base::AutoLock auto_lock(lock_);
  ProcessDetails* details = GetOrRegisterProcess(render_process_id);
  details->authorized_plugins.insert(plugin_path);
}

void ChromePluginServiceFilter::AuthorizeAllPlugins(
    content::WebContents* web_contents,
    bool load_blocked,
    const std::string& identifier) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  web_contents->ForEachFrame(base::Bind(&AuthorizeRenderer));
  if (load_blocked) {
    web_contents->SendToAllFrames(new ChromeViewMsg_LoadBlockedPlugins(
        MSG_ROUTING_NONE, identifier));
  }
}

ChromePluginServiceFilter::ChromePluginServiceFilter() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_CLOSED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_PLUGIN_ENABLE_STATUS_CHANGED,
                 content::NotificationService::AllSources());
}

ChromePluginServiceFilter::~ChromePluginServiceFilter() {
}

void ChromePluginServiceFilter::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  switch (type) {
    case content::NOTIFICATION_RENDERER_PROCESS_CLOSED: {
      int render_process_id =
          content::Source<content::RenderProcessHost>(source).ptr()->GetID();

      base::AutoLock auto_lock(lock_);
      plugin_details_.erase(render_process_id);
      break;
    }
    case chrome::NOTIFICATION_PLUGIN_ENABLE_STATUS_CHANGED: {
      Profile* profile = content::Source<Profile>(source).ptr();
      PluginService::GetInstance()->PurgePluginListCache(profile, false);
      if (profile && profile->HasOffTheRecordProfile()) {
        PluginService::GetInstance()->PurgePluginListCache(
            profile->GetOffTheRecordProfile(), false);
      }
      break;
    }
    default: {
      NOTREACHED();
    }
  }
}

ChromePluginServiceFilter::ProcessDetails*
ChromePluginServiceFilter::GetOrRegisterProcess(
    int render_process_id) {
  return &plugin_details_[render_process_id];
}

const ChromePluginServiceFilter::ProcessDetails*
ChromePluginServiceFilter::GetProcess(
    int render_process_id) const {
  std::map<int, ProcessDetails>::const_iterator it =
      plugin_details_.find(render_process_id);
  if (it == plugin_details_.end())
    return NULL;
  return &it->second;
}

ChromePluginServiceFilter::OverriddenPlugin::OverriddenPlugin()
    : render_frame_id(MSG_ROUTING_NONE) {
}

ChromePluginServiceFilter::OverriddenPlugin::~OverriddenPlugin() {
}

ChromePluginServiceFilter::ProcessDetails::ProcessDetails() {
}

ChromePluginServiceFilter::ProcessDetails::ProcessDetails(
    const ProcessDetails& other) = default;

ChromePluginServiceFilter::ProcessDetails::~ProcessDetails() {
}
