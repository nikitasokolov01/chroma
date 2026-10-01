// SPDX-License-Identifier: GPL-3.0-only
#include "SkinTextureDocument.h"

#include <QBitArray>
#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QSaveFile>

namespace {
constexpr auto PatchMime = "application/x-chroma-skin-pixels-v1";
constexpr int PatchHeaderSize = 16;
constexpr int MaximumPatchBytes = PatchHeaderSize + 64 * 64 * 4 + 64 * 64 / 8;
constexpr int MaximumPngBytes = 1024 * 1024;

bool validPatch(const SkinTextureDocument::PixelPatch& patch)
{
    const auto size = patch.image.size();
    return !patch.image.isNull() && size.width() > 0 && size.height() > 0 && size.width() <= 64 && size.height() <= 64 &&
           patch.origin.x() >= 0 && patch.origin.y() >= 0 && patch.origin.x() <= 64 - size.width() &&
           patch.origin.y() <= 64 - size.height() && !patch.mask.isEmpty() && patch.mask.subtracted(QRegion(patch.image.rect())).isEmpty();
}
bool sameFillColor(QRgb left, QRgb right)
{
    return left == right || (qAlpha(left) == 0 && qAlpha(right) == 0);
}
QColor effectColor(QColor original, SkinTextureDocument::Effect effect, int amount)
{
    using E = SkinTextureDocument;
    if ((effect == E::Hue || effect == E::Brightness) && amount == 0)
        return original;
    auto next = original;
    switch (effect) {
        case E::Hue: {
            const auto hue = original.hsvHueF();
            if (hue >= 0) {
                qreal shifted = hue + amount / 360.0;
                if (shifted < 0)
                    shifted += 1;
                if (shifted >= 1)
                    shifted -= 1;
                next = QColor::fromHsvF(shifted, original.hsvSaturationF(), original.valueF(), original.alphaF());
            }
            break;
        }
        case E::Brightness: {
            const auto channel = [amount](int value) {
                return amount > 0 ? value + qRound((255 - value) * amount / 100.0) : qRound(value * (100 + amount) / 100.0);
            };
            next.setRgb(channel(original.red()), channel(original.green()), channel(original.blue()), original.alpha());
            break;
        }
        case E::Grayscale: {
            const int gray = qGray(original.rgba());
            next.setRgb(gray, gray, gray, original.alpha());
            break;
        }
        case E::Invert:
            next.setRgb(255 - original.red(), 255 - original.green(), 255 - original.blue(), original.alpha());
            break;
    }
    return next;
}
}  // namespace

SkinTextureDocument::SkinTextureDocument(QObject* parent) : QObject(parent) {}

bool SkinTextureDocument::isDirty() const
{
    return !(m_state == m_saved);
}

void SkinTextureDocument::setSelection(QRegion selection)
{
    selection &= uvRegion(All, Both, model()) & QRegion(m_state.image.rect());
    if (m_selection == selection)
        return;
    finishColorAdjustments();
    m_selection = selection;
    emit selectionChanged();
}

void SkinTextureDocument::clearSelection()
{
    setSelection({});
}

QRegion SkinTextureDocument::editableRegion(const QRegion& allowed) const
{
    auto region = allowed & uvRegion(All, Both, model()) & QRegion(m_state.image.rect());
    if (hasSelection())
        region &= m_selection;
    return region;
}

QImage SkinTextureDocument::readPng(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.peek(8) != QByteArray("\x89PNG\r\n\x1a\n", 8)) {
        if (error)
            *error = tr("Choose a readable PNG skin file.");
        return {};
    }
    QImageReader reader(&file, "png");
    reader.setAutoDetectImageFormat(false);
    const auto size = reader.size();
    // Inspect dimensions before allocating. A Minecraft texture never needs an
    // arbitrarily large decoded image, even if the filename ends in .png.
    if (size != QSize(64, 32) && size != QSize(64, 64)) {
        if (error)
            *error = tr("Choose a 64 × 64 or 64 × 32 pixel PNG skin.");
        return {};
    }
    auto image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = tr("The PNG could not be read: %1").arg(reader.errorString());
        return {};
    }
    return SkinModel::normalizeTexture(image);
}

