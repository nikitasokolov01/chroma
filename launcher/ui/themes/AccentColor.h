// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QString>
#include <cmath>

namespace AccentColor {

inline QColor fromSetting(const QString& value)
{
    const QString hex = value.trimmed();
    const QColor color(hex);
    // Persist opaque RGB values only. Malformed or old configuration values
    // must never result in invisible controls.
    return hex.size() == 7 && hex.startsWith('#') && color.isValid() ? color : QColor("#7c3aed");
}

inline double luminance(const QColor& color)
{
    const auto linear = [](double channel) { return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

inline QColor foreground(const QColor& background)
{
    const double light = luminance(background);
    const double blackContrast = (light + 0.05) / 0.05;
    const double whiteContrast = 1.05 / (light + 0.05);
    return blackContrast >= whiteContrast ? QColor(Qt::black) : QColor(Qt::white);
}

inline QColor link(const QColor& accent, const QColor& background)
{
    QColor color = accent;
    const double backgroundLight = luminance(background);
    const auto contrast = [backgroundLight](const QColor& candidate) {
        const double light = luminance(candidate);
        return (qMax(light, backgroundLight) + 0.05) / (qMin(light, backgroundLight) + 0.05);
    };
    // Preserve readable custom accents. Otherwise move towards the endpoint
    // with the strongest contrast, on either a light or dark surface.
    const int direction = foreground(background) == QColor(Qt::white) ? 8 : -8;
    for (int step = 0; step < 32 && contrast(color) < 4.5; ++step) {
        color.setRgb(qBound(0, color.red() + direction, 255), qBound(0, color.green() + direction, 255),
                     qBound(0, color.blue() + direction, 255));
    }
    return color;
}

}  // namespace AccentColor
