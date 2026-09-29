// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2024 Trial97 <alexandru.tripon97@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "BoxGeometry.h"
#include "SkinGeometry.h"

#include <QList>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>

struct VertexData {
    QVector4D position;
    QVector2D texCoord;
    VertexData(const QVector4D& pos, const QVector2D& tex) : position(pos), texCoord(tex) {}
};

// Indices for drawing cube faces using triangle strips.
// Triangle strips can be connected by duplicating indices
// between the strips. If connecting strips have opposite
// vertex order then last index of the first strip and first
// index of the second strip needs to be duplicated. If
// connecting strips have same vertex order then only last
// index of the first strip needs to be duplicated.
static const QList<GLushort> indices = {
    0,  1,  2,  3,  3,       // Face 0 - triangle strip ( v0,  v1,  v2,  v3)
    4,  4,  5,  6,  7,  7,   // Face 1 - triangle strip ( v4,  v5,  v6,  v7)
    8,  8,  9,  10, 11, 11,  // Face 2 - triangle strip ( v8,  v9, v10, v11)
    12, 12, 13, 14, 15, 15,  // Face 3 - triangle strip (v12, v13, v14, v15)
    16, 16, 17, 18, 19, 19,  // Face 4 - triangle strip (v16, v17, v18, v19)
    20, 20, 21, 22, 23       // Face 5 - triangle strip (v20, v21, v22, v23)
};

static const QList<VertexData> planeVertices = {
    { QVector4D(-1.0f, -1.0f, -0.5f, 1.0f), QVector2D(0.0f, 0.0f) },  // Bottom-left
    { QVector4D(1.0f, -1.0f, -0.5f, 1.0f), QVector2D(1.0f, 0.0f) },   // Bottom-right
    { QVector4D(-1.0f, 1.0f, -0.5f, 1.0f), QVector2D(0.0f, 1.0f) },   // Top-left
    { QVector4D(1.0f, 1.0f, -0.5f, 1.0f), QVector2D(1.0f, 1.0f) },    // Top-right
};
static const QList<GLushort> planeIndices = {
    0, 1, 2, 3, 3  // Face 0 - triangle strip ( v0,  v1,  v2,  v3)
};

namespace opengl {
BoxGeometry::BoxGeometry(QVector3D size, QVector3D position)
    : QOpenGLFunctions(), m_indexBuf(QOpenGLBuffer::IndexBuffer), m_size(size), m_position(position)
{
    initializeOpenGLFunctions();

    // Generate 2 VBOs
    m_vertexBuf.create();
    m_indexBuf.create();
}

BoxGeometry::BoxGeometry(QVector3D size, QVector3D position, QPoint uv, QVector3D textureDim, QSize textureSize)
    : BoxGeometry(size, position)
{
    initGeometry(uv.x(), uv.y(), textureDim.x(), textureDim.y(), textureDim.z(), textureSize.width(), textureSize.height());
}

BoxGeometry::~BoxGeometry()
{
    m_vertexBuf.destroy();
    m_indexBuf.destroy();
}

void BoxGeometry::draw(QOpenGLShaderProgram* program)
{
    // Tell OpenGL which VBOs to use
    program->setUniformValue("model_matrix", m_matrix);
    m_vertexBuf.bind();
    m_indexBuf.bind();

    // Offset for position
    quintptr offset = 0;

    // Tell OpenGL programmable pipeline how to locate vertex position data
    int vertexLocation = program->attributeLocation("a_position");
    program->enableAttributeArray(vertexLocation);
    program->setAttributeBuffer(vertexLocation, GL_FLOAT, offset, 4, sizeof(VertexData));

    // Offset for texture coordinate
    offset += sizeof(QVector4D);
    // Tell OpenGL programmable pipeline how to locate vertex texture coordinate data
    int texcoordLocation = program->attributeLocation("a_texcoord");
    program->enableAttributeArray(texcoordLocation);
    program->setAttributeBuffer(texcoordLocation, GL_FLOAT, offset, 2, sizeof(VertexData));

    // Draw cube geometry using indices from VBO 1
    glDrawElements(GL_TRIANGLE_STRIP, m_indecesCount, GL_UNSIGNED_SHORT, nullptr);
}

void BoxGeometry::initGeometry(float u, float v, float width, float height, float depth, float textureWidth, float textureHeight)
{
    const auto faces = boxFaces(m_size, m_position, QPoint(u, v), QVector3D(width, height, depth));
    QList<VertexData> verticesData;
    verticesData.reserve(24);
    for (const auto& face : faces) {
        for (const auto corner : { QPoint(0, 0), QPoint(1, 0), QPoint(0, 1), QPoint(1, 1) }) {
            const auto position = face.origin + corner.x() * face.horizontal + corner.y() * face.vertical;
            const auto uv = face.uv + corner.x() * face.uvHorizontal + corner.y() * face.uvVertical;
            verticesData.append(VertexData(QVector4D(position, 1), QVector2D(uv.x() / textureWidth, 1.f - uv.y() / textureHeight)));
        }
    }

    // Transfer vertex data to VBO 0
    m_vertexBuf.bind();
    m_vertexBuf.allocate(verticesData.constData(), verticesData.size() * sizeof(VertexData));

    // Transfer index data to VBO 1
    m_indexBuf.bind();
    m_indexBuf.allocate(indices.constData(), indices.size() * sizeof(GLushort));
    m_indecesCount = indices.size();
}

void BoxGeometry::rotate(float angle, const QVector3D& vector)
{
    m_matrix.rotate(angle, vector);
}

BoxGeometry* BoxGeometry::Plane()
{
    auto b = new BoxGeometry(QVector3D(), QVector3D());

    // Transfer vertex data to VBO 0
    b->m_vertexBuf.bind();
    b->m_vertexBuf.allocate(planeVertices.constData(), planeVertices.size() * sizeof(VertexData));

    // Transfer index data to VBO 1
    b->m_indexBuf.bind();
    b->m_indexBuf.allocate(planeIndices.constData(), planeIndices.size() * sizeof(GLushort));
    b->m_indecesCount = planeIndices.size();

    return b;
}

void BoxGeometry::scale(const QVector3D& vector)
{
    m_matrix.scale(vector);
}
}  // namespace opengl
