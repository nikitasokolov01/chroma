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
    return hex.size() == 7 && hex.startsWith('#') && color.isValid() ? color : QColor("#b7a5f5");
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
    // A very dark custom accent is valid for filled buttons, but links still
    // need enough contrast against the dark content surface.
    for (int step = 0; step < 32 && (luminance(color) + 0.05) / (backgroundLight + 0.05) < 4.5; ++step) {
        color.setRgb(qMin(255, color.red() + 8), qMin(255, color.green() + 8), qMin(255, color.blue() + 8));
    }
    return color;
}

}  // namespace AccentColor
