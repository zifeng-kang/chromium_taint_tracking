// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/features/permission_feature.h"

#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"

namespace extensions {

PermissionFeature::PermissionFeature() {
}

PermissionFeature::~PermissionFeature() {
}

Feature::Availability PermissionFeature::IsAvailableToContext(
    const Extension* extension,
    Feature::Context context,
    const GURL& url,
    Feature::Platform platform) const {
  Availability availability = SimpleFeature::IsAvailableToContext(extension,
                                                                  context,
                                                                  url,
                                                                  platform);
  if (!availability.is_available())
    return availability;

  if (extension && !extension->permissions_data()->HasAPIPermission(name()))
    return CreateAvailability(NOT_PRESENT, extension->GetType());

  return CreateAvailability(IS_AVAILABLE);
}

bool PermissionFeature::Validate(std::string* error) {
  if (!SimpleFeature::Validate(error))
    return false;

  if (extension_types().empty()) {
    *error = name() + ": Permission features must specify at least one " +
             "value for extension_types.";
    return false;
  }

  if (!contexts().empty()) {
    *error = name() + ": Permission features do not support contexts.";
    return false;
  }

  return true;
}

}  // namespace extensions
