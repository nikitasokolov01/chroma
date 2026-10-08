// SPDX-License-Identifier: GPL-3.0-only
#include "SkinPalette.h"

#include <QHash>
#include <algorithm>
#include <cmath>

namespace {
bool neutral(const QColor& color)
{
    return color.hsvSaturationF() < 0.12;
}
qreal hueOffset(qreal hue, qreal anchor)
{
    auto offset = hue - anchor;
    if (offset > 0.5)
        offset -= 1;
    if (offset < -0.5)
        offset += 1;
    return offset;
}
// Map the anchor to the requested value while retaining the ordering and
// available range of shadows and highlights, instead of clipping an offset.
qreal remapLightness(qreal value, qreal source, qreal target)
{
    if (value <= source && source > 0)
        return value * target / source;
    if (source < 1)
        return target + (value - source) * (1 - target) / (1 - source);
    return target;
}
QColor recolor(const QColor& original, const QColor& source, const QColor& target)
{
    qreal hue = target.hslHueF();
    const qreal targetSaturation = target.hslSaturationF();
    const qreal saturation = neutral(source)
                                 ? targetSaturation
                                 : qBound(qreal(0), original.hslSaturationF() * targetSaturation / source.hslSaturationF(), qreal(1));
    if (hue >= 0 && !neutral(source)) {
        hue += hueOffset(original.hslHueF(), source.hslHueF());
        hue -= std::floor(hue);
    }
    const auto lightness = remapLightness(original.lightnessF(), source.lightnessF(), target.lightnessF());
    auto result = QColor::fromHslF(hue, saturation, qBound(qreal(0), lightness, qreal(1)));
    result.setAlpha(original.alpha());
    return result;
}
}  // namespace

bool SkinPalette::matchesFamily(const QColor& color, const QColor& source, int tolerance)
{
    if (!color.isValid() || !source.isValid() || color.alpha() == 0 || source.alpha() == 0)
        return false;
    if (neutral(color) != neutral(source))
        return false;
    if (neutral(source))
        return true;
    return std::abs(hueOffset(color.hsvHueF(), source.hsvHueF())) * 360 <= qBound(0, tolerance, 180) + 0.01;
}

QVector<SkinPalette::Family> SkinPalette::extractFamilies(const QImage& image, const QRegion& region, int tolerance)
{
    QHash<QRgb, int> histogram;
    for (const auto& rect : region & QRegion(image.rect()))
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.alpha())
                    ++histogram[color.rgb()];
            }
    QVector<Family> colors;
    colors.reserve(histogram.size());
    for (auto it = histogram.cbegin(); it != histogram.cend(); ++it)
        colors.append({ QColor::fromRgb(it.key()), it.value() });
    const auto order = [](const Family& a, const Family& b) {
        return a.pixels != b.pixels ? a.pixels > b.pixels : a.color.rgb() < b.color.rgb();
    };
    std::sort(colors.begin(), colors.end(), order);
    QVector<Family> families;
    for (const auto& color : colors) {
        auto family = std::find_if(families.begin(), families.end(), [&](const Family& item) {
            return matchesFamily(color.color, item.color, tolerance);
        });
        if (family == families.end())
            families.append(color);
        else
            family->pixels += color.pixels;
    }
    std::sort(families.begin(), families.end(), order);
    return families;
}

QImage SkinPalette::swapFamily(const QImage& image,
                               const QRegion& region,
                               const QColor& source,
                               const QColor& target,
                               int tolerance,
                               int* changedPixels)
{
    if (changedPixels)
        *changedPixels = 0;
    if (image.isNull() || region.isEmpty() || !source.isValid() || !target.isValid() || source.rgb() == target.rgb())
        return image;
    auto result = image.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    for (const auto& rect : region & QRegion(image.rect()))
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const auto original = image.pixelColor(x, y);
                if (!matchesFamily(original, source, tolerance))
                    continue;
                const auto replacement = recolor(original, source, target);
                if (original.rgba() != replacement.rgba()) {
                    result.setPixelColor(x, y, replacement);
                    ++count;
                }
            }
    if (changedPixels)
        *changedPixels = count;
    return count ? result : image;
}
