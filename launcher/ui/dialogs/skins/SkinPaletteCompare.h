// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include "minecraft/skins/SkinModel.h"
#include "minecraft/skins/SkinTextureDocument.h"

class QLabel;
class QPushButton;
class QStackedLayout;
class QResizeEvent;
class SkinOpenGLWindow;
class SkinPaletteFallback;

// A read-only comparison workspace. Its previews deliberately have no editing
// document, so rotating or zooming cannot change the skin or its undo history.
class SkinPaletteCompare : public QWidget {
    Q_OBJECT
   public:
    explicit SkinPaletteCompare(QWidget* parent = nullptr);
    void setTextures(const QImage& current, const QImage& preview, SkinModel::Model model);
    void setLayersVisible(bool base, bool overlay);
    void setPartLayerVisible(int part, SkinTextureDocument::Layer layer, bool visible);
    QSize sizeHint() const override { return { 640, 480 }; }
    QSize minimumSizeHint() const override { return { 0, 0 }; }

   public slots:
    void resetView();

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    void showFallback();
    void fitDefaultView();
    void updateFallbackVisibility();
    SkinOpenGLWindow* m_currentView = nullptr;
    SkinOpenGLWindow* m_previewView = nullptr;
    SkinPaletteFallback* m_currentFallback = nullptr;
    SkinPaletteFallback* m_previewFallback = nullptr;
    QStackedLayout* m_currentStack = nullptr;
    QStackedLayout* m_previewStack = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_reset = nullptr;
    bool m_syncingCamera = false;
    bool m_autoFit = true;
    bool m_baseVisible = true;
    bool m_outerVisible = true;
    unsigned m_baseParts = 0x3f;
    unsigned m_outerParts = 0x3f;
};
