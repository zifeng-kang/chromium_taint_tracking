// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/desktop_engagement/chrome_visibility_observer.h"

#include "base/memory/singleton.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/metrics/desktop_engagement/desktop_engagement_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "components/variations/variations_associated_data.h"

namespace metrics {

ChromeVisibilityObserver::ChromeVisibilityObserver() : weak_factory_(this) {
  BrowserList::AddObserver(this);
  InitVisibilityGapTimeout();
}

ChromeVisibilityObserver::~ChromeVisibilityObserver() {
  BrowserList::RemoveObserver(this);
}

void ChromeVisibilityObserver::SendVisibilityChangeEvent(bool active) {
  DesktopEngagementService::Get()->OnVisibilityChanged(active);
}

void ChromeVisibilityObserver::CancelVisibilityChange() {
  weak_factory_.InvalidateWeakPtrs();
}

void ChromeVisibilityObserver::OnBrowserSetLastActive(Browser* browser) {
  if (weak_factory_.HasWeakPtrs())
    CancelVisibilityChange();
  else
    SendVisibilityChangeEvent(true);
}

void ChromeVisibilityObserver::OnBrowserNoLongerActive(Browser* browser) {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ChromeVisibilityObserver::SendVisibilityChangeEvent,
                 weak_factory_.GetWeakPtr(), false),
      visibility_gap_timeout_);
}

void ChromeVisibilityObserver::OnBrowserRemoved(Browser* browser) {
  // If there are no browser instances left then we should notify that browser
  // is not visible anymore immediately without waiting.
  if (BrowserList::GetInstance()->empty()) {
    CancelVisibilityChange();
    SendVisibilityChangeEvent(false);
  }
}

void ChromeVisibilityObserver::InitVisibilityGapTimeout() {
  const int kDefaultVisibilityGapTimeout = 3;

  int timeout_seconds = kDefaultVisibilityGapTimeout;
  std::string param_value = variations::GetVariationParamValue(
      "DesktopEngagement", "visibility_gap_timeout");
  if (!param_value.empty())
    base::StringToInt(param_value, &timeout_seconds);

  visibility_gap_timeout_ = base::TimeDelta::FromSeconds(timeout_seconds);
}

}  // namespace metrics