// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 *
 * This file contains typedefs for chromeOS OOBE properties.
 */

var OobeTypes = {};

/**
 * ChromeOS OOBE language descriptor.
 * @typedef {{
 *   code: (String|undefined),
 *   displayName: (String|undefined),
 *   textDirection: (String|undefined),
 *   nativeDisplayName: (String|undefined),
 * }}
 */
OobeTypes.LanguageDsc;

/**
 * ChromeOS OOBE input method descriptor.
 * @typedef {{
 *   value: (String|undefined),
 *   title: (String|undefined),
 *   selected: (Boolean|undefined),
 * }}
 */
OobeTypes.IMEDsc;