QRegion SkinTextureDocument::uvRegion(Part part, Layer layer, SkinModel::Model model)
{
    QRegion result;
    auto addBox = [&result](QPoint origin, int width, int height, int depth) {
        result += QRect(origin + QPoint(depth, 0), QSize(width * 2, depth));
        result += QRect(origin + QPoint(0, depth), QSize(2 * (width + depth), height));
    };
    const int armWidth = model == SkinModel::SLIM ? 3 : 4;
    for (int p = Head; p <= LeftLeg; ++p) {
        if (part != All && p != part)
            continue;
        const QPoint base[] = { { 0, 0 }, { 16, 16 }, { 40, 16 }, { 32, 48 }, { 0, 16 }, { 16, 48 } };
        const QPoint outer[] = { { 32, 0 }, { 16, 32 }, { 40, 32 }, { 48, 48 }, { 0, 32 }, { 0, 48 } };
        const int width = p == Head || p == Body ? 8 : p == RightArm || p == LeftArm ? armWidth : 4;
        const int height = p == Head ? 8 : 12;
        const int depth = p == Head ? 8 : 4;
        if (layer != Overlay)
            addBox(base[p], width, height, depth);
        if (layer != Base)
            addBox(outer[p], width, height, depth);
    }
    return result;
}

bool SkinTextureDocument::load(const QImage& image, SkinModel::Model model)
{
    finishColorAdjustments();
    auto normalized = SkinModel::normalizeTexture(image);
    if (normalized.isNull())
        return false;
    m_state = { normalized, model };
    m_original = m_saved = m_state;
    m_history = { m_state };
    m_cursor = 0;
    m_stroke = false;
    clearSelection();
    emit changed();
    return true;
}

bool SkinTextureDocument::importPng(const QString& path, QString* error)
{
    finishColorAdjustments();
    auto image = readPng(path, error);
    if (image.isNull())
        return false;
    if (m_state.image.isNull())
        return load(image, m_state.model);
    endStroke();
    m_state.image = image;
    clearSelection();
    record();
    emit changed();
    return true;
}

bool SkinTextureDocument::exportPng(const QString& path, QString* error) const
{
    QSaveFile file(path);
    if (m_state.image.isNull() || !file.open(QIODevice::WriteOnly) || !m_state.image.save(&file, "PNG") || !file.commit()) {
        if (error)
            *error = tr("Could not save the skin: %1").arg(file.errorString());
        return false;
    }
    return true;
}

void SkinTextureDocument::markSaved()
{
    m_saved = m_state;
    emit changed();
}

void SkinTextureDocument::beginStroke()
{
    finishColorAdjustments();
    m_stroke = true;
}

void SkinTextureDocument::paintPixel(QPoint pixel, QColor color, int brushSize, Part part, Layer layer, bool erase)
{
    finishColorAdjustments();
    if (m_state.image.isNull())
        return;
    const auto editable = editableRegion(uvRegion(part, layer, model()));
    const auto base = uvRegion(All, Base, model());
    const int size = qBound(1, brushSize, 8);
    const QRect brush(pixel - QPoint((size - 1) / 2, (size - 1) / 2), QSize(size, size));
    bool modified = false;
    for (int y = brush.top(); y <= brush.bottom(); ++y) {
        for (int x = brush.left(); x <= brush.right(); ++x) {
            const QPoint point(x, y);
            if (!editable.contains(point) || !m_state.image.rect().contains(point) || (erase && base.contains(point)))
                continue;
            QColor next = erase ? QColor(Qt::transparent) : color;
            if (base.contains(point))
                next.setAlpha(255);
            if (m_state.image.pixelColor(point) != next) {
                m_state.image.setPixelColor(point, next);
                modified = true;
            }
        }
    }
    if (modified) {
        if (!m_stroke)
            record();
        emit changed();
    }
}

void SkinTextureDocument::endStroke()
{
    if (!m_stroke)
        return;
    m_stroke = false;
    record();
    emit changed();
}

void SkinTextureDocument::record()
{
    if (!m_history.isEmpty() && m_history[m_cursor] == m_state)
        return;
    m_history.resize(m_cursor + 1);
    m_history.append(m_state);
    if (m_history.size() > 101)
        m_history.removeFirst();
    m_cursor = m_history.size() - 1;
}

