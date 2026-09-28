// SPDX-License-Identifier: GPL-3.0-only
#include "SkinTextureDocument.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>

SkinTextureDocument::SkinTextureDocument(QObject* parent) : QObject(parent) {}

bool SkinTextureDocument::isDirty() const
{
    return !(m_state == m_saved);
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
    auto normalized = SkinModel::normalizeTexture(image);
    if (normalized.isNull())
        return false;
    m_state = { normalized, model };
    m_original = m_saved = m_state;
    m_history = { m_state };
    m_cursor = 0;
    m_stroke = false;
    emit changed();
    return true;
}

bool SkinTextureDocument::importPng(const QString& path, QString* error)
{
    auto image = readPng(path, error);
    if (image.isNull())
        return false;
    if (m_state.image.isNull())
        return load(image, m_state.model);
    endStroke();
    m_state.image = image;
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
    m_stroke = true;
}

void SkinTextureDocument::paintPixel(QPoint pixel, QColor color, int brushSize, Part part, Layer layer, bool erase)
{
    if (m_state.image.isNull())
        return;
    const auto editable = uvRegion(part, layer, model());
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
    endStroke();
    if (m_state.model == model)
        return;
    m_state.model = model;
    record();
    emit changed();
}

void SkinTextureDocument::undo()
{
    endStroke();
    if (!canUndo())
        return;
    m_state = m_history[--m_cursor];
    emit changed();
}

void SkinTextureDocument::redo()
{
    endStroke();
    if (!canRedo())
        return;
    m_state = m_history[++m_cursor];
    emit changed();
}

void SkinTextureDocument::reset()
{
    endStroke();
    m_state = m_original;
    record();
    emit changed();
}
