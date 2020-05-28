// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "headless/app/headless_shell_switches.h"

namespace headless {
namespace switches {

// Instructs headless_shell to print document.body.innerHTML to stdout.
const char kDumpDom[] = "dump-dom";

// Uses a specified proxy server, overrides system settings. This switch only
// affects HTTP and HTTPS requests.
const char kProxyServer[] = "proxy-server";

// Use the given address instead of the default loopback for accepting remote
// debugging connections. Should be used together with --remote-debugging-port.
// Note that the remote debugging protocol does not perform any authentication,
// so exposing it too widely can be a security risk.
const char kRemoteDebuggingAddress[] = "remote-debugging-address";

// Runs a read-eval-print loop that allows the user to evaluate Javascript
// expressions.
const char kRepl[] = "repl";

// Save a screenshot of the loaded page.
const char kScreenshot[] = "screenshot";

// Sets the GL implementation to use. Use a blank string to disable GL
// rendering.
const char kUseGL[] = "use-gl";

// Directory where the browser stores the user profile.
const char kUserDataDir[] = "user-data-dir";

// Sets the initial window size. Provided as string in the format "800x600".
const char kWindowSize[] = "window-size";

}  // namespace switches
}  // namespace headless
