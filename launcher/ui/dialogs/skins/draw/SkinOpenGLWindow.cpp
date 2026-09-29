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

#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QSignalBlocker>
#include <QVector2D>
#include <QVector3D>
#include <QtMath>
#include <functional>

#include "minecraft/skins/SkinModel.h"
#include "rainbow.h"
#include "ui/dialogs/skins/draw/BoxGeometry.h"
#include "ui/dialogs/skins/draw/Scene.h"

SkinOpenGLWindow::SkinOpenGLWindow(SkinProvider* provider, QColor color, QWidget* parent)
    : QOpenGLWidget(parent), QOpenGLFunctions(), m_baseColor(color), m_parent(provider)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setDepthBufferSize(24);
    setFormat(format);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

SkinOpenGLWindow::~SkinOpenGLWindow()
{
    finishStroke();
    cleanupGL();
}

void SkinOpenGLWindow::cleanupGL()
{
    // Reparenting an inline page can replace its top-level context. Release
    // every resource with its original context current, then rebuild on show.
    disconnect(m_contextCleanup);
    const bool hadScene = m_scene != nullptr;
    if (context())
        makeCurrent();
    delete m_scene;
    m_scene = nullptr;
    delete m_background;
    m_background = nullptr;
    delete m_backgroundTexture;
    m_backgroundTexture = nullptr;
    delete m_modelProgram;
    m_modelProgram = nullptr;
    delete m_backgroundProgram;
    m_backgroundProgram = nullptr;
    m_vertexArray.destroy();
    if (context())
        doneCurrent();
    m_textureDirty = m_textureDirty || hadScene;
    m_capeDirty = m_capeDirty || hadScene;
    m_isFirstFrame = true;
}

void SkinOpenGLWindow::setDocument(SkinTextureDocument* document)
{
    finishStroke();
    disconnect(m_documentChanged);
    m_document = document;
    if (document) {
        m_documentChanged = connect(document, &SkinTextureDocument::changed, this, [this] {
            if (m_document)
                setTexture(m_document->image(), m_document->model());
        });
        setTexture(document->image(), document->model());
    }
    updateCursor();
}

void SkinOpenGLWindow::setEditingEnabled(bool enabled)
{
    finishStroke();
    m_rotateButton = Qt::NoButton;
    m_editingEnabled = enabled;
    updateCursor();
}

void SkinOpenGLWindow::setTool(SkinCanvas::Tool tool)
{
    if (m_tool == tool)
        return;
    finishStroke();
    m_rotateButton = Qt::NoButton;
    m_tool = tool;
    updateCursor();
    emit toolChanged(tool);
}

void SkinOpenGLWindow::setRegion(SkinTextureDocument::Part part, SkinTextureDocument::Layer layer)
{
    finishStroke();
    m_part = part;
    m_layer = layer;
}

void SkinOpenGLWindow::updateCursor()
{
    setCursor(m_rotateButton != Qt::NoButton                                  ? Qt::ClosedHandCursor
              : !m_editingEnabled || !m_document || m_tool == SkinCanvas::Pan ? Qt::OpenHandCursor
                                                                              : Qt::CrossCursor);
}

void SkinOpenGLWindow::finishStroke()
{
    const bool painting = m_painting;
    m_painting = false;
    m_lastPaintPick.reset();
    if (painting && m_document)
        m_document->endStroke();
}

QVector3D SkinOpenGLWindow::cameraEye() const
{
    const float yaw = qDegreesToRadians(m_yaw), pitch = qDegreesToRadians(m_pitch);
    return QVector3D(m_distance * qCos(pitch) * qCos(yaw), m_distance * qSin(pitch) - 8, m_distance * qCos(pitch) * qSin(yaw));
}

std::optional<opengl::SkinPick> SkinOpenGLWindow::pickAt(QPointF position) const
{
    if (width() <= 0 || height() <= 0 || !QRectF(rect()).contains(position))
        return {};
    const auto eye = cameraEye();
    const auto forward = (QVector3D(0, -8, 0) - eye).normalized();
    const auto right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
    const auto up = QVector3D::crossProduct(right, forward);
    const float tangent = qTan(qDegreesToRadians(22.5f));
    const float x = (2.f * position.x() / width() - 1.f) * float(width()) / height() * tangent;
    const float y = (1.f - 2.f * position.y() / height()) * tangent;
    return opengl::pickSkin(eye, forward + x * right + y * up, m_model == SkinModel::SLIM, m_baseVisible ? m_baseParts : 0,
                            m_overlayVisible ? m_outerParts : 0, m_part, m_layer, m_document ? m_document->image() : m_pendingTexture);
}

