// SPDX-License-Identifier: GPL-3.0-only
#include "SkinEditorDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include "Application.h"
#include "ui/dialogs/skins/SkinCanvas.h"
#include "ui/widgets/ClayWidgets.h"

SkinEditorDialog::SkinEditorDialog(QWidget* parent, MinecraftAccountPtr account, const SkinModel& skin)
    : QDialog(parent), m_account(account), m_document(this), m_previewModel(skin)
{
    setObjectName("SkinEditorDialog");
    setProperty("chromaOwnsScrolling", true);
    setWindowTitle(tr("Skin Studio"));
    setWindowModality(Qt::WindowModal);
    resize(1060, 820);
    setMinimumSize(640, 560);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(6);

    auto* title = new QLabel(tr("Skin Studio"), this);
    m_heading = title;
    auto titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 7);
    titleFont.setBold(true);
    title->setFont(titleFont);
    root->addWidget(title);
    auto* subtitle =
        new QLabel(m_account ? tr("Edit %1’s skin. Changes stay local until you select Apply Skin.").arg(m_account->profileName())
                             : tr("Paint, preview and export your Minecraft skin."),
                   this);
    subtitle->setTextFormat(Qt::PlainText);
    subtitle->setWordWrap(true);
    root->addWidget(subtitle);

    m_editorControls = new QWidget(this);
    auto* editor = new QVBoxLayout(m_editorControls);
    editor->setContentsMargins(0, 0, 0, 0);
    editor->setSpacing(6);
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_editorControls);
    root->addWidget(scroll, 1);
    auto* files = new QGridLayout;
    m_fileLayout = files;
    files->setSpacing(6);
    auto* import = new QPushButton(tr("Import PNG…"), this);
    import->setObjectName("skinImport");
    auto* current = new QPushButton(tr("Current Skin"), this);
    current->setEnabled(m_account && !m_account->accountData()->minecraftProfile.skin.data.isEmpty());
    auto* exportButton = new QPushButton(tr("Export PNG…"), this);
    exportButton->setObjectName("skinExport");
    m_undo = new QPushButton(tr("Undo"), this);
    m_undo->setObjectName("skinUndo");
    m_redo = new QPushButton(tr("Redo"), this);
    m_redo->setObjectName("skinRedo");
    auto* reset = new QPushButton(tr("Reset Changes"), this);
    int fileIndex = 0;
    for (auto* button : { import, current, exportButton, m_undo, m_redo, reset }) {
        button->setFixedHeight(32);
        button->setAutoDefault(false);
        m_fileButtons.append(button);
        files->addWidget(button, fileIndex / 3, fileIndex % 3);
        ++fileIndex;
    }
    editor->addLayout(files);
    connect(import, &QPushButton::clicked, this, &SkinEditorDialog::importSkin);
    connect(current, &QPushButton::clicked, this, &SkinEditorDialog::loadCurrentSkin);
    connect(exportButton, &QPushButton::clicked, this, &SkinEditorDialog::exportSkin);
    connect(m_undo, &QPushButton::clicked, &m_document, &SkinTextureDocument::undo);
    connect(m_redo, &QPushButton::clicked, &m_document, &SkinTextureDocument::redo);
    connect(reset, &QPushButton::clicked, &m_document, &SkinTextureDocument::reset);
    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, this, [this] {
        if (!m_applying)
            m_document.undo();
    });
    connect(new QShortcut(QKeySequence::Redo, this), &QShortcut::activated, this, [this] {
        if (!m_applying)
            m_document.redo();
    });
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] {
        if (!m_applying)
            saveToLibrary();
    });

    auto* split = m_split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    editor->addWidget(split, 1);
    auto* canvasPanel = new ClayPanel(split);
    auto* canvasLayout = new QVBoxLayout(canvasPanel);
    m_canvasLayout = canvasLayout;
    canvasLayout->setContentsMargins(8, 8, 8, 8);
    canvasLayout->setSpacing(6);
    auto* tools = new QHBoxLayout;
    auto* group = new QButtonGroup(this);
    const QStringList toolNames{ tr("Brush"), tr("Eraser"), tr("Pick Color"), tr("Pan") };
    m_canvas = new SkinCanvas(&m_document, canvasPanel);
    m_canvas->setObjectName("skinCanvas");
    for (int i = 0; i < toolNames.size(); ++i) {
        auto* button = new QPushButton(toolNames[i], canvasPanel);
        button->setFixedHeight(32);
        button->setAutoDefault(false);
        button->setCheckable(true);
        button->setChecked(i == 0);
        group->addButton(button, i);
        tools->addWidget(button);
    }
    group->button(SkinCanvas::Eraser)->setToolTip(tr("Erase outer-layer pixels. Minecraft base layers must remain opaque."));
    connect(group, &QButtonGroup::idClicked, this, [this](int id) { m_canvas->setTool(SkinCanvas::Tool(id)); });
    canvasLayout->addLayout(tools);
    auto* colors = new QHBoxLayout;
    m_colorButton = new QPushButton(canvasPanel);
    m_colorButton->setFixedHeight(28);
    m_colorButton->setAutoDefault(false);
    m_colorButton->setAccessibleName(tr("Brush color"));
    colors->addWidget(m_colorButton);
    connect(m_colorButton, &QPushButton::clicked, this, [this] {
        const auto color = QColorDialog::getColor(m_color, this, tr("Brush color"), QColorDialog::ShowAlphaChannel);
        if (color.isValid())
            setColor(color);
    });
    auto* swatches = new QGridLayout;
    m_paletteLayout = swatches;
    swatches->setSpacing(4);
    int swatchIndex = 0;
    for (const QColor& color : { QColor("#ffffff"), QColor("#20242d"), QColor("#e8b798"), QColor("#754f38"), QColor("#7f9cca"),
                                 QColor("#8bae7a"), QColor("#b7a5f5"), QColor("#dc7781") }) {
        auto* swatch = new QToolButton(canvasPanel);
        swatch->setObjectName("skinPaletteSwatch");
        // Form-button padding would leave no room for the color icon at this
        // compact size. Preserve the themed border/focus states, with a 28px
        // total target (24px content plus the two-pixel border on each side).
        swatch->setStyleSheet(
            "QToolButton#skinPaletteSwatch { padding: 0; min-width: 24px; max-width: 24px; "
            "min-height: 24px; max-height: 24px; border-radius: 8px; }");
        swatch->setFixedSize(28, 28);
        swatch->setIconSize(QSize(18, 18));
        m_swatches.append(swatch);
        QPixmap icon(18, 18);
        icon.fill(color);
        swatch->setIcon(QIcon(icon));
        swatch->setToolTip(color.name());
        swatch->setAccessibleName(tr("Use color %1").arg(color.name()));
        swatches->addWidget(swatch, swatchIndex / 4, swatchIndex % 4);
        ++swatchIndex;
        connect(swatch, &QToolButton::clicked, this, [this, color] { setColor(color); });
    }
    colors->addLayout(swatches);
    colors->addStretch();
    canvasLayout->addLayout(colors);
    auto* regions = new QHBoxLayout;
    auto* part = new ClayComboBox(canvasPanel);
    part->setObjectName("skinRegion");
    part->setAccessibleName(tr("Body region to edit"));
    part->addItems({ tr("All body regions"), tr("Head"), tr("Torso"), tr("Right arm"), tr("Left arm"), tr("Right leg"), tr("Left leg") });
    auto* layer = new ClayComboBox(canvasPanel);
    layer->setObjectName("skinLayer");
    layer->setAccessibleName(tr("Skin layer to edit"));
    layer->addItems({ tr("Both layers"), tr("Base layer"), tr("Outer layer") });
    auto* brush = new QSpinBox(canvasPanel);
    part->setFixedHeight(32);
    layer->setFixedHeight(32);
    brush->setFixedHeight(32);
    brush->setAccessibleName(tr("Brush size in pixels"));
    brush->setRange(1, 8);
    brush->setSuffix(tr(" px"));
    regions->addWidget(part, 2);
    regions->addWidget(layer, 2);
    regions->addWidget(brush);
    canvasLayout->addLayout(regions);
    auto regionChanged = [this, part, layer] {
        m_canvas->setRegion(SkinTextureDocument::Part(part->currentIndex() - 1), SkinTextureDocument::Layer(layer->currentIndex()));
    };
    connect(part, &QComboBox::currentIndexChanged, this, regionChanged);
    connect(layer, &QComboBox::currentIndexChanged, this, regionChanged);
    connect(brush, qOverload<int>(&QSpinBox::valueChanged), m_canvas, &SkinCanvas::setBrushSize);
    canvasLayout->addWidget(m_canvas, 1);
    auto* zoom = new QHBoxLayout;
    auto* zoomOut = new QPushButton(tr("−"), canvasPanel);
    zoomOut->setFixedSize(32, 28);
    zoomOut->setAutoDefault(false);
    zoomOut->setAccessibleName(tr("Zoom out texture"));
    auto* zoomIn = new QPushButton(tr("+"), canvasPanel);
    zoomIn->setFixedSize(32, 28);
    zoomIn->setAutoDefault(false);
    zoomIn->setAccessibleName(tr("Zoom in texture"));
    auto* fit = new QPushButton(tr("Fit Texture"), canvasPanel);
    fit->setFixedHeight(28);
    fit->setAutoDefault(false);
    auto* grid = new QCheckBox(tr("Pixel grid"), canvasPanel);
    grid->setChecked(true);
    m_position = new QLabel(tr("64 × 64 PNG"), canvasPanel);
    zoom->addWidget(zoomOut);
    zoom->addWidget(zoomIn);
    zoom->addWidget(fit);
    zoom->addWidget(grid);
    zoom->addStretch();
    zoom->addWidget(m_position);
    canvasLayout->addLayout(zoom);
    connect(zoomOut, &QPushButton::clicked, this, [this] { m_canvas->zoomBy(0.8); });
    connect(zoomIn, &QPushButton::clicked, this, [this] { m_canvas->zoomBy(1.25); });
    connect(fit, &QPushButton::clicked, m_canvas, &SkinCanvas::fitToView);
    connect(grid, &QCheckBox::toggled, m_canvas, &SkinCanvas::setGridVisible);
    connect(m_canvas, &SkinCanvas::colorPicked, this, &SkinEditorDialog::setColor);
    connect(m_canvas, &SkinCanvas::pixelHovered, this,
            [this](QPoint point, QColor color) { m_position->setText(tr("%1, %2 · %3").arg(point.x()).arg(point.y()).arg(color.name())); });

    auto* previewPanel = new ClayPanel(split);
    previewPanel->setMinimumWidth(250);
    auto* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(8, 8, 8, 8);
    previewLayout->setSpacing(6);
    auto* previewTitle = new QLabel(tr("Live preview"), previewPanel);
    auto previewFont = previewTitle->font();
    previewFont.setBold(true);
    previewTitle->setFont(previewFont);
    previewLayout->addWidget(previewTitle);
    m_fallback = new QLabel(previewPanel);
    m_fallback->setMinimumSize(180, 200);
    m_fallback->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_fallback->setAlignment(Qt::AlignCenter);
    m_fallback->setAccessibleName(tr("Front and back skin preview"));
    previewLayout->addWidget(m_fallback, 1);
    if (SkinOpenGLWindow::hasOpenGL()) {
        m_preview = new SkinOpenGLWindow(this, palette().color(QPalette::Base));
        m_previewContainer = QWidget::createWindowContainer(m_preview, previewPanel);
        m_previewContainer->setMinimumSize(200, 200);
        m_previewContainer->setFocusPolicy(Qt::StrongFocus);
        m_previewContainer->setAccessibleName(tr("Interactive 3D skin preview"));
        previewLayout->insertWidget(1, m_previewContainer, 1);
        m_fallback->hide();
        connect(
            m_preview, &SkinOpenGLWindow::renderingFailed, this,
            [this] {
                m_previewFailed = true;
                m_previewContainer->hide();
                m_fallback->show();
                m_status->setText(tr("3D preview is unavailable on this device. The texture editor and PNG export are still available."));
            },
            Qt::QueuedConnection);
    }
    previewPanel->setToolTip(tr("Drag to rotate · Scroll to zoom · Home to reset"));
    m_model = new ClayComboBox(previewPanel);
    m_model->setObjectName("skinModel");
    m_model->setAccessibleName(tr("Minecraft skin model"));
    m_model->addItems({ tr("Classic · Steve"), tr("Slim · Alex") });
    m_model->setFixedHeight(32);
    auto* modelRow = new QHBoxLayout;
    modelRow->addWidget(m_model, 1);
    previewLayout->addLayout(modelRow);
    connect(m_model, &QComboBox::currentIndexChanged, this, [this](int index) { m_document.setModel(SkinModel::Model(index)); });
    auto* visibility = new QMenu(this);
    auto* base = visibility->addAction(tr("Show base layer"));
    auto* outer = visibility->addAction(tr("Show outer layer"));
    base->setCheckable(true);
    base->setChecked(true);
    outer->setCheckable(true);
    outer->setChecked(true);
    visibility->addSeparator();
    const QStringList parts{ tr("Head"), tr("Torso"), tr("Right arm"), tr("Left arm"), tr("Right leg"), tr("Left leg") };
    for (int i = 0; i < parts.size(); ++i) {
        auto* check = visibility->addAction(parts[i]);
        check->setCheckable(true);
        check->setChecked(true);
        check->setEnabled(m_preview != nullptr);
        connect(check, &QAction::toggled, this, [this, i](bool checked) {
            if (m_preview)
                m_preview->setPartVisible(i, checked);
        });
    }
    base->setEnabled(m_preview != nullptr);
    outer->setEnabled(m_preview != nullptr);
    auto layersChanged = [this, base, outer] {
        if (m_preview)
            m_preview->setLayersVisible(base->isChecked(), outer->isChecked());
    };
    connect(base, &QAction::toggled, this, layersChanged);
    connect(outer, &QAction::toggled, this, layersChanged);
    auto* visibilityButton = new QPushButton(tr("Visible parts and layers"), previewPanel);
    visibilityButton->setMenu(visibility);
    visibilityButton->setFixedHeight(32);
    visibilityButton->setAutoDefault(false);
    visibilityButton->setEnabled(m_preview != nullptr);
    previewLayout->addWidget(visibilityButton);
    auto* camera = new QPushButton(tr("Reset"), previewPanel);
    camera->setAccessibleName(tr("Reset 3D view"));
    camera->setFixedHeight(32);
    camera->setAutoDefault(false);
    camera->setEnabled(m_preview != nullptr);
    modelRow->addWidget(camera);
    if (m_preview)
        connect(camera, &QPushButton::clicked, m_preview, &SkinOpenGLWindow::resetView);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    m_status = new QLabel(this);
    m_status->setObjectName("skinEditorStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setText(tr("Alt-click picks a color. Erase affects the outer layer."));
    root->addWidget(m_status);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    m_progress->hide();
    root->addWidget(m_progress);
    auto* footer = new QHBoxLayout;
    footer->addStretch();
    m_save = new QPushButton(tr("Save to Library"), this);
    m_save->setObjectName("skinSave");
    m_apply = new QPushButton(tr("Apply Skin"), this);
    m_apply->setObjectName("skinApply");
    m_close = new QPushButton(tr("Close"), this);
    for (auto* button : { m_save, m_apply, m_close }) {
        button->setFixedHeight(32);
        button->setAutoDefault(false);
    }
    footer->addWidget(m_save);
    footer->addWidget(m_apply);
    footer->addWidget(m_close);
    root->addLayout(footer);
    connect(m_save, &QPushButton::clicked, this, [this] { saveToLibrary(); });
    connect(m_apply, &QPushButton::clicked, this, &SkinEditorDialog::applySkin);
    connect(m_close, &QPushButton::clicked, this, &SkinEditorDialog::reject);
    connect(&m_document, &SkinTextureDocument::changed, this, &SkinEditorDialog::synchronize);
    if (m_account) {
        connect(m_account.get(), &MinecraftAccount::changed, this, &SkinEditorDialog::updateActions);
        connect(m_account.get(), &MinecraftAccount::activityChanged, this, &SkinEditorDialog::updateActions);
    }
    setColor(Qt::white);
    m_document.load(skin.getTexture(), skin.getModel());
    updateActions();
    updateLayout();
}