void SkinTextureDocument::setModel(SkinModel::Model model)
{
    finishColorAdjustments();
    endStroke();
    if (m_state.model == model)
        return;
    m_state.model = model;
    setSelection(m_selection);
    record();
    emit changed();
}

void SkinTextureDocument::undo()
{
    finishColorAdjustments();
    endStroke();
    if (!canUndo())
        return;
    m_state = m_history[--m_cursor];
    restoreColorAdjustments();
    setSelection(m_selection);
    emit changed();
}

void SkinTextureDocument::redo()
{
    finishColorAdjustments();
    endStroke();
    if (!canRedo())
        return;
    m_state = m_history[++m_cursor];
    restoreColorAdjustments();
    setSelection(m_selection);
    emit changed();
}

void SkinTextureDocument::reset()
{
    finishColorAdjustments();
    endStroke();
    m_state = m_original;
    restoreColorAdjustments();
    setSelection(m_selection);
    record();
    emit changed();
}

void SkinTextureDocument::floodFill(QPoint seed, QColor color, const QRegion& allowed)
{
    finishColorAdjustments();
    endStroke();
    const auto editable = editableRegion(allowed);
    if (!color.isValid() || !editable.contains(seed))
        return;
    const auto target = m_state.image.pixel(seed);
    const auto base = uvRegion(All, Base, model());
    QBitArray visited(64 * 64);
    QVector<QPoint> pending{ seed };
    visited.setBit(seed.y() * 64 + seed.x());
    bool modified = false;
    for (int i = 0; i < pending.size(); ++i) {
        const auto point = pending[i];
        QColor next = color;
        if (base.contains(point))
            next.setAlpha(255);
        if (m_state.image.pixel(point) != next.rgba()) {
            m_state.image.setPixelColor(point, next);
            modified = true;
        }
        for (const QPoint direction : { QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1) }) {
            const auto neighbor = point + direction;
            if (!editable.contains(neighbor))
                continue;
            const int index = neighbor.y() * 64 + neighbor.x();
            if (!visited.testBit(index) && sameFillColor(m_state.image.pixel(neighbor), target)) {
                visited.setBit(index);
                pending.append(neighbor);
            }
        }
    }
    if (modified) {
        record();
        emit changed();
    }
}

void SkinTextureDocument::applyEffect(Effect effect, int amount, const QRegion& allowed)
{
    finishColorAdjustments();
    endStroke();
    if (effect < Hue || effect > Invert || ((effect == Hue || effect == Brightness) && amount == 0))
        return;
    amount = qBound(effect == Hue ? -180 : -100, amount, effect == Hue ? 180 : 100);
    const auto editable = editableRegion(allowed);
    bool modified = false;
    for (const auto& rect : editable) {
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const auto original = m_state.image.pixelColor(x, y);
                if (!original.alpha())
                    continue;
                auto next = original;
                next = effectColor(original, effect, amount);
                if (next.rgba() != original.rgba()) {
                    m_state.image.setPixelColor(x, y, next);
                    modified = true;
                }
            }
        }
    }
    if (modified) {
        record();
        emit changed();
    }
}

void SkinTextureDocument::finishColorAdjustments()
{
    if (m_adjustmentBase.isNull())
        return;
    m_adjustmentBase = {};
    m_adjustmentRegion = {};
    m_adjustmentHistory = -1;
    m_hueAdjustment = m_brightnessAdjustment = 0;
    m_state.adjustmentBase = {};
    m_state.adjustmentRegion = {};
    m_state.hue = m_state.brightness = 0;
    emit adjustmentsChanged();
}

void SkinTextureDocument::restoreColorAdjustments()
{
    m_adjustmentBase = m_state.adjustmentBase;
    m_adjustmentRegion = m_state.adjustmentRegion;
    m_hueAdjustment = m_state.hue;
    m_brightnessAdjustment = m_state.brightness;
    m_adjustmentHistory = m_adjustmentBase.isNull() ? -1 : m_cursor;
    emit adjustmentsChanged();
}

