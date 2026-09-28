// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QFont>
#include <QRectF>
#include <QString>

class QPainter;

namespace Clay {

struct Colors {
    QColor Canvas;
    QColor Surface;
    QColor Input;
    QColor Foreground;
    QColor Muted;
};

const Colors& colors(bool dark);
const Colors& colors();
bool dark();

inline const QColor Violet{ "#7C3AED" };
inline const QColor Pink{ "#DB2777" };
inline const QColor Blue{ "#0EA5E9" };
inline const QColor Green{ "#10B981" };
inline const QColor Amber{ "#F59E0B" };

namespace Radius {
inline constexpr qreal Control = 20;
inline constexpr qreal Card = 32;
inline constexpr qreal Container = 48;
}
inline constexpr int ShadowMargin = 5;
inline constexpr int MinimumTarget = 44;

bool enabled();
QFont headingFont(int pixelSize = 0);
// Scoped form surfaces shared by inline dialogs and their nested page containers.
QString formStyleSheet(const QString& selector);
// Windows follows the system's client-area animation preference. Other
// platforms use Qt's UI effects preference; CHROMA_REDUCED_MOTION=1 is an
// explicit, cross-platform opt-out for decorative and interaction motion.
bool motionAllowed();
void drawSurface(QPainter* painter, const QRectF& rect, const QColor& color, qreal radius = Radius::Card,
                 bool pressed = false, qreal lift = 0);

}  // namespace Clay
