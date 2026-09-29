// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QImage>
#include <QVector2D>
#include <QVector3D>
#include <array>
#include <optional>

#include "minecraft/skins/SkinTextureDocument.h"

namespace opengl {
// The renderer and picker share these dimensions and face coordinates. UVs
// use image coordinates (top left origin); the GPU upload flips their Y axis.
struct SkinBox {
    SkinTextureDocument::Part part;
    SkinTextureDocument::Layer layer;
    QVector3D size;
    QVector3D center;
    QPoint uv;
    QVector3D textureSize;
};
struct SkinFace {
    QVector3D origin, horizontal, vertical;
    QVector2D uv, uvHorizontal, uvVertical;
    QRect pixels;
};
struct SkinPick {
    SkinTextureDocument::Part part;
    SkinTextureDocument::Layer layer;
    QPoint pixel;
    QRect faceRect;
    QVector3D position;
    float distance;
    int face;
};
std::array<SkinBox, 12> skinBoxes(bool slim);
std::array<SkinFace, 6> boxFaces(QVector3D size, QVector3D center, QPoint uv, QVector3D textureSize);
std::optional<SkinPick> pickSkin(QVector3D origin,
                                 QVector3D direction,
                                 bool slim,
                                 unsigned baseParts,
                                 unsigned outerParts,
                                 SkinTextureDocument::Part part = SkinTextureDocument::All,
                                 SkinTextureDocument::Layer layer = SkinTextureDocument::Both,
                                 const QImage& texture = {});
}  // namespace opengl
