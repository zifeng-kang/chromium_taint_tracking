// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

if (typeof Polymer == 'undefined') {
  Polymer = {
    dom: 'shadow',
    lazyRegister: true,
    // TODO(dbeam): re-enable when this doesn't break things.
    // useNativeCSSProperties: true,
  };
} else {
  console.error('Polymer is already defined.');
}
