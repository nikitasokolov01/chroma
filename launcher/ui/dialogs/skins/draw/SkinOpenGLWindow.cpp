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

#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QVector2D>
#include <QVector3D>
#include <QtMath>
#include <functional>

#include "minecraft/skins/SkinModel.h"
#include "rainbow.h"
#include "ui/dialogs/skins/draw/BoxGeometry.h"
#include "ui/dialogs/skins/draw/Scene.h"

SkinOpenGLWindow::SkinOpenGLWindow(SkinProvider* parent, QColor color)
    : QOpenGLWindow(), QOpenGLFunctions(), m_baseColor(color), m_parent(parent)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setDepthBufferSize(24);
    setFormat(format);
}

SkinOpenGLWindow::~SkinOpenGLWindow()
{
    if (!context())
        return;
    // Make sure the context is current when deleting the texture
    // and the buffers.
    makeCurrent();
    // double check if resources were initialized because they are not
    // initialized together with the object
    if (m_scene) {
        delete m_scene;
    }
    if (m_background) {
        delete m_background;
    }
    if (m_backgroundTexture) {
        if (m_backgroundTexture->isCreated()) {
            m_backgroundTexture->destroy();
        }
        delete m_backgroundTexture;
    }
    if (m_modelProgram) {
        if (m_modelProgram->isLinked()) {
            m_modelProgram->release();
        }
        m_modelProgram->removeAllShaders();
        delete m_modelProgram;
    }
    if (m_backgroundProgram) {
        if (m_backgroundProgram->isLinked()) {
            m_backgroundProgram->release();
        }
        m_backgroundProgram->removeAllShaders();
        delete m_backgroundProgram;
    }
    doneCurrent();
}

void SkinOpenGLWindow::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    // Save mouse press position
    m_mousePosition = QVector2D(e->pos());
    m_isMousePressed = true;
}

void SkinOpenGLWindow::mouseMoveEvent(QMouseEvent* event)
{
    // Prevents mouse sticking on Wayland compositors
    if (!(event->buttons() & Qt::MouseButton::LeftButton)) {
        m_isMousePressed = false;
        return;
    }

    if (m_isMousePressed) {
        int dx = event->position().x() - m_mousePosition.x();
        int dy = event->position().y() - m_mousePosition.y();

        m_yaw += dx * 0.5f;
        m_pitch = qBound(-80.f, m_pitch + dy * 0.5f, 80.f);

        // Normalize yaw to keep it manageable
        if (m_yaw > 360.0f)
            m_yaw -= 360.0f;
        else if (m_yaw < 0.0f)
            m_yaw += 360.0f;

        m_mousePosition = QVector2D(event->pos());
        update();  // Trigger a repaint
    }
}

void SkinOpenGLWindow::mouseReleaseEvent([[maybe_unused]] QMouseEvent* e)
{
    m_isMousePressed = false;
}

void SkinOpenGLWindow::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(m_baseColor.redF(), m_baseColor.greenF(), m_baseColor.blueF(), 1);

    if (!initShaders()) {
        emit renderingFailed();
        return;
    }

    generateBackgroundTexture(32, 32, 1);

    QImage skin, cape;
    bool slim = false;
    if (m_parent) {
        if (auto s = m_parent->getSelectedSkin()) {
            skin = s->getTexture();
            slim = s->getModel() == SkinModel::SLIM;
            cape = m_parent->capes().value(s->getCapeId(), {});
        }
    }

    if (m_textureDirty) {
        skin = m_pendingTexture;
        slim = m_model == SkinModel::SLIM;
        m_textureDirty = false;
    }
    if (m_capeDirty) {
        cape = m_pendingCape;
        m_capeDirty = false;
    }
    m_scene = new opengl::Scene(skin, slim, cape);
    m_scene->setLayersVisible(m_baseVisible, m_overlayVisible);
    m_scene->setElytraVisible(m_elytraVisible);
    for (int part = 0; part < 6; ++part)
        m_scene->setPartVisible(part, m_visibleParts & (1u << part));
    m_background = opengl::BoxGeometry::Plane();
    glEnable(GL_TEXTURE_2D);
}