void SkinOpenGLWindow::applyTool(QPointF position)
{
    if (!m_document)
        return;
    const auto pick = pickAt(position);
    if (!pick) {
        m_lastPaintPick.reset();
        return;
    }
    emit pixelHovered(pick->pixel, m_document->image().pixelColor(pick->pixel));
    if (m_tool == SkinCanvas::Eyedropper) {
        emit colorPicked(m_document->image().pixelColor(pick->pixel));
        return;
    }
    if (m_lastPaintPick && m_lastPaintPick->pixel == pick->pixel && m_lastPaintPick->faceRect == pick->faceRect)
        return;
    m_lastPaintPick = pick;
    // Keep a face's brush square inside that face. Adjacent UV rectangles in
    // the PNG are not necessarily adjacent surfaces on the 3D model.
    const auto previousKey = m_document->image().cacheKey();
    QSignalBlocker blocker(m_document);
    const QRect brush(pick->pixel - QPoint((m_brushSize - 1) / 2, (m_brushSize - 1) / 2), QSize(m_brushSize, m_brushSize));
    const auto pixels = brush.intersected(pick->faceRect);
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x)
            m_document->paintPixel(QPoint(x, y), m_color, 1, pick->part, pick->layer, m_tool == SkinCanvas::Eraser);
    blocker.unblock();
    if (m_document->image().cacheKey() != previousKey)
        emit m_document->changed();
}

void SkinOpenGLWindow::paintTo(QPointF position)
{
    // Interpolate on the screen, then pick each sample independently: never
    // connect unrelated texture regions across a face seam or a model gap.
    if (!m_document)
        return;
    const auto previousKey = m_document->image().cacheKey();
    QSignalBlocker blocker(m_document);
    const int steps = qMax(1, qCeil(QLineF(m_lastPaintPosition, position).length()));
    for (int i = 1; i <= steps; ++i)
        applyTool(m_lastPaintPosition + (position - m_lastPaintPosition) * (qreal(i) / steps));
    m_lastPaintPosition = position;
    blocker.unblock();
    if (m_document->image().cacheKey() != previousKey)
        emit m_document->changed();
}

void SkinOpenGLWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton && event->button() != Qt::MiddleButton) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    finishStroke();
    if (event->button() != Qt::LeftButton || !m_editingEnabled || !m_document || m_tool == SkinCanvas::Pan) {
        m_rotateButton = event->button();
        m_mousePosition = QVector2D(event->position());
        updateCursor();
    } else {
        m_rotateButton = Qt::NoButton;
        m_lastPaintPosition = event->position();
        if (event->modifiers() & Qt::AltModifier) {
            const auto pick = pickAt(event->position());
            if (pick)
                emit colorPicked(m_document->image().pixelColor(pick->pixel));
            event->accept();
            return;
        }
        if (m_tool != SkinCanvas::Eyedropper) {
            m_document->beginStroke();
            m_painting = true;
        }
        applyTool(event->position());
    }
    event->accept();
}

void SkinOpenGLWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_painting && !(event->buttons() & Qt::LeftButton))
        finishStroke();
    if (m_rotateButton != Qt::NoButton && !(event->buttons() & m_rotateButton)) {
        m_rotateButton = Qt::NoButton;
        updateCursor();
    }
    if (m_rotateButton != Qt::NoButton) {
        const auto movement = QVector2D(event->position()) - m_mousePosition;
        m_yaw = std::fmod(m_yaw + movement.x() * .5f + 360.f, 360.f);
        m_pitch = qBound(-80.f, m_pitch + movement.y() * .5f, 80.f);
        m_mousePosition = QVector2D(event->position());
        update();
    } else if (m_painting) {
        paintTo(event->position());
    } else if (m_editingEnabled && m_document) {
        const auto pick = pickAt(event->position());
        if (pick)
            emit pixelHovered(pick->pixel, m_document->image().pixelColor(pick->pixel));
    }
    event->accept();
}

void SkinOpenGLWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_painting)
            paintTo(event->position());
        finishStroke();
    }
    if (event->button() == m_rotateButton)
        m_rotateButton = Qt::NoButton;
    updateCursor();
    event->accept();
}

void SkinOpenGLWindow::focusOutEvent(QFocusEvent* event)
{
    finishStroke();
    m_rotateButton = Qt::NoButton;
    updateCursor();
    QOpenGLWidget::focusOutEvent(event);
}

void SkinOpenGLWindow::hideEvent(QHideEvent* event)
{
    finishStroke();
    m_rotateButton = Qt::NoButton;
    updateCursor();
    QOpenGLWidget::hideEvent(event);
}