SkinEditorDialog::~SkinEditorDialog()
{
    // QWidget normally destroys children after C++ members. The canvas and GL
    // provider refer to those members, including during focus-out handling.
    disconnect(&m_document, nullptr, this, nullptr);
    m_canvas->clearFocus();
    m_document.endStroke();
    delete m_previewContainer;
    m_previewContainer = nullptr;
    m_preview = nullptr;
    delete m_editorControls;
    m_editorControls = nullptr;
}

void SkinEditorDialog::synchronize()
{
    if (m_document.isDirty())
        m_savedPath.clear();
    m_previewModel.setModel(m_document.model());
    m_previewModel.setTexture(m_document.image());
    if (m_preview)
        m_preview->updateScene(&m_previewModel);
    m_fallback->setPixmap(QPixmap::fromImage(m_previewModel.getPreview()).scaled(180, 180, Qt::KeepAspectRatio, Qt::FastTransformation));
    const QSignalBlocker blocker(m_model);
    m_model->setCurrentIndex(m_document.model());
    m_undo->setEnabled(m_document.canUndo());
    m_redo->setEnabled(m_document.canRedo());
    setWindowTitle(m_document.isDirty() ? tr("Skin Studio — Unsaved changes") : tr("Skin Studio"));
    updateActions();
}