void SkinTextureDocument::setColorAdjustments(int hue, int brightness, const QRegion& allowed)
{
    endStroke();
    const auto region = editableRegion(allowed);
    if (m_state.image.isNull())
        return;
    if (!m_adjustmentBase.isNull() && region != m_adjustmentRegion)
        finishColorAdjustments();
    if (m_adjustmentBase.isNull()) {
        m_adjustmentBase = m_state.image;
        m_adjustmentRegion = region;
    }
    m_hueAdjustment = qBound(-180, hue, 180);
    m_brightnessAdjustment = qBound(-100, brightness, 100);
    const auto before = m_state.image;
    auto image = m_adjustmentBase;
    for (const auto& rect : m_adjustmentRegion)
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const auto original = m_adjustmentBase.pixelColor(x, y);
                if (!original.alpha())
                    continue;
                auto color = effectColor(original, Hue, m_hueAdjustment);
                color = effectColor(color, Brightness, m_brightnessAdjustment);
                image.setPixelColor(x, y, color);
            }
    m_state.image = image;
    m_state.adjustmentBase = m_adjustmentBase;
    m_state.adjustmentRegion = m_adjustmentRegion;
    m_state.hue = m_hueAdjustment;
    m_state.brightness = m_brightnessAdjustment;
    if (image == m_adjustmentBase && m_adjustmentHistory >= 0) {
        m_history.resize(m_cursor + 1);
        if (m_adjustmentHistory == 0) {
            // The baseline can have fallen outside the bounded undo history.
            m_history[0] = { image, m_state.model };
            m_cursor = 0;
        } else {
            m_history.removeAt(m_adjustmentHistory);
            m_cursor = m_adjustmentHistory - 1;
        }
        m_adjustmentHistory = -1;
    } else if (image != m_adjustmentBase) {
        if (m_adjustmentHistory < 0) {
            record();
            m_adjustmentHistory = m_cursor;
        } else {
            if (before != image)
                m_history.resize(m_cursor + 1);
            m_history[m_adjustmentHistory] = m_state;
        }
    }
    emit adjustmentsChanged();
    if (before != image)
        emit changed();
}

SkinTextureDocument::PixelPatch SkinTextureDocument::copyPixels(const QRegion& allowed) const
{
    const auto region = editableRegion(allowed);
    if (region.isEmpty())
        return {};
    const auto bounds = region.boundingRect();
    PixelPatch patch{ QImage(bounds.size(), QImage::Format_ARGB32), region.translated(-bounds.topLeft()), bounds.topLeft() };
    patch.image.fill(Qt::transparent);
    for (const auto& rect : region)
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x)
                patch.image.setPixel(x - bounds.left(), y - bounds.top(), m_state.image.pixel(x, y));
    return patch;
}

void SkinTextureDocument::pastePixels(const PixelPatch& patch, QPoint destination, const QRegion& allowed)
{
    finishColorAdjustments();
    endStroke();
    if (!validPatch(patch))
        return;
    const auto editable = editableRegion(allowed);
    const auto base = uvRegion(All, Base, model());
    bool modified = false;
    for (const auto& rect : patch.mask) {
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x) {
                const qint64 targetX = qint64(destination.x()) + x, targetY = qint64(destination.y()) + y;
                if (targetX < 0 || targetX >= 64 || targetY < 0 || targetY >= 64)
                    continue;
                const QPoint point(static_cast<int>(targetX), static_cast<int>(targetY));
                if (!editable.contains(point))
                    continue;
                auto next = patch.image.pixelColor(x, y);
                if (base.contains(point)) {
                    if (!next.alpha())
                        continue;
                    next.setAlpha(255);
                }
                if (m_state.image.pixel(point) != next.rgba()) {
                    m_state.image.setPixelColor(point, next);
                    modified = true;
                }
            }
        }
    }
    if (modified) {
        record();
        emit changed();
    }
}

