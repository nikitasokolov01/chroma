// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QImage>
#include <QRegion>
#include <QVector>

namespace SkinPalette {
struct Family {
    QColor color;  // The most common color anchors the family's replacement shade.
    int pixels = 0;
};

// Hue families include their darker/lighter shades. Near-neutral colors form
// their own family and never match a colored source, regardless of tolerance.
QVector<Family> extractFamilies(const QImage& image, const QRegion& region, int tolerance = 24);
bool matchesFamily(const QColor& color, const QColor& source, int tolerance = 24);
QImage swapFamily(const QImage& image,
                  const QRegion& region,
                  const QColor& source,
                  const QColor& target,
                  int tolerance = 24,
                  int* changedPixels = nullptr);
}  // namespace SkinPalette