void SkinEditorDialog::setColor(QColor color)
{
    m_color = color;
    m_canvas->setColor(color);
    QPixmap swatch(18, 18);
    swatch.fill(color);
    m_colorButton->setIcon(QIcon(swatch));
    m_colorButton->setText(color.name());
}

void SkinEditorDialog::showError(const QString& error)
{
    m_status->setText(error);
}

void SkinEditorDialog::importSkin()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Import skin"), {}, tr("PNG images (*.png)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!m_document.importPng(path, &error))
        showError(error);
    else
        m_status->setText(tr("Skin imported. Select Classic or Slim to match its arm width. Undo restores the previous texture."));
}

void SkinEditorDialog::exportSkin()
{
    auto path = QFileDialog::getSaveFileName(this, tr("Export skin"), "skin.png", tr("PNG images (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".png", Qt::CaseInsensitive))
        path += ".png";
    QString error;
    if (!m_document.exportPng(path, &error))
        showError(error);
    else {
        m_document.markSaved();
        m_status->setText(tr("Skin exported to %1").arg(QDir::toNativeSeparators(path)));
    }
}

void SkinEditorDialog::loadCurrentSkin()
{
    if (!m_account)
        return;
    if (m_document.isDirty() &&
        QMessageBox::question(this, tr("Load current skin?"), tr("Discard unsaved edits and load the account’s current skin?"),
                              QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard)
        return;
    const auto& skin = m_account->accountData()->minecraftProfile.skin;
    if (!m_document.load(QImage::fromData(skin.data, "PNG"),
                         skin.variant.compare("slim", Qt::CaseInsensitive) == 0 ? SkinModel::SLIM : SkinModel::CLASSIC))
        showError(tr("The current skin is unavailable. Refresh the account or import a PNG."));
    m_savedPath.clear();
}

QString SkinEditorDialog::saveToLibrary()
{
    if (!m_document.isDirty() && !m_savedPath.isEmpty())
        return m_savedPath;
    QDir directory(APPLICATION->settings()->get("SkinsDir").toString());
    if (!directory.mkpath(".")) {
        showError(tr("The skins folder could not be created. Export the PNG to another folder."));
        return {};
    }
    const auto name = "Edited " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss") + " " +
                      QUuid::createUuid().toString(QUuid::Id128).left(6) + ".png";
    auto path = directory.absoluteFilePath(name);
    QString error;
    if (!m_document.exportPng(path, &error)) {
        showError(error);
        return {};
    }
    m_savedPath = path;
    emit skinSaved(path, m_document.model());
    m_document.markSaved();
    m_status->setText(tr("Saved to your skin library. Select Apply Skin to update Minecraft."));
    return path;
}

void SkinEditorDialog::updateActions()
{
    const bool available = m_account && m_account->accountType() == AccountType::MSA && m_account->hasProfile();
    const bool ready = available && !m_account->isActive() && !m_account->isInUse();
    m_apply->setEnabled(ready && !m_applying && !m_document.image().isNull());
    m_apply->setToolTip(!available ? tr("A Microsoft account with a Minecraft Java profile is required.")
                        : !ready   ? tr("Close Minecraft and wait for account sign-in to finish before applying.")
                                   : tr("Upload this skin to your Minecraft profile."));
    m_save->setEnabled(!m_applying && !m_document.image().isNull());
    m_close->setEnabled(!m_applying);
    m_editorControls->setEnabled(!m_applying);
}

void SkinEditorDialog::applySkin()
{
    const auto path = saveToLibrary();
    if (path.isEmpty())
        return;
    m_applying = true;
    m_progress->show();
    updateActions();
    m_applyTask.reset(new SkinApplyTask(m_account, path, m_document.model()));
    connect(m_applyTask.get(), &Task::status, m_status, &QLabel::setText);
    connect(m_applyTask.get(), &Task::succeeded, this,
            [this] { m_status->setText(tr("Skin applied to Minecraft. Your account preview is up to date.")); });
    connect(m_applyTask.get(), &Task::failed, this, &SkinEditorDialog::showError);
    connect(m_applyTask.get(), &Task::finished, this, [this] {
        m_applying = false;
        m_progress->hide();
        updateActions();
    });
    m_applyTask->start();
}

void SkinEditorDialog::reject()
{
    if (m_applying)
        return;
    if (m_document.isDirty()) {
        const auto answer =
            QMessageBox::question(this, tr("Save skin changes?"), tr("Save your edited skin to the local library before closing?"),
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && saveToLibrary().isEmpty()))
            return;
    }
    QDialog::reject();
}

void SkinEditorDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);
    // Native child windows otherwise remain above a nested inline dialog.
    if (event->type() == QEvent::EnabledChange && m_previewContainer)
        m_previewContainer->setVisible(isEnabled() && !m_previewFailed);
}

void SkinEditorDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateLayout();
}

void SkinEditorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    updateLayout();
}

void SkinEditorDialog::updateLayout()
{
    if (m_heading)
        m_heading->setVisible(!property("chromaInline").toBool());
    if (!m_split || !m_position || m_fileButtons.isEmpty())
        return;
    const int compact = width() < 900 ? 1 : 0;
    if (m_layoutMode == compact)
        return;
    m_layoutMode = compact;
    m_split->setOrientation(compact ? Qt::Vertical : Qt::Horizontal);
    m_position->setVisible(!compact);
    const int columns = compact ? 3 : 6;
    for (int i = 0; i < m_fileButtons.size(); ++i) {
        m_fileLayout->removeWidget(m_fileButtons[i]);
        m_fileLayout->addWidget(m_fileButtons[i], i / columns, i % columns);
    }
    const int paletteColumns = compact ? 4 : 8;
    for (int i = 0; i < m_swatches.size(); ++i) {
        m_paletteLayout->removeWidget(m_swatches[i]);
        m_paletteLayout->addWidget(m_swatches[i], i / paletteColumns, i % paletteColumns);
    }
    m_canvasLayout->removeWidget(m_canvas);
    m_canvasLayout->insertWidget(compact ? 1 : 3, m_canvas, 1);
}
