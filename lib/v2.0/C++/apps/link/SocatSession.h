// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace link_app {
  /// Manage the user's detached socat process without loading the link configuration.
  int manageSocatSession(const std::string &action, const char *executable);
}
