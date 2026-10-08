// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>
#include "minecraft/skins/SkinTextureDocument.h"

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSpinBox;
class SkinColorWheel;

class SkinPalettePanel : public QWidget {
    Q_OBJECT
   public:
    explicit SkinPalettePanel(SkinTextureDocument* document, QWidget* parent = nullptr);
    void setAllowedRegion(QRegion region);
    const QImage& previewImage() const { return m_previewImage; }

   signals:
    void statusMessage(QString text);
    void previewChanged(const QImage& image);

   private:
    void refresh();
    void sourceChanged();
    void updatePreview();
    void setTarget(QColor color);
    void openColorPicker();
    void apply();
    QColor sourceColor() const;
    QRegion effectiveRegion() const;
    SkinTextureDocument* m_document;
    QRegion m_allowed;
    QComboBox* m_part;
    QComboBox* m_layer;
    QComboBox* m_source;
    QGridLayout* m_swatches;
    QLineEdit* m_target;
    QPushButton* m_choose;
    QSpinBox* m_tolerance;
    QMenu* m_picker;
    SkinColorWheel* m_wheel;
    QLineEdit* m_popupHex;
    QLabel* m_message;
    QPushButton* m_apply;
    QPushButton* m_reset;
    SkinTextureDocument::PixelPatch m_patch;
    QImage m_previewImage;
    int m_changedPixels = 0;
};
