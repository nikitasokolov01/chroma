// SPDX-License-Identifier: GPL-3.0-only
#pragma once

class QWidget;

namespace WindowChrome {

// Match supported native decorations to the application palette while keeping
// the window manager's controls, resizing, snapping, and accessibility intact.
// Platforms with existing native integration keep their current behavior.
void install(QWidget* window);

}  // namespace WindowChrome
