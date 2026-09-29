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

#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointer>
#include <QVector2D>
#include "minecraft/skins/SkinModel.h"
#include "ui/dialogs/skins/SkinCanvas.h"
#include "ui/dialogs/skins/draw/BoxGeometry.h"
#include "ui/dialogs/skins/draw/Scene.h"

class SkinProvider {
   public:
    virtual ~SkinProvider() = default;
    virtual SkinModel* getSelectedSkin() = 0;
    virtual QHash<QString, QImage> capes() = 0;
};
// Kept under its existing name for callers; a widget framebuffer lets Qt
// composite the preview correctly inside scrolling and stacked inline pages.
class SkinOpenGLWindow : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

   public:
    SkinOpenGLWindow(SkinProvider* provider, QColor color, QWidget* parent = nullptr);
    virtual ~SkinOpenGLWindow();

    void updateScene(SkinModel* skin);
    void setTexture(const QImage& texture, SkinModel::Model model);
    void updateCape(const QImage& cape);
    void setElytraVisible(bool visible);
    void resetView();
    void setLayersVisible(bool base, bool overlay);
    void setPartVisible(int part, bool visible);
    void setPartLayerVisible(int part, SkinTextureDocument::Layer layer, bool visible);
    bool partLayerVisible(int part, SkinTextureDocument::Layer layer) const;
    void setDocument(SkinTextureDocument* document);
    void setEditingEnabled(bool enabled);
    void setTool(SkinCanvas::Tool tool);
    void setColor(QColor color) { m_color = color; }
    void setBrushSize(int size) { m_brushSize = qBound(1, size, 8); }
    void setRegion(SkinTextureDocument::Part part, SkinTextureDocument::Layer layer);
    std::optional<opengl::SkinPick> pickAt(QPointF position) const;

    static bool hasOpenGL();

   signals:
    void renderingFailed();
    void colorPicked(QColor color);
    void toolChanged(SkinCanvas::Tool tool);
    void pixelHovered(QPoint pixel, QColor color);
    // Signals completed drawing. Read grabFramebuffer() outside this callback
    // because a widget framebuffer readback may itself invoke paintGL().
    void frameRendered();

   protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hideEvent(QHideEvent* event) override;

    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    bool initShaders();

    void generateBackgroundTexture(int width, int height, int tileSize);
    void renderBackground();

   private:
    void cleanupGL();
    QVector3D cameraEye() const;
    void finishStroke();
    void updateCursor();
    void paintTo(QPointF position);
    void applyTool(QPointF position);
    QMetaObject::Connection m_contextCleanup;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLShaderProgram* m_modelProgram = nullptr;
    QOpenGLShaderProgram* m_backgroundProgram = nullptr;
    opengl::Scene* m_scene = nullptr;

    QMatrix4x4 m_projection;

    QVector2D m_mousePosition;

    Qt::MouseButton m_rotateButton = Qt::NoButton;
    QPointer<SkinTextureDocument> m_document;
    QMetaObject::Connection m_documentChanged;
    bool m_editingEnabled = false;
    bool m_painting = false;
    SkinCanvas::Tool m_tool = SkinCanvas::Brush;
    QColor m_color = Qt::white;
    int m_brushSize = 1;
    SkinTextureDocument::Part m_part = SkinTextureDocument::All;
    SkinTextureDocument::Layer m_layer = SkinTextureDocument::Base;
    QPointF m_lastPaintPosition;
    std::optional<opengl::SkinPick> m_lastPaintPick;
    float m_distance = 48;
    float m_yaw = 90;   // Horizontal rotation angle
    float m_pitch = 0;  // Vertical rotation angle

    bool m_isFirstFrame = true;

    opengl::BoxGeometry* m_background = nullptr;
    QOpenGLTexture* m_backgroundTexture = nullptr;
    QColor m_baseColor;
    SkinProvider* m_parent = nullptr;
    QImage m_pendingTexture;
    QImage m_pendingCape;
    SkinModel::Model m_model = SkinModel::CLASSIC;
    bool m_textureDirty = false;
    bool m_capeDirty = false;
    bool m_baseVisible = true;
    bool m_overlayVisible = true;
    bool m_elytraVisible = false;
    unsigned m_baseParts = 0x3f;
    unsigned m_outerParts = 0x3f;
};
