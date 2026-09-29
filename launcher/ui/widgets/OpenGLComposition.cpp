// SPDX-License-Identifier: GPL-3.0-only
#include "OpenGLComposition.h"

#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLWidget>

namespace OpenGLComposition {

bool prepare(QWidget* window)
{
    if (!window || !window->isWindow())
        return false;
    constexpr auto anchorName = "openGLCompositionAnchor";
    if (window->findChild<QOpenGLWidget*>(anchorName, Qt::FindDirectChildrenOnly))
        return true;
    if (window->testAttribute(Qt::WA_WState_Created))
        return false;

    const auto platform = QGuiApplication::platformName();
    if (platform == "offscreen" || platform == "minimal")
        return false;
    QOpenGLContext probe;
    if (!probe.create())
        return false;

    // Qt 6.4+ recreates a raster native window when its first QOpenGLWidget
    // arrives. A hidden child is included in Qt's initial surface selection,
    // so inline skin previews can be inserted later without replacing the
    // launcher window. The anchor never shows, paints, or creates its own GL
    // context, and occupies no layout space.
    // https://doc.qt.io/qt-6/qopenglwidget.html#limitations-and-other-considerations
    auto* anchor = new QOpenGLWidget(window);
    anchor->setObjectName(anchorName);
    anchor->setFixedSize(0, 0);
    anchor->setFocusPolicy(Qt::NoFocus);
    anchor->setAttribute(Qt::WA_TransparentForMouseEvents);
    anchor->hide();
    return true;
}

}  // namespace OpenGLComposition
