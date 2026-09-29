// SPDX-License-Identifier: GPL-3.0-only
#include "SkinGeometry.h"

#include <cmath>

namespace opengl {
std::array<SkinBox, 12> skinBoxes(bool slim)
{
    using D = SkinTextureDocument;
    const float arm = slim ? 3.f : 4.f;
    const float armX = 4.f + arm / 2.f;
    const std::array<QVector3D, 6> sizes = { QVector3D(8, 8, 8),    QVector3D(8, 12, 4), QVector3D(arm, 12, 4),
                                             QVector3D(arm, 12, 4), QVector3D(4, 12, 4), QVector3D(4, 12, 4) };
    const std::array<QVector3D, 6> centers = { QVector3D(0, 4, 0),     QVector3D(0, -6, 0),         QVector3D(-armX, -6, 0),
                                               QVector3D(armX, -6, 0), QVector3D(-1.9f, -18, -.1f), QVector3D(1.9f, -18, -.1f) };
    const std::array<QPoint, 12> origins = { QPoint(0, 0),  QPoint(16, 16), QPoint(40, 16), QPoint(32, 48), QPoint(0, 16), QPoint(16, 48),
                                             QPoint(32, 0), QPoint(16, 32), QPoint(40, 32), QPoint(48, 48), QPoint(0, 32), QPoint(0, 48) };
    std::array<SkinBox, 12> boxes;
    for (int i = 0; i < 12; ++i) {
        const int part = i % 6;
        const float expansion = i < 6 ? 0.f : part == 0 ? 1.f : .5f;
        boxes[i] = { static_cast<D::Part>(part),
                     i < 6 ? D::Base : D::Overlay,
                     sizes[part] + QVector3D(expansion, expansion, expansion),
                     centers[part],
                     origins[i],
                     sizes[part] };
    }
    return boxes;
}

std::array<SkinFace, 6> boxFaces(QVector3D size, QVector3D center, QPoint uv, QVector3D textureSize)
{
    const float x = size.x(), y = size.y(), z = size.z();
    const float u = uv.x(), v = uv.y(), w = textureSize.x(), h = textureSize.y(), d = textureSize.z();
    const QVector3D low = center - size / 2;
    const std::array<QRect, 6> regions = { QRect(u + d, v + d, w, h), QRect(u + d + w, v + d, d, h), QRect(u + 2 * d + w, v + d, w, h),
                                           QRect(u, v + d, d, h),     QRect(u + d + w, v, w, d),     QRect(u + d, v, w, d) };
    std::array<SkinFace, 6> faces = {
        SkinFace{ low + QVector3D(0, 0, z), QVector3D(x, 0, 0), QVector3D(0, y, 0), {}, {}, {}, {} },
        SkinFace{ low + QVector3D(x, 0, z), QVector3D(0, 0, -z), QVector3D(0, y, 0), {}, {}, {}, {} },
        SkinFace{ low + QVector3D(x, 0, 0), QVector3D(-x, 0, 0), QVector3D(0, y, 0), {}, {}, {}, {} },
        SkinFace{ low, QVector3D(0, 0, z), QVector3D(0, y, 0), {}, {}, {}, {} },
        SkinFace{ low, QVector3D(x, 0, 0), QVector3D(0, 0, z), {}, {}, {}, {} },
        SkinFace{ low + QVector3D(0, y, z), QVector3D(x, 0, 0), QVector3D(0, 0, -z), {}, {}, {}, {} },
    };
    for (int i = 0; i < 6; ++i) {
        const auto r = regions[i];
        faces[i].pixels = r;
        faces[i].uv = QVector2D(r.x(), r.y() + (i == 4 ? 0 : r.height()));
        faces[i].uvHorizontal = QVector2D(r.width(), 0);
        faces[i].uvVertical = QVector2D(0, i == 4 ? r.height() : -r.height());
    }
    return faces;
}

std::optional<SkinPick> pickSkin(QVector3D origin,
                                 QVector3D direction,
                                 bool slim,
                                 unsigned baseParts,
                                 unsigned outerParts,
                                 SkinTextureDocument::Part part,
                                 SkinTextureDocument::Layer layer,
                                 const QImage& texture)
{
    using D = SkinTextureDocument;
    if (direction.lengthSquared() < 1e-12f)
        return {};
    direction.normalize();
    std::optional<SkinPick> closest;
    QList<SkinPick> visibleHits;
    for (const auto& box : skinBoxes(slim)) {
        if (!((box.layer == D::Base ? baseParts : outerParts) & (1u << box.part)))
            continue;
        const auto faces = boxFaces(box.size, box.center, box.uv, box.textureSize);
        for (int i = 0; i < 6; ++i) {
            const auto& face = faces[i];
            const auto normal = QVector3D::crossProduct(face.horizontal, face.vertical).normalized();
            const float denominator = QVector3D::dotProduct(direction, normal);
            // Match back-face culling, including rays which start inside a box.
            if (denominator >= -1e-6f)
                continue;
            const float distance = QVector3D::dotProduct(face.origin - origin, normal) / denominator;
            if (distance < 0)
                continue;
            const auto position = origin + direction * distance;
            const auto relative = position - face.origin;
            const float horizontal = QVector3D::dotProduct(relative, face.horizontal) / face.horizontal.lengthSquared();
            const float vertical = QVector3D::dotProduct(relative, face.vertical) / face.vertical.lengthSquared();
            if (horizontal < -1e-5f || horizontal > 1.00001f || vertical < -1e-5f || vertical > 1.00001f)
                continue;
            const auto uv = face.uv + horizontal * face.uvHorizontal + vertical * face.uvVertical;
            const QPoint pixel(qBound(face.pixels.left(), int(std::floor(uv.x())), face.pixels.right()),
                               qBound(face.pixels.top(), int(std::floor(uv.y())), face.pixels.bottom()));
            const SkinPick hit{ box.part, box.layer, pixel, face.pixels, position, distance, i };
            // Match the fragment shader's alpha discard. A transparent outer
            // texel is still an editable target when Outer is explicit.
            const bool visible = box.layer == D::Base || !texture.rect().contains(pixel) || texture.pixelColor(pixel).alpha() >= 26;
            if (visible)
                visibleHits.append(hit);
            if ((layer == D::Both ? visible : layer == box.layer) && (!closest || distance < closest->distance))
                closest = hit;
        }
    }
    // An explicit Base target may be under its own Outer shell. Other visible
    // body parts still occlude it, even when their target layer is hidden.
    if (closest) {
        for (const auto& visible : visibleHits)
            if (visible.part != closest->part && visible.distance < closest->distance - 1e-4f)
                return {};
        if (part != D::All && closest->part != part)
            return {};
    }
    return closest;
}
}  // namespace opengl
