// SPDX-License-Identifier: GPL-3.0-only
#pragma once

class QWidget;

namespace WindowChrome {

// Match supported native decorations to the application palette while keeping
// the window manager's controls, resizing, snapping, and accessibility intact.
// Platforms with existing native integration keep their current behavior.
void install(QWidget* window);

// Remove the native Windows caption and use the supplied title strip instead.
// Retain the system resize borders so Qt's client-size calculations remain
// accurate, along with resize, snap, keyboard commands and work-area behavior.
// Only this explicit top-level window is customized; dialogs remain native.
// Offscreen/minimal Windows tests retain controls without a native frame.
// Returns false (and hides controls) on unsupported platforms or invalid input.
bool installCustom(QWidget* window, QWidget* dragRegion, QWidget* controls);

}  // namespace WindowChrome
