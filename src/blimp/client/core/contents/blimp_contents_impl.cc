// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/client/core/contents/blimp_contents_impl.h"

#include "base/memory/ptr_util.h"
#include "base/supports_user_data.h"
#include "blimp/client/public/contents/blimp_contents_observer.h"

#if defined(OS_ANDROID)
#include "blimp/client/core/contents/android/blimp_contents_impl_android.h"
#endif  // OS_ANDROID

namespace blimp {
namespace client {

namespace {

#if defined(OS_ANDROID)
const char kBlimpContentsImplAndroidKey[] = "blimp_contents_impl_android";
#endif  // OS_ANDROID
}

BlimpContentsImpl::BlimpContentsImpl()
    : navigation_controller_(this, nullptr) {}

BlimpContentsImpl::~BlimpContentsImpl() {}

#if defined(OS_ANDROID)

base::android::ScopedJavaLocalRef<jobject>
BlimpContentsImpl::GetJavaBlimpContentsImpl() {
  return GetBlimpContentsImplAndroid()->GetJavaObject();
}

BlimpContentsImplAndroid* BlimpContentsImpl::GetBlimpContentsImplAndroid() {
  BlimpContentsImplAndroid* blimp_contents_impl_android =
      static_cast<BlimpContentsImplAndroid*>(
          GetUserData(kBlimpContentsImplAndroidKey));
  if (!blimp_contents_impl_android) {
    blimp_contents_impl_android = new BlimpContentsImplAndroid(this);
    SetUserData(kBlimpContentsImplAndroidKey, blimp_contents_impl_android);
  }
  return blimp_contents_impl_android;
}

#endif  // defined(OS_ANDROID)

BlimpNavigationControllerImpl& BlimpContentsImpl::GetNavigationController() {
  return navigation_controller_;
}

void BlimpContentsImpl::AddObserver(BlimpContentsObserver* observer) {
  observers_.AddObserver(observer);
}

void BlimpContentsImpl::RemoveObserver(BlimpContentsObserver* observer) {
  observers_.RemoveObserver(observer);
}

void BlimpContentsImpl::OnNavigationStateChanged() {
  FOR_EACH_OBSERVER(BlimpContentsObserver, observers_,
                    OnNavigationStateChanged());
}

}  // namespace client
}  // namespace blimp
