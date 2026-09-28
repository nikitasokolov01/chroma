// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>

#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/skins/SkinApplyTask.h"
#include "minecraft/skins/SkinTextureDocument.h"
#include "ui/dialogs/skins/draw/SkinOpenGLWindow.h"

class QLabel;
class QComboBox;
class QPushButton;
class QProgressBar;
class QSplitter;
class QGridLayout;
class QVBoxLayout;
class QToolButton;
class SkinCanvas;

class SkinEditorDialog : public QDialog, public SkinProvider {
    Q_OBJECT
   public:
    SkinEditorDialog(QWidget* parent, MinecraftAccountPtr account, const SkinModel& skin);
    ~SkinEditorDialog() override;
    SkinModel* getSelectedSkin() override { return &m_previewModel; }
    QHash<QString, QImage> capes() override { return {}; }

   public slots:
    void reject() override;

   signals:
    void skinSaved(QString path, SkinModel::Model model);

   protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

   private:
    void synchronize();
    void setColor(QColor color);
    void importSkin();
    void exportSkin();
    void loadCurrentSkin();
    QString saveToLibrary();
    void applySkin();
    void updateActions();
    void showError(const QString& error);
    void updateLayout();
    void updateToolIcons();

    MinecraftAccountPtr m_account;
    SkinTextureDocument m_document;
    SkinModel m_previewModel;
    SkinCanvas* m_canvas;
    SkinOpenGLWindow* m_preview = nullptr;
    QWidget* m_previewContainer = nullptr;
    QLabel* m_fallback;
    QLabel* m_status;
    QLabel* m_position = nullptr;
    QLabel* m_heading = nullptr;
    QWidget* m_editorControls;
    QSplitter* m_split = nullptr;
    QGridLayout* m_fileLayout = nullptr;
    QGridLayout* m_paletteLayout = nullptr;
    QVBoxLayout* m_canvasLayout = nullptr;
    QList<QPushButton*> m_fileButtons;
    QList<QToolButton*> m_swatches;
    QList<QToolButton*> m_toolButtons;
    int m_layoutMode = -1;
    QComboBox* m_model;
    QPushButton* m_colorButton;
    QPushButton* m_undo;
    QPushButton* m_redo;
    QPushButton* m_save;
    QPushButton* m_apply;
    QPushButton* m_close;
    QProgressBar* m_progress;
    QColor m_color = Qt::white;
    QString m_savedPath;
    shared_qobject_ptr<SkinApplyTask> m_applyTask;
    bool m_applying = false;
    bool m_previewFailed = false;
};
