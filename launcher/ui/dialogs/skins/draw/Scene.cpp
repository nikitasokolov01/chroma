
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

#include "ui/dialogs/skins/draw/Scene.h"

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWindow>

namespace opengl {
Scene::Scene(const QImage& skin, bool slim, const QImage& cape) : QOpenGLFunctions(), m_slim(slim), m_capeVisible(!cape.isNull())
{
    initializeOpenGLFunctions();
    for (int model = 0; model < 2; ++model) {
        for (const auto& box : skinBoxes(model == 1))
            m_parts[model].append(new BoxGeometry(box.size, box.center, box.uv, box.textureSize));
    }

    m_cape = new opengl::BoxGeometry(QVector3D(10, 16, 1), QVector3D(0, -8, 2.5), QPoint(0, 0), QVector3D(10, 16, 1), QSize(64, 32));
    m_cape->rotate(10.8, QVector3D(1, 0, 0));
    m_cape->rotate(180, QVector3D(0, 1, 0));

    auto leftWing =
        new opengl::BoxGeometry(QVector3D(12, 22, 4), QVector3D(0, -13, -2), QPoint(22, 0), QVector3D(10, 20, 2), QSize(64, 32));
    leftWing->rotate(15, QVector3D(1, 0, 0));
    leftWing->rotate(15, QVector3D(0, 0, 1));
    leftWing->rotate(1, QVector3D(1, 0, 0));
    auto rightWing =
        new opengl::BoxGeometry(QVector3D(12, 22, 4), QVector3D(0, -13, -2), QPoint(22, 0), QVector3D(10, 20, 2), QSize(64, 32));
    rightWing->scale(QVector3D(-1, 1, 1));
    rightWing->rotate(15, QVector3D(1, 0, 0));
    rightWing->rotate(15, QVector3D(0, 0, 1));
    rightWing->rotate(1, QVector3D(1, 0, 0));
    m_elytra << leftWing << rightWing;

    // texture init
    QImage emptyTexture(64, 64, QImage::Format_ARGB32);
    emptyTexture.fill(Qt::transparent);
    m_skinTexture = new QOpenGLTexture(skin.isNull() ? emptyTexture : skin.mirrored());
    m_skinTexture->setMinificationFilter(QOpenGLTexture::Nearest);
    m_skinTexture->setMagnificationFilter(QOpenGLTexture::Nearest);

    m_capeTexture = new QOpenGLTexture(cape.isNull() ? emptyTexture : cape.mirrored());
    m_capeTexture->setMinificationFilter(QOpenGLTexture::Nearest);
    m_capeTexture->setMagnificationFilter(QOpenGLTexture::Nearest);
    emptyTexture.fill(Qt::black);
    m_selectionTexture = new QOpenGLTexture(emptyTexture);
    m_selectionTexture->setMinificationFilter(QOpenGLTexture::Nearest);
    m_selectionTexture->setMagnificationFilter(QOpenGLTexture::Nearest);
}
Scene::~Scene()
{
    for (const auto& array : { m_parts[0], m_parts[1], m_elytra })
        qDeleteAll(array);
    delete m_cape;

    m_skinTexture->destroy();
    delete m_skinTexture;

    m_capeTexture->destroy();
    delete m_capeTexture;
    delete m_selectionTexture;
}

void Scene::draw(QOpenGLShaderProgram* program, QOpenGLShaderProgram* gridProgram, bool bodyThroughOverlay)
{
    m_selectionTexture->bind(1);
    m_skinTexture->bind(0);
    const auto& parts = m_parts[m_slim ? 1 : 0];
    for (int layer = 0; layer < 2; ++layer) {
        if (!(layer == 0 ? m_baseVisible : m_overlayVisible))
            continue;
        const unsigned mask = layer == 0 ? m_baseParts : m_outerParts;
        auto drawLayer = [&](QOpenGLShaderProgram* shader) {
            for (int part = 0; part < 6; ++part) {
                if (mask & (1u << part)) {
                    const float opacity = layer == 1 && bodyThroughOverlay && m_baseVisible && (m_baseParts & (1u << part)) ? .5f : 1.f;
                    if (shader == program)
                        shader->setUniformValue("layerOpacity", opacity);
                    else
                        shader->setUniformValue("gridOpacity", (layer == 0 ? .48f : .68f) * opacity);
                    parts[layer * 6 + part]->draw(shader);
                }
            }
        };
        program->bind();
        program->setUniformValue("texture", 0);
        program->setUniformValue("selectedPixels", 1);
        program->setUniformValue("showSelection", 1.f);
        drawLayer(program);
        if (gridProgram) {
            // Draw each grid after its own texture, so translucent Outer pixels
            // blend over the Base grid and opaque pixels cover it naturally.
            // The shared face triangles provide back-face culling and exact
            // Classic/Slim texel boundaries, including empty outer surfaces.
            gridProgram->bind();
            gridProgram->setUniformValue("texture", 0);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.f, -1.f);
            drawLayer(gridProgram);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            gridProgram->release();
        }
    }
    program->bind();
    program->setUniformValue("layerOpacity", 1.f);
    program->setUniformValue("showSelection", 0.f);
    m_selectionTexture->release(1);
    glActiveTexture(GL_TEXTURE0);
    m_skinTexture->release();
    if (m_capeVisible) {
        m_capeTexture->bind();
        program->setUniformValue("texture", 0);
        if (!m_elytraVisible) {
            m_cape->draw(program);
        } else {
            glDisable(GL_CULL_FACE);
            for (auto e : m_elytra) {
                e->draw(program);
            }
            glEnable(GL_CULL_FACE);
        }
        m_capeTexture->release();
    }
}

void updateTexture(QOpenGLTexture* texture, const QImage& img)
{
    if (texture) {
        if (texture->isBound())
            texture->release();
        texture->destroy();
        texture->create();
        texture->setSize(img.width(), img.height());
        texture->setData(img);
        texture->setMinificationFilter(QOpenGLTexture::Nearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    }
}

void Scene::setSkin(const QImage& skin)
{
    updateTexture(m_skinTexture, skin.mirrored());
}

void Scene::setSelection(const QRegion& selection)
{
    QImage mask(64, 64, QImage::Format_RGBA8888);
    mask.fill(Qt::black);
    for (const auto& rect : selection.intersected(mask.rect()))
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x)
                mask.setPixelColor(x, y, Qt::white);
    updateTexture(m_selectionTexture, mask.mirrored());
}

void Scene::setMode(bool slim)
{
    m_slim = slim;
}
void Scene::setCape(const QImage& cape)
{
    updateTexture(m_capeTexture, cape.mirrored());
}
void Scene::setCapeVisible(bool visible)
{
    m_capeVisible = visible;
}
void Scene::setElytraVisible(bool elytraVisible)
{
    m_elytraVisible = elytraVisible;
}
void Scene::setLayersVisible(bool base, bool overlay)
{
    m_baseVisible = base;
    m_overlayVisible = overlay;
}
void Scene::setPartVisible(int part, bool visible)
{
    setPartLayerVisible(part, SkinTextureDocument::Both, visible);
}
void Scene::setPartLayerVisible(int part, SkinTextureDocument::Layer layer, bool visible)
{
    if (part < 0 || part > 5)
        return;
    auto set = [part, visible](unsigned& mask) {
        if (visible)
            mask |= (1u << part);
        else
            mask &= ~(1u << part);
    };
    if (layer != SkinTextureDocument::Overlay)
        set(m_baseParts);
    if (layer != SkinTextureDocument::Base)
        set(m_outerParts);
}
}  // namespace opengl