bool SkinOpenGLWindow::initShaders()
{
    // Skin model shaders
    m_modelProgram = new QOpenGLShaderProgram(this);
    // Compile vertex shader
    if (!m_modelProgram->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/vshader_skin_model.glsl"))
        return false;

    // Compile fragment shader
    if (!m_modelProgram->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/fshader.glsl"))
        return false;

    // Link shader pipeline
    if (!m_modelProgram->link())
        return false;

    // Bind shader pipeline for use
    if (!m_modelProgram->bind())
        return false;

    // Background shaders
    m_backgroundProgram = new QOpenGLShaderProgram(this);
    // Compile vertex shader
    if (!m_backgroundProgram->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/vshader_skin_background.glsl"))
        return false;

    // Compile fragment shader
    if (!m_backgroundProgram->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/fshader.glsl"))
        return false;

    // Link shader pipeline
    if (!m_backgroundProgram->link())
        return false;

    // Bind shader pipeline for use (verification)
    if (!m_backgroundProgram->bind())
        return false;
    return true;
}

void SkinOpenGLWindow::resizeGL(int w, int h)
{
    // Calculate aspect ratio
    qreal aspect = qreal(w) / qreal(h ? h : 1);

    const qreal zNear = 15., fov = 45;

    // Reset projection
    m_projection.setToIdentity();

    // Build the reverse z perspective projection matrix
    double radians = qDegreesToRadians(fov / 2.);
    double sine = std::sin(radians);
    if (sine == 0)
        return;
    double cotan = std::cos(radians) / sine;

    m_projection(0, 0) = cotan / aspect;
    m_projection(1, 1) = cotan;
    m_projection(2, 2) = 0.;
    m_projection(3, 2) = -1.;
    m_projection(2, 3) = zNear;
    m_projection(3, 3) = 0.;
}

void SkinOpenGLWindow::paintGL()
{
    if (!m_scene)
        return;
    // GPU uploads only happen inside paintGL, where Qt has made this window's
    // context current. Painting the editor never invokes OpenGL directly.
    if (m_textureDirty) {
        m_scene->setMode(m_model == SkinModel::SLIM);
        m_scene->setSkin(m_pendingTexture);
        m_textureDirty = false;
    }
    if (m_capeDirty) {
        m_scene->setCapeVisible(!m_pendingCape.isNull());
        if (!m_pendingCape.isNull())
            m_scene->setCape(m_pendingCape);
        m_capeDirty = false;
    }
    // Adjust the viewport to account for fractional scaling
    qreal dpr = devicePixelRatio();
    if (dpr != 1.f) {
        QSize scaledSize = size() * dpr;
        glViewport(0, 0, scaledSize.width(), scaledSize.height());
    }

    // Clear color and depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Enable depth buffer
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Enable back face culling
    glEnable(GL_CULL_FACE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_backgroundProgram->bind();
    renderBackground();
    m_backgroundProgram->release();

    // Calculate model view transformation
    QMatrix4x4 matrix;
    float yawRad = qDegreesToRadians(m_yaw);
    float pitchRad = qDegreesToRadians(m_pitch);
    matrix.lookAt(QVector3D(                                       //
                      m_distance * qCos(pitchRad) * qCos(yawRad),  //
                      m_distance * qSin(pitchRad) - 8,             //
                      m_distance * qCos(pitchRad) * qSin(yawRad)),
                  QVector3D(0, -8, 0), QVector3D(0, 1, 0));

    // Set modelview-projection matrix
    m_modelProgram->bind();
    m_modelProgram->setUniformValue("mvp_matrix", m_projection * matrix);

    m_scene->draw(m_modelProgram);
    m_modelProgram->release();

    // Redraw the first frame; this is necessary because the pixel ratio for Wayland fractional scaling is not negotiated properly on the
    // first frame
    if (m_isFirstFrame) {
        m_isFirstFrame = false;
        update();
    }
    emit frameRendered();
}

void SkinOpenGLWindow::updateScene(SkinModel* skin)
{
    if (skin)
        setTexture(skin->getTexture(), skin->getModel());
}
void SkinOpenGLWindow::setTexture(const QImage& texture, SkinModel::Model model)
{
    m_pendingTexture = texture;
    m_model = model;
    m_textureDirty = true;
    update();
}
void SkinOpenGLWindow::updateCape(const QImage& cape)
{
    m_pendingCape = cape;
    m_capeDirty = true;
    update();
}

QColor calculateContrastingColor(const QColor& color)
{
    auto luma = Rainbow::luma(color);
    if (luma < 0.5) {
        constexpr float contrast = 0.05;
        return Rainbow::lighten(color, contrast);
    } else {
        constexpr float contrast = 0.2;
        return Rainbow::darken(color, contrast);
    }
}

QImage generateChessboardImage(int width, int height, int tileSize, QColor baseColor)
{
    QImage image(width, height, QImage::Format_RGB888);
    bool isDarkBase = Rainbow::luma(baseColor) < 0.5;
    float contrast = isDarkBase ? 0.05 : 0.45;
    auto contrastFunc = std::bind(isDarkBase ? Rainbow::lighten : Rainbow::darken, std::placeholders::_1, contrast, 1.0);
    auto white = contrastFunc(baseColor);
    auto black = contrastFunc(calculateContrastingColor(baseColor));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool isWhite = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            image.setPixelColor(x, y, isWhite ? white : black);
        }
    }
    return image;
}

void SkinOpenGLWindow::generateBackgroundTexture(int width, int height, int tileSize)
{
    m_backgroundTexture = new QOpenGLTexture(generateChessboardImage(width, height, tileSize, m_baseColor));
    m_backgroundTexture->setMinificationFilter(QOpenGLTexture::Nearest);
    m_backgroundTexture->setMagnificationFilter(QOpenGLTexture::Nearest);
}

void SkinOpenGLWindow::renderBackground()
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);  // Disable depth buffer writing
    m_backgroundTexture->bind();
    m_backgroundProgram->setUniformValue("texture", 0);
    m_background->draw(m_backgroundProgram);
    m_backgroundTexture->release();
    glDepthMask(GL_TRUE);  // Re-enable depth buffer writing
    glEnable(GL_DEPTH_TEST);
}

void SkinOpenGLWindow::wheelEvent(QWheelEvent* event)
{
    // Adjust distance based on scroll
    int delta = event->angleDelta().y();  // Positive for scroll up, negative for scroll down
    m_distance -= delta * 0.01f;          // Adjust sensitivity factor
    m_distance = qBound(28.f, m_distance, 120.f);
    update();  // Trigger a repaint
}
void SkinOpenGLWindow::setElytraVisible(bool visible)
{
    m_elytraVisible = visible;
    if (m_scene)
        m_scene->setElytraVisible(visible);
    update();
}

void SkinOpenGLWindow::resetView()
{
    m_distance = 48;
    m_yaw = 90;
    m_pitch = 0;
    update();
}

void SkinOpenGLWindow::setLayersVisible(bool base, bool overlay)
{
    m_baseVisible = base;
    m_overlayVisible = overlay;
    if (m_scene)
        m_scene->setLayersVisible(base, overlay);
    update();
}

void SkinOpenGLWindow::setPartVisible(int part, bool visible)
{
    if (part < 0 || part > 5)
        return;
    if (visible)
        m_visibleParts |= (1u << part);
    else
        m_visibleParts &= ~(1u << part);
    if (m_scene)
        m_scene->setPartVisible(part, visible);
    update();
}

void SkinOpenGLWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Home:
        case Qt::Key_R:
            resetView();
            break;
        case Qt::Key_Left:
            m_yaw -= 10;
            break;
        case Qt::Key_Right:
            m_yaw += 10;
            break;
        case Qt::Key_Up:
            m_pitch = qMax(-80.f, m_pitch - 10);
            break;
        case Qt::Key_Down:
            m_pitch = qMin(80.f, m_pitch + 10);
            break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            m_distance = qMax(28.f, m_distance - 4);
            break;
        case Qt::Key_Minus:
            m_distance = qMin(120.f, m_distance + 4);
            break;
        default:
            QOpenGLWindow::keyPressEvent(event);
            return;
    }
    update();
    event->accept();
}

bool SkinOpenGLWindow::hasOpenGL()
{
    QOpenGLContext ctx;
    return ctx.create();
}
