// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QDialog>
#include <memory>

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
class SkinColorWheel;
class QLineEdit;
class QSlider;
class QSpinBox;
class QTabWidget;
class SkinExtrasPanel;
class SkinPalettePanel;
class SkinPaletteCompare;

class SkinEditorDialog : public QDialog, public SkinProvider {
    Q_OBJECT
   public:
    SkinEditorDialog(QWidget* parent, MinecraftAccountPtr account, const SkinModel& skin);
    ~SkinEditorDialog() override;
    SkinModel* getSelectedSkin() override { return &m_previewModel; }
    QHash<QString, QImage> capes() override { return {}; }
    bool loadReference(const QString& path);

   public slots:
    void reject() override;

   signals:
    void skinSaved(QString path, SkinModel::Model model);

   protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    void updateEditingMode();
    void updateVisibility();
    void updateReferenceVisibility();
    void setSharedTool(SkinCanvas::Tool tool);
    void updateEffectControls();
    void previewColorAdjustments();
    void setBodyThroughOverlay(bool enabled);
    void updateLayerHint();
    void applyEffect();
    void importReference();
    void updateReferenceView();
    bool paletteViewActive() const;
    void updatePaletteView();
    void updateTextureSettings();
    void updateInspectorControls();
    void copyMainSelection();
    void pasteMainSelection();
    QRegion effectRegion() const;

    MinecraftAccountPtr m_account;
    SkinTextureDocument m_document;
    SkinModel m_previewModel;
    SkinTextureDocument m_referenceDocument;
    SkinModel m_referenceModel;
    std::unique_ptr<SkinProvider> m_referenceProvider;
    SkinCanvas* m_canvas = nullptr;
    SkinOpenGLWindow* m_preview = nullptr;
    QWidget* m_previewContainer = nullptr;
    QLabel* m_fallback = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_position = nullptr;
    QLabel* m_heading = nullptr;
    QLabel* m_subtitle = nullptr;
    QWidget* m_editorControls = nullptr;
    QSplitter* m_split = nullptr;
    QSplitter* m_referenceSplit = nullptr;
    QWidget* m_referencePanel = nullptr;
    SkinCanvas* m_referenceCanvas = nullptr;
    SkinOpenGLWindow* m_referencePreview = nullptr;
    QComboBox* m_referenceMode = nullptr;
    QComboBox* m_referenceWorkspace = nullptr;
    QComboBox* m_referenceModelChoice = nullptr;
    QToolButton* m_referenceToggle = nullptr;
    QTabWidget* m_inspectorTabs = nullptr;
    SkinExtrasPanel* m_extras = nullptr;
    SkinPalettePanel* m_paletteSwap = nullptr;
    SkinPaletteCompare* m_paletteComparison = nullptr;
    QComboBox* m_effect = nullptr;
    QSlider* m_effectAmount = nullptr;
    QSpinBox* m_effectValue = nullptr;
    QRegion m_effectRegion;
    QPushButton* m_effectApply = nullptr;
    QGridLayout* m_fileLayout = nullptr;
    QGridLayout* m_paletteLayout = nullptr;
    QList<QToolButton*> m_swatches;
    QWidget* m_canvasPanel = nullptr;
    QWidget* m_paintControls = nullptr;
    QWidget* m_paintToolbar = nullptr;
    QWidget* m_workspaceCommands = nullptr;
    QWidget* m_textureControls = nullptr;
    QComboBox* m_textureStyle = nullptr;
    QSpinBox* m_textureStrength = nullptr;
    QWidget* m_previewPanel = nullptr;
    QList<QPushButton*> m_zoomButtons;
    QWidget* m_inspector = nullptr;
    QComboBox* m_editMode = nullptr;
    QComboBox* m_region = nullptr;
    QToolButton* m_showBody = nullptr;
    QToolButton* m_showOuter = nullptr;
    QToolButton* m_referenceBody = nullptr;
    QToolButton* m_referenceOuter = nullptr;
    QList<QToolButton*> m_partButtons;
    QList<QToolButton*> m_referencePartButtons;
    QLabel* m_layerHint = nullptr;
    SkinColorWheel* m_colorWheel = nullptr;
    QLineEdit* m_colorHex = nullptr;
    QSlider* m_opacity = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QList<QPushButton*> m_fileButtons;
    QList<QToolButton*> m_toolButtons;
    int m_layoutMode = -1;
    QComboBox* m_model;
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
    bool m_referenceFailed = false;
    bool m_referenceLoaded = false;
    bool m_bodyThroughOverlay = false;
    bool m_sharingTool = false;
    bool m_activeReference = false;
};
