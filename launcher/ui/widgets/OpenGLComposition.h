// SPDX-License-Identifier: GPL-3.0-only
#pragma once

class QWidget;

namespace OpenGLComposition {
// Call before a top-level widget creates its native window. Returns false if
// OpenGL is unavailable or the window has already been created.
bool prepare(QWidget* window);
}  // namespace OpenGLComposition