QByteArray SkinTextureDocument::encodePatch(const PixelPatch& patch)
{
    if (!validPatch(patch))
        return {};
    const int width = patch.image.width(), height = patch.image.height();
    const int maskBytes = (width * height + 7) / 8;
    QByteArray bytes(PatchHeaderSize + maskBytes + width * height * 4, '\0');
    bytes.replace(qsizetype{ 0 }, qsizetype{ 8 }, "CHSKPIX1", qsizetype{ 8 });
    bytes[8] = static_cast<char>(width);
    bytes[9] = static_cast<char>(height);
    bytes[10] = static_cast<char>(patch.origin.x());
    bytes[11] = static_cast<char>(patch.origin.y());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!patch.mask.contains(QPoint(x, y)))
                continue;
            const int index = y * width + x;
            bytes[PatchHeaderSize + index / 8] =
                static_cast<char>(static_cast<quint8>(bytes[PatchHeaderSize + index / 8]) | (1 << (index % 8)));
            const auto color = patch.image.pixelColor(x, y);
            const int offset = PatchHeaderSize + maskBytes + index * 4;
            bytes[offset] = static_cast<char>(color.red());
            bytes[offset + 1] = static_cast<char>(color.green());
            bytes[offset + 2] = static_cast<char>(color.blue());
            bytes[offset + 3] = static_cast<char>(color.alpha());
        }
    }
    return bytes;
}

SkinTextureDocument::PixelPatch SkinTextureDocument::decodePatch(const QByteArray& bytes)
{
    if (bytes.size() < PatchHeaderSize || bytes.size() > MaximumPatchBytes || bytes.first(8) != "CHSKPIX1" ||
        bytes.mid(12, 4) != QByteArray(4, '\0'))
        return {};
    const int width = static_cast<quint8>(bytes[8]), height = static_cast<quint8>(bytes[9]);
    const QPoint origin(static_cast<quint8>(bytes[10]), static_cast<quint8>(bytes[11]));
    if (width < 1 || height < 1 || width > 64 || height > 64 || origin.x() > 64 - width || origin.y() > 64 - height)
        return {};
    const int count = width * height, maskBytes = (count + 7) / 8;
    if (bytes.size() != PatchHeaderSize + maskBytes + count * 4)
        return {};
    if (count % 8 && (static_cast<quint8>(bytes[PatchHeaderSize + maskBytes - 1]) >> (count % 8)))
        return {};
    const auto selected = [&bytes](int index) {
        return (static_cast<quint8>(bytes[PatchHeaderSize + index / 8]) & (1 << (index % 8))) != 0;
    };
    QRegion mask;
    for (int y = 0; y < height; ++y) {
        int start = -1;
        for (int x = 0; x <= width; ++x) {
            const bool included = x < width && selected(y * width + x);
            if (included && start < 0)
                start = x;
            else if (!included && start >= 0) {
                mask += QRect(start, y, x - start, 1);
                start = -1;
            }
        }
    }
    if (mask.isEmpty())
        return {};
    PixelPatch patch{ QImage(width, height, QImage::Format_ARGB32), mask, origin };
    patch.image.fill(Qt::transparent);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int index = y * width + x;
            if (!selected(index))
                continue;
            const int offset = PatchHeaderSize + maskBytes + index * 4;
            patch.image.setPixel(x, y,
                                 qRgba(static_cast<quint8>(bytes[offset]), static_cast<quint8>(bytes[offset + 1]),
                                       static_cast<quint8>(bytes[offset + 2]), static_cast<quint8>(bytes[offset + 3])));
        }
    }
    return patch;
}

bool SkinTextureDocument::writeClipboard(const PixelPatch& patch)
{
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return false;
    const auto bytes = encodePatch(patch);
    if (bytes.isEmpty())
        return false;
    // Also publish a normal image with unselected pixels cleared for other applications.
    const auto image = decodePatch(bytes).image;
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return false;
    auto* mime = new QMimeData;
    mime->setData(PatchMime, bytes);
    mime->setData("image/png", png);
    mime->setImageData(image);
    QGuiApplication::clipboard()->setMimeData(mime);
    return true;
}

SkinTextureDocument::PixelPatch SkinTextureDocument::readClipboard()
{
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        return {};
    const auto* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return {};
    if (mime->hasFormat(PatchMime))
        return decodePatch(mime->data(PatchMime));
    if (!mime->hasFormat("image/png"))
        return {};
    QByteArray bytes = mime->data("image/png");
    if (bytes.size() > MaximumPngBytes || !bytes.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)))
        return {};
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "png");
    reader.setAutoDetectImageFormat(false);
    const auto size = reader.size();
    if (size.width() < 1 || size.height() < 1 || size.width() > 64 || size.height() > 64)
        return {};
    auto image = reader.read();
    if (image.isNull())
        return {};
    return { image.convertToFormat(QImage::Format_ARGB32), QRegion(image.rect()), {} };
}