void SkinOpenGLWindow::initializeGL()
{
    initializeOpenGLFunctions();
    m_contextCleanup = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &SkinOpenGLWindow::cleanupGL, Qt::DirectConnection);

    if (!initShaders()) {
        emit renderingFailed();
        return;
    }

    generateBackgroundTexture(32, 32, 1);
    m_vertexArray.create();
    if (m_vertexArray.isCreated())
        m_vertexArray.bind();

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
    m_pendingTexture = skin;
    m_model = slim ? SkinModel::SLIM : SkinModel::CLASSIC;
    m_pendingCape = cape;
    m_scene = new opengl::Scene(skin, slim, cape);
    m_scene->setLayersVisible(m_baseVisible, m_overlayVisible);
    m_scene->setElytraVisible(m_elytraVisible);
    for (int part = 0; part < 6; ++part) {
        m_scene->setPartLayerVisible(part, SkinTextureDocument::Base, m_baseParts & (1u << part));
        m_scene->setPartLayerVisible(part, SkinTextureDocument::Overlay, m_outerParts & (1u << part));
    }
    m_background = opengl::BoxGeometry::Plane();
    if (m_vertexArray.isCreated())
        m_vertexArray.release();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
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
    // QOpenGLWidget renders to its own FBO, never the top-level window's
    // default target. Reestablish state after Qt paints other widget content.
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    const QSize pixels = size() * devicePixelRatioF();
    glViewport(0, 0, pixels.width(), pixels.height());
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClearColor(m_baseColor.redF(), m_baseColor.greenF(), m_baseColor.blueF(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_scene)
        return;
    glActiveTexture(GL_TEXTURE0);
    if (m_vertexArray.isCreated())
        m_vertexArray.bind();
    // GPU uploads only happen inside paintGL, where Qt has made this widget's
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
    // Enable depth buffer
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Enable back face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    // Keep the checkerboard-backed preview opaque for Qt's compositor even
    // when an outer skin layer uses partial transparency.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_backgroundProgram->bind();
    renderBackground();
    m_backgroundProgram->release();

    // Calculate model view transformation
    QMatrix4x4 matrix;
    matrix.lookAt(cameraEye(), QVector3D(0, -8, 0), QVector3D(0, 1, 0));

    // Set modelview-projection matrix
    m_modelProgram->bind();
    m_modelProgram->setUniformValue("mvp_matrix", m_projection * matrix);

    m_scene->draw(m_modelProgram);
    m_modelProgram->release();
    if (m_vertexArray.isCreated())
        m_vertexArray.release();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

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
    finishStroke();
    // Adjust distance based on scroll
    int delta = event->angleDelta().y();  // Positive for scroll up, negative for scroll down
    m_distance -= delta * 0.01f;          // Adjust sensitivity factor
    m_distance = qBound(28.f, m_distance, 120.f);
    update();  // Trigger a repaint
    event->accept();
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
    finishStroke();
    m_distance = 48;
    m_yaw = 90;
    m_pitch = 0;
    update();
}

void SkinOpenGLWindow::setLayersVisible(bool base, bool overlay)
{
    finishStroke();
    m_baseVisible = base;
    m_overlayVisible = overlay;
    if (m_scene)
        m_scene->setLayersVisible(base, overlay);
    update();
}

void SkinOpenGLWindow::setPartVisible(int part, bool visible)
{
    setPartLayerVisible(part, SkinTextureDocument::Both, visible);
}

void SkinOpenGLWindow::setPartLayerVisible(int part, SkinTextureDocument::Layer layer, bool visible)
{
    if (part < 0 || part > 5)
        return;
    finishStroke();
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
    if (m_scene)
        m_scene->setPartLayerVisible(part, layer, visible);
    update();
}

bool SkinOpenGLWindow::partLayerVisible(int part, SkinTextureDocument::Layer layer) const
{
    if (part < 0 || part > 5)
        return false;
    return ((layer != SkinTextureDocument::Overlay ? m_baseParts : 0) | (layer != SkinTextureDocument::Base ? m_outerParts : 0)) &
           (1u << part);
}

void SkinOpenGLWindow::keyPressEvent(QKeyEvent* event)
{
    if (m_editingEnabled && m_document && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        switch (event->key()) {
            case Qt::Key_B:
                setTool(SkinCanvas::Brush);
                event->accept();
                return;
            case Qt::Key_E:
                setTool(SkinCanvas::Eraser);
                event->accept();
                return;
            case Qt::Key_I:
                setTool(SkinCanvas::Eyedropper);
                event->accept();
                return;
            case Qt::Key_H:
                setTool(SkinCanvas::Pan);
                event->accept();
                return;
            default:
                break;
        }
    }
    finishStroke();
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
            QOpenGLWidget::keyPressEvent(event);
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
