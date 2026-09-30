// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QPointer>
#include <QWidget>
#include "minecraft/skins/SkinExtrasLibrary.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

class SkinExtrasPanel : public QWidget {
    Q_OBJECT
   public:
    explicit SkinExtrasPanel(SkinTextureDocument* document, QWidget* parent = nullptr, QString directory = {});
    void setReferenceDocument(SkinTextureDocument* document);
    void setVisibleParts(unsigned parts) { m_visibleParts = parts & 0x3f; }
   signals:
    void statusMessage(QString text);
    void inventorySaved();

   private:
    void refresh(const QString& select = {});
    void save();
    void apply();
    void remove();
    void report(const QString& text);
    QRegion sourceRegion(const SkinTextureDocument* document) const;
    SkinTextureDocument* m_document;
    QPointer<SkinTextureDocument> m_reference;
    SkinExtrasLibrary m_library;
    QComboBox* m_source;
    QComboBox* m_part;
    QComboBox* m_layer;
    QLineEdit* m_name;
    QListWidget* m_entries;
    QCheckBox* m_replace;
    QLabel* m_message;
    QPushButton* m_apply;
    QPushButton* m_remove;
    QToolButton* m_saveToggle;
    QWidget* m_saveForm;
    unsigned m_visibleParts = 0x3f;
};
