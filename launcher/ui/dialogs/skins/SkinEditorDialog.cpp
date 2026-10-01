// SPDX-License-Identifier: GPL-3.0-only
#include "SkinEditorDialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include "Application.h"
#include "ui/dialogs/skins/SkinCanvas.h"
#include "ui/dialogs/skins/SkinColorWheel.h"
#include "ui/dialogs/skins/SkinExtrasPanel.h"
#include "ui/widgets/ClayWidgets.h"

namespace {
class ReferenceProvider : public SkinProvider {
   public:
    explicit ReferenceProvider(SkinModel* model) : m_model(model) {}
    SkinModel* getSelectedSkin() override { return m_model; }
    QHash<QString, QImage> capes() override { return {}; }

   private:
    SkinModel* m_model;
};

class BodyPartButton : public QToolButton {
   public:
    using QToolButton::QToolButton;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto bounds = QRectF(rect()).adjusted(2, 2, -2, -2);
        QColor fill = palette().color(isChecked() ? QPalette::Highlight : QPalette::AlternateBase);
        if (underMouse() || isDown())
            fill = fill.lighter(115);
        painter.setBrush(fill);
        painter.setPen(QPen(palette().color(isChecked() ? QPalette::Highlight : QPalette::Mid), 1.5));
        painter.drawRoundedRect(bounds, 4, 4);
        if (!isChecked()) {
            painter.setPen(QPen(palette().color(QPalette::Mid), 1.5));
            painter.drawLine(bounds.topLeft() + QPointF(5, 5), bounds.bottomRight() - QPointF(5, 5));
        }
        if (hasFocus()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(palette().color(isChecked() ? QPalette::HighlightedText : QPalette::Text), 1.5, Qt::DashLine));
            painter.drawRoundedRect(bounds.adjusted(3, 3, -3, -3), 2, 2);
        }
    }
};

QPixmap toolPixmap(SkinCanvas::Tool tool, QColor color)
{
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    switch (tool) {
        case SkinCanvas::Brush:
            path.moveTo(10, 14);
            path.lineTo(18, 4);
            path.cubicTo(20, 2, 22, 4, 20, 6);
            path.lineTo(12, 16);
            path.closeSubpath();
            painter.drawPath(path);
            path = {};
            path.moveTo(10, 14);
            path.cubicTo(4, 13, 8, 20, 3, 20);
            path.cubicTo(9, 23, 15, 18, 12, 16);
            path.closeSubpath();
            painter.fillPath(path, color);
            break;
        case SkinCanvas::Eraser:
            painter.drawPolygon(QPolygonF{ { 3, 14 }, { 13, 4 }, { 21, 12 }, { 13, 20 }, { 9, 20 } });
            painter.drawLine(QPointF(8, 9), QPointF(16, 17));
            painter.drawLine(QPointF(10, 21), QPointF(21, 21));
            break;
        case SkinCanvas::Eyedropper:
            painter.drawLine(QPointF(12, 5), QPointF(20, 13));
            painter.drawPolygon(QPolygonF{ { 13, 7 }, { 5, 15 }, { 4, 20 }, { 9, 19 }, { 17, 11 } });
            painter.setBrush(color);
            painter.drawRoundedRect(QRectF(16, 3, 4, 6), 2, 2);
            break;
        case SkinCanvas::Pan:
            painter.drawLine(12, 3, 12, 21);
            painter.drawLine(3, 12, 21, 12);
            painter.drawPolyline(QPolygonF{ { 9, 6 }, { 12, 3 }, { 15, 6 } });
            painter.drawPolyline(QPolygonF{ { 9, 18 }, { 12, 21 }, { 15, 18 } });
            painter.drawPolyline(QPolygonF{ { 6, 9 }, { 3, 12 }, { 6, 15 } });
            painter.drawPolyline(QPolygonF{ { 18, 9 }, { 21, 12 }, { 18, 15 } });
            break;
        case SkinCanvas::Bucket:
            painter.drawPolygon(QPolygonF{ { 4, 11 }, { 12, 3 }, { 20, 11 }, { 12, 19 } });
            painter.drawLine(QPointF(4, 11), QPointF(20, 11));
            painter.drawArc(QRectF(3, 2, 10, 11), 0, 180 * 16);
            path.moveTo(20, 14);
            path.cubicTo(16, 19, 18, 22, 20, 22);
            path.cubicTo(23, 22, 24, 19, 20, 14);
            painter.fillPath(path, color);
            break;
        case SkinCanvas::Select:
            painter.setPen(QPen(color, 1.7, Qt::DashLine));
            painter.drawRect(QRectF(4, 4, 16, 16));
            break;
    }
    return pixmap;
}
}  // namespace

SkinEditorDialog::SkinEditorDialog(QWidget* parent, MinecraftAccountPtr account, const SkinModel& skin)
    : QDialog(parent), m_account(account), m_document(this), m_previewModel(skin), m_referenceDocument(this)
{
    m_document.setObjectName("skinEditingDocument");
    m_referenceDocument.setObjectName("skinReferenceDocument");
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
    m_subtitle = subtitle;
    subtitle->setTextFormat(Qt::PlainText);
    subtitle->setWordWrap(true);
    root->addWidget(subtitle);

    m_editorControls = new QWidget(this);
    auto* editor = new QVBoxLayout(m_editorControls);
    editor->setContentsMargins(0, 0, 0, 0);
    editor->setSpacing(6);
    root->addWidget(m_editorControls, 1);
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

    auto* workspace = new QHBoxLayout;
    workspace->setSpacing(8);
    editor->addLayout(workspace, 1);
    auto* drawing = new ClayPanel(m_editorControls);
    drawing->setMinimumWidth(200);
    auto* drawingLayout = new QVBoxLayout(drawing);
    drawingLayout->setContentsMargins(8, 8, 8, 8);
    drawingLayout->setSpacing(6);
    workspace->addWidget(drawing, 1);

    m_paintControls = new QWidget(drawing);
    auto* paintControlsLayout = new QVBoxLayout(m_paintControls);
    paintControlsLayout->setContentsMargins(0, 0, 0, 0);
    paintControlsLayout->setSpacing(6);
    drawingLayout->addWidget(m_paintControls);
    auto* toolRow = new QHBoxLayout;
    auto* toolbox = new QWidget(drawing);
    toolbox->setObjectName("skinToolbox");
    toolbox->setAccessibleName(tr("Painting tools"));
    auto* tools = new QHBoxLayout(toolbox);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(4);
    auto* group = new QButtonGroup(this);
    const QStringList toolNames{
        tr("Brush"), tr("Eraser"), tr("Pick Color"), tr("Pan / Rotate"), tr("Bucket fill"), tr("Marquee selection")
    };
    const QStringList toolIds{ "skinToolBrush", "skinToolEraser", "skinToolPicker", "skinToolPan", "skinToolBucket", "skinToolSelect" };
    const QList<Qt::Key> toolKeys{ Qt::Key_B, Qt::Key_E, Qt::Key_I, Qt::Key_H, Qt::Key_G, Qt::Key_M };
    const QStringList descriptions{ tr("Paint with the selected color."),
                                    tr("Erase outer-layer pixels. Base layers stay opaque."),
                                    tr("Sample a color. Alt-click also picks a color."),
                                    tr("Drag to rotate the model or pan the texture."),
                                    tr("Fill connected pixels on the editable surface."),
                                    tr("Drag to select pixels. Copy with Ctrl+C; paste with Ctrl+V, then click to place.") };
    for (int i = 0; i < toolNames.size(); ++i) {
        auto* button = new QToolButton(toolbox);
        button->setObjectName(toolIds[i]);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setText(toolNames[i]);
        button->setIconSize(QSize(22, 22));
        button->setStyleSheet(QString("QToolButton#%1 { padding: 0; min-width: 30px; max-width: 30px; "
                                      "min-height: 30px; max-height: 30px; border: 2px solid palette(mid); "
                                      "border-radius: 7px; background: palette(button); } "
                                      "QToolButton#%1:checked { background: palette(highlight); border-color: palette(highlight); } "
                                      "QToolButton#%1:hover { border-color: palette(highlight); } "
                                      "QToolButton#%1:focus { border-color: palette(text); }")
                                  .arg(toolIds[i]));
        button->setFixedSize(34, 34);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setCheckable(true);
        button->setChecked(i == 0);
        button->setToolTip(tr("%1 (%2)\n%3").arg(toolNames[i], QKeySequence(toolKeys[i]).toString(), descriptions[i]));
        button->setAccessibleName(toolNames[i]);
        button->setAccessibleDescription(descriptions[i]);
        group->addButton(button, i);
        m_toolButtons.append(button);
        tools->addWidget(button);
    }
    updateToolIcons();
    toolRow->addWidget(toolbox);
    toolRow->addStretch();
    auto* brush = new QSpinBox(drawing);
    brush->setObjectName("skinBrushSize");
    brush->setAccessibleName(tr("Brush size in pixels"));
    brush->setRange(1, 8);
    brush->setSuffix(tr(" px"));
    brush->setFixedHeight(30);
    brush->setFixedWidth(66);
    drawingLayout->insertLayout(0, toolRow);

    auto* modes = new QHBoxLayout;
    m_editMode = new QComboBox(drawing);
    m_editMode->setObjectName("skinEditMode");
    m_editMode->setAccessibleName(tr("Editing view"));
    m_editMode->addItems({ tr("3D Paint"), tr("2D Texture") });
    m_editMode->setMinimumWidth(0);
    m_editMode->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_editMode->setFixedHeight(32);
    auto* grid = new QCheckBox(tr("Grid"), drawing);
    grid->setObjectName("skinGrid");
    grid->setAccessibleName(tr("Show pixel grid"));
    grid->setToolTip(tr("Show the pixel grid on every visible skin layer."));
    grid->setChecked(true);
    modes->addWidget(m_editMode, 1);
    modes->addWidget(brush);
    modes->addWidget(grid);
    paintControlsLayout->addLayout(modes);

    auto* commands = new QHBoxLayout;
    commands->setSpacing(4);
    auto* referenceImport = new QPushButton(tr("Reference…"), drawing);
    referenceImport->setObjectName("skinImportReference");
    referenceImport->setToolTip(tr("Import a separate reference PNG. The editing skin stays unchanged."));
    m_referenceToggle = new QToolButton(drawing);
    m_referenceToggle->setObjectName("skinReferenceToggle");
    m_referenceToggle->setText(tr("Show"));
    m_referenceToggle->setAccessibleName(tr("Show reference"));
    m_referenceToggle->setCheckable(true);
    m_referenceToggle->setEnabled(false);
    auto* copy = new QPushButton(tr("Copy"), drawing);
    copy->setObjectName("skinCopySelection");
    copy->setToolTip(tr("Copy selected pixels (Ctrl+C)."));
    auto* paste = new QPushButton(tr("Paste"), drawing);
    paste->setObjectName("skinPasteSelection");
    paste->setToolTip(tr("Paste copied pixels (Ctrl+V), then click the editing canvas to place them. Escape cancels."));
    for (auto* button : { referenceImport, copy, paste }) {
        button->setAutoDefault(false);
        button->setFixedHeight(28);
        commands->addWidget(button);
    }
    commands->addWidget(m_referenceToggle);
    m_referenceWorkspace = new QComboBox(drawing);
    m_referenceWorkspace->setObjectName("skinReferenceWorkspace");
    m_referenceWorkspace->setAccessibleName(tr("Active skin workspace"));
    m_referenceWorkspace->addItems({ tr("Editing"), tr("Reference") });
    m_referenceWorkspace->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_referenceWorkspace->setMinimumWidth(86);
    m_referenceWorkspace->setFixedHeight(28);
    m_referenceWorkspace->hide();
    commands->addWidget(m_referenceWorkspace, 1);
    drawingLayout->addLayout(commands);
    connect(referenceImport, &QPushButton::clicked, this, &SkinEditorDialog::importReference);
    connect(copy, &QPushButton::clicked, this, &SkinEditorDialog::copyMainSelection);
    connect(paste, &QPushButton::clicked, this, &SkinEditorDialog::pasteMainSelection);
    connect(m_referenceWorkspace, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == 1)
            m_referenceToggle->setChecked(true);
        updateReferenceView();
    });

    m_referenceSplit = new QSplitter(Qt::Horizontal, drawing);
    m_referenceSplit->setChildrenCollapsible(false);
    drawingLayout->addWidget(m_referenceSplit, 1);
    auto* split = m_split = new QSplitter(Qt::Horizontal, m_referenceSplit);
    split->setChildrenCollapsible(false);
    m_canvasPanel = new QWidget(split);
    auto* canvasLayout = new QVBoxLayout(m_canvasPanel);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(4);
    auto* part = m_region = new QComboBox(m_canvasPanel);
    part->setObjectName("skinRegion");
    part->setAccessibleName(tr("Body region to edit"));
    part->addItems({ tr("All body regions"), tr("Head"), tr("Torso"), tr("Right arm"), tr("Left arm"), tr("Right leg"), tr("Left leg") });
    part->setMinimumWidth(0);
    part->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    part->setFixedHeight(30);
    canvasLayout->addWidget(part);
    m_canvas = new SkinCanvas(&m_document, m_canvasPanel);
    m_canvas->setObjectName("skinCanvas");
    m_canvas->setMinimumSize(150, 100);
    canvasLayout->addWidget(m_canvas, 1);
    auto* zoom = new QHBoxLayout;
    zoom->setSpacing(4);
    auto* zoomOut = new QPushButton(tr("−"), m_canvasPanel);
    auto* zoomIn = new QPushButton(tr("+"), m_canvasPanel);
    for (auto* button : { zoomOut, zoomIn }) {
        button->setFixedSize(28, 26);
        button->setAutoDefault(false);
    }
    zoomOut->setAccessibleName(tr("Zoom out texture"));
    zoomIn->setAccessibleName(tr("Zoom in texture"));
    auto* fit = new QPushButton(tr("Fit"), m_canvasPanel);
    m_zoomButtons = { zoomOut, zoomIn, fit };
    fit->setFixedSize(42, 26);
    fit->setAutoDefault(false);
    fit->setAccessibleName(tr("Fit texture"));
    zoom->addWidget(zoomOut);
    zoom->addWidget(zoomIn);
    zoom->addWidget(fit);
    zoom->addStretch();
    canvasLayout->addLayout(zoom);
    connect(zoomOut, &QPushButton::clicked, this, [this] { m_canvas->zoomBy(0.8); });
    connect(zoomIn, &QPushButton::clicked, this, [this] { m_canvas->zoomBy(1.25); });
    connect(fit, &QPushButton::clicked, m_canvas, &SkinCanvas::fitToView);
    connect(grid, &QCheckBox::toggled, m_canvas, &SkinCanvas::setGridVisible);

    auto* previewPanel = m_previewPanel = new QWidget(split);
    auto* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    m_fallback = new QLabel(previewPanel);
    m_fallback->setMinimumSize(100, 80);
    m_fallback->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_fallback->setAlignment(Qt::AlignCenter);
    m_fallback->setAccessibleName(tr("Front and back skin preview"));
    previewLayout->addWidget(m_fallback, 1);
    if (SkinOpenGLWindow::hasOpenGL()) {
        m_preview = new SkinOpenGLWindow(this, palette().color(QPalette::Base), previewPanel);
        m_preview->setObjectName("skin3DCanvas");
        m_preview->setDocument(&m_document);
        m_previewContainer = m_preview;
        m_preview->setMinimumSize(100, 100);
        m_preview->setFocusPolicy(Qt::StrongFocus);
        m_preview->setAccessibleName(tr("3D skin painting canvas"));
        previewLayout->insertWidget(0, m_preview, 1);
        m_fallback->hide();
        connect(
            m_preview, &SkinOpenGLWindow::renderingFailed, this,
            [this] {
                m_previewFailed = true;
                m_preview->setEditingEnabled(false);
                m_preview->hide();
                m_fallback->show();
                m_editMode->setCurrentIndex(1);
                m_editMode->setEnabled(false);
                synchronize();
                m_status->setText(tr("3D rendering is unavailable. You can edit and export the 2D texture."));
            },
            Qt::QueuedConnection);
        connect(m_preview, &SkinOpenGLWindow::colorPicked, this, &SkinEditorDialog::setColor);
        connect(m_preview, &SkinOpenGLWindow::toolChanged, m_canvas, &SkinCanvas::setTool);
        connect(grid, &QCheckBox::toggled, m_preview, &SkinOpenGLWindow::setGridVisible);
    } else {
        m_editMode->setCurrentIndex(1);
        m_editMode->setEnabled(false);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        setSharedTool(SkinCanvas::Tool(id));
        if (m_activeReference && m_referencePanel && m_referencePanel->isVisible()) {
            if (m_referencePreview && !m_referenceFailed && m_referenceMode->currentIndex() == 0)
                m_referencePreview->setFocus(Qt::ShortcutFocusReason);
            else
                m_referenceCanvas->setFocus(Qt::ShortcutFocusReason);
        } else if (m_preview && m_editMode->currentIndex() == 0) {
            m_preview->setFocus(Qt::ShortcutFocusReason);
        } else {
            m_canvas->setFocus(Qt::ShortcutFocusReason);
        }
    });
    connect(m_canvas, &SkinCanvas::toolChanged, this, &SkinEditorDialog::setSharedTool);
    connect(m_canvas, &SkinCanvas::colorPicked, this, &SkinEditorDialog::setColor);
    connect(brush, qOverload<int>(&QSpinBox::valueChanged), this, [this](int size) {
        m_canvas->setBrushSize(size);
        if (m_preview)
            m_preview->setBrushSize(size);
    });
    connect(part, &QComboBox::currentIndexChanged, this, &SkinEditorDialog::updateVisibility);
    connect(m_editMode, &QComboBox::currentIndexChanged, this, [this] {
        m_document.endStroke();
        updateEditingMode();
    });
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    m_referencePanel = new QWidget(m_referenceSplit);
    m_referencePanel->setObjectName("skinReferencePanel");
    m_referencePanel->setMinimumWidth(150);
    auto* referenceLayout = new QVBoxLayout(m_referencePanel);
    referenceLayout->setContentsMargins(0, 0, 0, 0);
    referenceLayout->setSpacing(4);
    auto* referenceHeader = new QHBoxLayout;
    referenceHeader->setSpacing(3);
    m_referenceMode = new QComboBox(m_referencePanel);
    m_referenceMode->setObjectName("skinReferenceMode");
    m_referenceMode->setAccessibleName(tr("Reference view"));
    m_referenceMode->addItems({ tr("Reference · 3D"), tr("Reference · 2D") });
    m_referenceModelChoice = new QComboBox(m_referencePanel);
    m_referenceModelChoice->setObjectName("skinReferenceModel");
    m_referenceModelChoice->setAccessibleName(tr("Reference model"));
    m_referenceModelChoice->addItems({ tr("Classic"), tr("Slim") });
    for (auto* combo : { m_referenceMode, m_referenceModelChoice }) {
        combo->setMinimumWidth(0);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->setFixedHeight(28);
    }
    referenceHeader->addWidget(m_referenceMode, 3);
    referenceHeader->addWidget(m_referenceModelChoice, 2);
    auto* referenceHide = new QToolButton(m_referencePanel);
    referenceHide->setObjectName("skinHideReference");
    referenceHide->setText(QString::fromUtf8("×"));
    referenceHide->setAccessibleName(tr("Hide reference"));
    referenceHide->setToolTip(tr("Hide reference. Show opens it again."));
    referenceHide->setStyleSheet(
        "QToolButton#skinHideReference { padding: 0; min-width: 20px; max-width: 20px; "
        "min-height: 22px; max-height: 22px; border-radius: 5px; }");
    referenceHeader->addWidget(referenceHide);
    referenceLayout->addLayout(referenceHeader);
    auto* referenceVisibility = new QHBoxLayout;
    referenceVisibility->setSpacing(6);
    auto* referenceBody = new QWidget(m_referencePanel);
    referenceBody->setObjectName("skinReferenceBodySelector");
    referenceBody->setAccessibleName(tr("Visible reference body parts"));
    referenceBody->setFixedSize(64, 92);
    auto* referenceBodyLayout = new QGridLayout(referenceBody);
    referenceBodyLayout->setContentsMargins(8, 0, 8, 0);
    referenceBodyLayout->setSpacing(0);
    const QStringList referenceParts{ tr("Head"), tr("Torso"), tr("Right arm"), tr("Left arm"), tr("Right leg"), tr("Left leg") };
    const QList<QRect> referenceCells{ { 1, 0, 2, 1 }, { 1, 1, 2, 1 }, { 0, 1, 1, 1 }, { 3, 1, 1, 1 }, { 1, 2, 1, 1 }, { 2, 2, 1, 1 } };
    for (int i = 0; i < referenceParts.size(); ++i) {
        auto* button = new BodyPartButton(referenceBody);
        button->setObjectName(QString("skinReferencePart%1").arg(i));
        button->setText(referenceParts[i]);
        button->setAccessibleName(tr("Reference %1").arg(referenceParts[i]));
        button->setAccessibleDescription(tr("Toggle both layers of this reference body part."));
        button->setCheckable(true);
        button->setChecked(true);
        button->setFocusPolicy(Qt::StrongFocus);
        const QSize size(i < 2 ? 24 : 12, i == 0 ? 24 : 34);
        button->setStyleSheet(QString("QToolButton#%1 { padding: 0; border: none; min-width: %2px; max-width: %2px; "
                                      "min-height: %3px; max-height: %3px; }")
                                  .arg(button->objectName())
                                  .arg(size.width())
                                  .arg(size.height()));
        button->setFixedSize(size);
        const auto cell = referenceCells[i];
        referenceBodyLayout->addWidget(button, cell.y(), cell.x(), cell.height(), cell.width());
        m_referencePartButtons.append(button);
        connect(button, &QToolButton::toggled, this, &SkinEditorDialog::updateReferenceVisibility);
    }
    referenceVisibility->addWidget(referenceBody);
    auto* referenceTools = new QVBoxLayout;
    referenceTools->setSpacing(3);
    m_referenceBody = new QToolButton(m_referencePanel);
    m_referenceBody->setObjectName("skinReferenceBody");
    m_referenceBody->setText(tr("Body"));
    m_referenceOuter = new QToolButton(m_referencePanel);
    m_referenceOuter->setObjectName("skinReferenceOuter");
    m_referenceOuter->setText(tr("Outer layer"));
    for (auto* button : { m_referenceBody, m_referenceOuter }) {
        button->setCheckable(true);
        button->setChecked(true);
        button->setAccessibleName(tr("Reference %1 visibility").arg(button->text()));
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setFixedHeight(28);
        button->setStyleSheet(QString("QToolButton#%1 { min-width: 0; min-height: 22px; max-height: 22px; padding: 1px 3px; "
                                      "border: 2px solid palette(mid); border-radius: 6px; } "
                                      "QToolButton#%1:checked { background: palette(highlight); color: palette(highlighted-text); }")
                                  .arg(button->objectName()));
        referenceTools->addWidget(button);
        connect(button, &QToolButton::toggled, this, &SkinEditorDialog::updateReferenceVisibility);
    }
    auto* referenceCopy = new QPushButton(tr("Copy pixels"), m_referencePanel);
    referenceCopy->setObjectName("skinCopyReference");
    referenceCopy->setToolTip(tr("Copy the selected reference pixels (Ctrl+C), then Paste onto the editing skin."));
    referenceCopy->setAutoDefault(false);
    referenceCopy->setFixedHeight(28);
    referenceCopy->setEnabled(false);
    referenceTools->addWidget(referenceCopy);
    referenceVisibility->addLayout(referenceTools, 1);
    referenceLayout->addLayout(referenceVisibility);
    m_referenceCanvas = new SkinCanvas(&m_referenceDocument, m_referencePanel);
    m_referenceCanvas->setObjectName("skinReferenceCanvas");
    m_referenceCanvas->setAccessibleName(tr("Read-only reference texture"));
    m_referenceCanvas->setMinimumSize(100, 70);
    m_referenceCanvas->setReadOnly(true);
    m_referenceCanvas->setRegion(SkinTextureDocument::All, SkinTextureDocument::Both);
    m_referenceCanvas->setTool(SkinCanvas::Brush);
    referenceLayout->addWidget(m_referenceCanvas, 1);
    m_referenceProvider = std::make_unique<ReferenceProvider>(&m_referenceModel);
    if (SkinOpenGLWindow::hasOpenGL()) {
        m_referencePreview = new SkinOpenGLWindow(m_referenceProvider.get(), palette().color(QPalette::Base), m_referencePanel);
        m_referencePreview->setObjectName("skinReference3DCanvas");
        m_referencePreview->setAccessibleName(tr("Read-only reference model"));
        m_referencePreview->setMinimumSize(100, 70);
        m_referencePreview->setFocusPolicy(Qt::StrongFocus);
        m_referencePreview->setDocument(&m_referenceDocument);
        m_referencePreview->setReadOnly(true);
        m_referencePreview->setTool(SkinCanvas::Brush);
        m_referencePreview->setEditingEnabled(true);
        referenceLayout->addWidget(m_referencePreview, 1);
        connect(m_referencePreview, &SkinOpenGLWindow::colorPicked, this, &SkinEditorDialog::setColor);
        connect(
            m_referencePreview, &SkinOpenGLWindow::renderingFailed, this,
            [this] {
                m_referenceFailed = true;
                m_referencePreview->setEditingEnabled(false);
                m_referenceMode->setCurrentIndex(1);
                m_referenceMode->setEnabled(false);
                updateReferenceView();
            },
            Qt::QueuedConnection);
        connect(m_referencePreview, &SkinOpenGLWindow::toolChanged, m_referenceCanvas, &SkinCanvas::setTool);
        connect(grid, &QCheckBox::toggled, m_referencePreview, &SkinOpenGLWindow::setGridVisible);
    } else {
        m_referenceMode->setCurrentIndex(1);
        m_referenceMode->setEnabled(false);
    }
    connect(grid, &QCheckBox::toggled, m_referenceCanvas, &SkinCanvas::setGridVisible);
    connect(m_referenceCanvas, &SkinCanvas::colorPicked, this, &SkinEditorDialog::setColor);
    connect(m_referenceCanvas, &SkinCanvas::toolChanged, this, &SkinEditorDialog::setSharedTool);
    connect(referenceCopy, &QPushButton::clicked, this, [this] {
        const bool copied = m_referencePreview && m_referenceMode->currentIndex() == 0 ? m_referencePreview->copySelection()
                                                                                       : m_referenceCanvas->copySelection();
        m_status->setText(copied ? tr("Reference pixels copied. Select Paste, then click the editing skin to place them.")
                                 : tr("Select reference pixels to copy."));
    });
    connect(&m_referenceDocument, &SkinTextureDocument::selectionChanged, referenceCopy,
            [this, referenceCopy] { referenceCopy->setEnabled(m_referenceDocument.hasSelection()); });
    connect(&m_referenceDocument, &SkinTextureDocument::changed, this, [this] {
        m_referenceModel.setTexture(m_referenceDocument.image());
        m_referenceModel.setModel(m_referenceDocument.model());
    });
    connect(m_referenceModelChoice, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_referenceDocument.setModel(SkinModel::Model(index)); });
    connect(referenceHide, &QToolButton::clicked, this, [this] {
        m_referenceWorkspace->setCurrentIndex(0);
        m_referenceToggle->setChecked(false);
    });
    connect(m_referenceToggle, &QToolButton::toggled, this, &SkinEditorDialog::updateReferenceView);
    connect(m_referenceMode, &QComboBox::currentIndexChanged, this, &SkinEditorDialog::updateReferenceView);
    m_referenceToggle->setStyleSheet(
        "QToolButton#skinReferenceToggle { padding: 0 3px; min-width: 24px; "
        "min-height: 24px; max-height: 24px; border-radius: 6px; } "
        "QToolButton#skinReferenceToggle:checked { background: palette(highlight); color: palette(highlighted-text); }");
    m_referenceSplit->setStretchFactor(0, 3);
    m_referenceSplit->setStretchFactor(1, 2);
    m_referencePanel->hide();

    m_inspector = new ClayPanel(m_editorControls);
    m_inspector->setObjectName("skinInspector");
    m_inspector->setFixedWidth(210);
    auto* inspector = new QVBoxLayout(m_inspector);
    inspector->setContentsMargins(8, 4, 8, 4);
    inspector->setSpacing(4);
    workspace->addWidget(m_inspector);
    m_colorWheel = new SkinColorWheel(m_inspector);
    m_colorWheel->setFixedHeight(180);
    inspector->addWidget(m_colorWheel);
    connect(m_colorWheel, &SkinColorWheel::colorChanged, this, &SkinEditorDialog::setColor);
    m_colorHex = new QLineEdit(m_inspector);
    m_colorHex->setObjectName("skinColorHex");
    m_colorHex->setAccessibleName(tr("Brush color in hexadecimal"));
    m_colorHex->setToolTip(tr("Enter #RRGGBB, or #AARRGGBB including opacity."));
    m_colorHex->setValidator(new QRegularExpressionValidator(QRegularExpression("#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?"), m_colorHex));
    m_colorHex->setFixedHeight(28);
    inspector->addWidget(m_colorHex);
    connect(m_colorHex, &QLineEdit::editingFinished, this, [this] {
        QColor color(m_colorHex->text());
        if (color.isValid()) {
            if (m_colorHex->text().size() == 7)
                color.setAlpha(m_color.alpha());
            setColor(color);
        } else {
            setColor(m_color);
        }
    });
    auto* opacityRow = new QHBoxLayout;
    opacityRow->setSpacing(4);
    auto* opacityLabel = new QLabel(tr("Opacity"), m_inspector);
    m_opacity = new QSlider(Qt::Horizontal, m_inspector);
    m_opacity->setObjectName("skinOpacity");
    m_opacity->setAccessibleName(tr("Brush opacity"));
    m_opacity->setRange(0, 255);
    m_opacity->setValue(255);
    opacityLabel->setBuddy(m_opacity);
    opacityRow->addWidget(opacityLabel);
    opacityRow->addWidget(m_opacity, 1);
    inspector->addLayout(opacityRow);
    connect(m_opacity, &QSlider::valueChanged, this, [this](int alpha) {
        auto color = m_color;
        color.setAlpha(alpha);
        setColor(color);
    });
    m_inspectorTabs = new QTabWidget(m_inspector);
    m_inspectorTabs->setObjectName("skinInspectorTabs");
    m_inspectorTabs->setMinimumSize(0, 40);
    m_inspectorTabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_inspectorTabs->setDocumentMode(true);
    m_inspectorTabs->setStyleSheet(
        "QTabWidget#skinInspectorTabs::pane { border: none; } "
        "QTabWidget#skinInspectorTabs QTabBar::tab { min-width: 0; padding: 5px 4px; }");
    inspector->addWidget(m_inspectorTabs, 1);
    auto* swatches = m_paletteLayout = new QGridLayout;
    swatches->setSpacing(3);
    int swatchIndex = 0;
    for (const QColor& color : { QColor("#ffffff"), QColor("#20242d"), QColor("#e8b798"), QColor("#754f38"), QColor("#7f9cca"),
                                 QColor("#8bae7a"), QColor("#b7a5f5"), QColor("#dc7781") }) {
        auto* swatch = new QToolButton(m_inspector);
        m_swatches.append(swatch);
        swatch->setObjectName("skinPaletteSwatch");
        swatch->setStyleSheet(
            "QToolButton#skinPaletteSwatch { padding: 0; min-width: 14px; max-width: 14px; "
            "min-height: 14px; max-height: 14px; border-radius: 4px; }");
        swatch->setFixedSize(18, 18);
        swatch->setIconSize(QSize(12, 12));
        QPixmap icon(12, 12);
        icon.fill(color);
        swatch->setIcon(QIcon(icon));
        swatch->setToolTip(color.name());
        swatch->setAccessibleName(tr("Use color %1").arg(color.name()));
        swatches->addWidget(swatch, 0, swatchIndex++);
        connect(swatch, &QToolButton::clicked, this, [this, color] { setColor(color); });
    }

    auto* visibilityScroll = new QScrollArea(m_inspector);
    visibilityScroll->setObjectName("skinVisibilityScroll");
    visibilityScroll->setFrameShape(QFrame::NoFrame);
    visibilityScroll->setWidgetResizable(true);
    visibilityScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    visibilityScroll->setMinimumHeight(40);
    auto* visibility = new QWidget;
    auto* visibilityGrid = new QGridLayout(visibility);
    visibilityGrid->setContentsMargins(0, 2, 0, 2);
    visibilityGrid->setVerticalSpacing(3);
    visibilityGrid->setHorizontalSpacing(3);
    visibilityGrid->addLayout(swatches, 0, 0);
    auto* layers = new QHBoxLayout;
    layers->setSpacing(4);
    m_showBody = new QToolButton(visibility);
    m_showBody->setObjectName("skinShowBase");
    m_showBody->setText(tr("Body"));
    m_showBody->setToolTip(tr("Show the body layer. Hide Outer layer to paint the body."));
    m_showOuter = new QToolButton(visibility);
    m_showOuter->setObjectName("skinShowOuter");
    m_showOuter->setText(tr("Outer layer"));
    m_showOuter->setToolTip(tr("Show the outer layer. While visible, painting only changes the outer layer."));
    for (auto* button : { m_showBody, m_showOuter }) {
        button->setCheckable(true);
        button->setChecked(true);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setAccessibleName(button->text());
        button->setAccessibleDescription(button->toolTip());
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(30);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setStyleSheet(QString("QToolButton#%1 { padding: 0 3px; min-width: 0; border: 1px solid palette(mid); "
                                      "border-radius: 7px; background: palette(button); color: palette(button-text); } "
                                      "QToolButton#%1:checked { background: palette(highlight); color: palette(highlighted-text); "
                                      "border-color: palette(highlight); } QToolButton#%1:hover { border-color: palette(text); } "
                                      "QToolButton#%1:focus { border: 2px solid palette(text); }")
                                  .arg(button->objectName()));
        layers->addWidget(button, button == m_showBody ? 2 : 3);
        connect(button, &QToolButton::toggled, this, &SkinEditorDialog::updateVisibility);
    }
    visibilityGrid->addLayout(layers, 1, 0);
    m_layerHint = new QLabel(visibility);
    m_layerHint->setObjectName("skinPaintLayerHint");
    m_layerHint->setWordWrap(true);
    m_layerHint->setAlignment(Qt::AlignCenter);
    visibilityGrid->addWidget(m_layerHint, 2, 0);

    auto* body = new QWidget(visibility);
    body->setObjectName("skinBodySelector");
    body->setAccessibleName(tr("Visible body parts"));
    body->setFixedSize(104, 148);
    auto* bodyLayout = new QGridLayout(body);
    bodyLayout->setContentsMargins(8, 0, 8, 0);
    bodyLayout->setSpacing(0);
    // Front view: the character's right arm and leg are on the viewer's left.
    const QStringList parts{ tr("Head"), tr("Torso"), tr("Right arm"), tr("Left arm"), tr("Right leg"), tr("Left leg") };
    const QList<QRect> cells{ { 1, 0, 2, 1 }, { 1, 1, 2, 1 }, { 0, 1, 1, 1 }, { 3, 1, 1, 1 }, { 1, 2, 1, 1 }, { 2, 2, 1, 1 } };
    for (int i = 0; i < parts.size(); ++i) {
        auto* button = new BodyPartButton(body);
        button->setObjectName(QString("skinPart%1").arg(i));
        button->setText(parts[i]);
        button->setAccessibleName(parts[i]);
        button->setAccessibleDescription(tr("Toggle visibility of both layers for this body part. Space toggles the selected part."));
        button->setCheckable(true);
        button->setChecked(true);
        button->setFocusPolicy(Qt::StrongFocus);
        const QSize buttonSize(i < 2 ? 44 : 22, i == 0 ? 44 : 52);
        // Stylesheet polish can replace QWidget minimum constraints. Pin both
        // dimensions here too so general inline button styles cannot flatten it.
        button->setStyleSheet(QString("QToolButton#%1 { padding: 0; border: none; "
                                      "min-width: %2px; max-width: %2px; min-height: %3px; max-height: %3px; }")
                                  .arg(button->objectName())
                                  .arg(buttonSize.width())
                                  .arg(buttonSize.height()));
        button->setFixedSize(buttonSize);
        const auto cell = cells[i];
        bodyLayout->addWidget(button, cell.y(), cell.x(), cell.height(), cell.width());
        m_partButtons.append(button);
        connect(button, &QToolButton::toggled, this, &SkinEditorDialog::updateVisibility);
    }
    visibilityGrid->addWidget(body, 3, 0, Qt::AlignHCenter);
    visibilityScroll->setWidget(visibility);
    m_inspectorTabs->addTab(visibilityScroll, tr("Layers"));
    m_model = new QComboBox(m_inspector);
    m_model->setObjectName("skinModel");
    m_model->setAccessibleName(tr("Minecraft skin model"));
    m_model->addItems({ tr("Classic · Steve"), tr("Slim · Alex") });
    m_model->setMinimumWidth(0);
    m_model->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_model->setFixedHeight(30);
    visibilityGrid->addWidget(m_model, 4, 0);
    connect(m_model, &QComboBox::currentIndexChanged, this, [this](int index) { m_document.setModel(SkinModel::Model(index)); });
    auto* camera = new QPushButton(tr("Reset view"), m_inspector);
    camera->setAccessibleName(tr("Reset 3D view"));
    camera->setFixedHeight(28);
    camera->setAutoDefault(false);
    camera->setEnabled(m_preview != nullptr);
    visibilityGrid->addWidget(camera, 5, 0);
    visibilityGrid->setRowStretch(6, 1);
    if (m_preview)
        connect(camera, &QPushButton::clicked, m_preview, &SkinOpenGLWindow::resetView);

    auto* effectsScroll = new QScrollArea(m_inspectorTabs);
    effectsScroll->setObjectName("skinEffectsScroll");
    effectsScroll->setFrameShape(QFrame::NoFrame);
    effectsScroll->setWidgetResizable(true);
    effectsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* effects = new QWidget;
    auto* effectsLayout = new QVBoxLayout(effects);
    effectsLayout->setContentsMargins(0, 4, 0, 2);
    effectsLayout->setSpacing(5);
    auto* effectsHint = new QLabel(tr("Changes visible parts and enabled layers. A selection limits the effect."), effects);
    effectsHint->setWordWrap(true);
    effectsHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    effectsLayout->addWidget(effectsHint);
    m_effect = new QComboBox(effects);
    m_effect->setObjectName("skinEffect");
    m_effect->setAccessibleName(tr("Color effect"));
    m_effect->addItems({ tr("Hue"), tr("Brightness"), tr("Grayscale"), tr("Invert") });
    m_effect->setMinimumWidth(0);
    m_effect->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_effect->setFixedHeight(30);
    effectsLayout->addWidget(m_effect);
    m_effectAmount = new QSlider(Qt::Horizontal, effects);
    m_effectAmount->setObjectName("skinEffectAmount");
    m_effectAmount->setAccessibleName(tr("Effect amount"));
    m_effectAmount->setRange(-180, 180);
    effectsLayout->addWidget(m_effectAmount);
    m_effectValue = new QSpinBox(effects);
    m_effectValue->setObjectName("skinEffectValue");
    m_effectValue->setAccessibleName(tr("Effect amount"));
    m_effectValue->setRange(-180, 180);
    m_effectValue->setSuffix(tr("°"));
    m_effectValue->setFixedHeight(28);
    effectsLayout->addWidget(m_effectValue);
    auto* effectApply = new QPushButton(tr("Apply effect"), effects);
    effectApply->setObjectName("skinApplyEffect");
    effectApply->setAutoDefault(false);
    effectApply->setFixedHeight(30);
    effectsLayout->addWidget(effectApply);
    effectsLayout->addStretch();
    connect(m_effectAmount, &QSlider::valueChanged, m_effectValue, &QSpinBox::setValue);
    connect(m_effectValue, &QSpinBox::valueChanged, m_effectAmount, &QSlider::setValue);
    m_effectApply = effectApply;
    connect(m_effect, &QComboBox::currentIndexChanged, this, &SkinEditorDialog::updateEffectControls);
    connect(m_effectValue, &QSpinBox::valueChanged, this, &SkinEditorDialog::previewColorAdjustments);
    connect(&m_document, &SkinTextureDocument::adjustmentsChanged, this, &SkinEditorDialog::updateEffectControls);
    connect(effectApply, &QPushButton::clicked, this, &SkinEditorDialog::applyEffect);
    updateEffectControls();
    effectsScroll->setWidget(effects);
    m_inspectorTabs->addTab(effectsScroll, tr("Effects"));

    auto* extrasScroll = new QScrollArea(m_inspectorTabs);
    extrasScroll->setObjectName("skinExtrasScroll");
    extrasScroll->setFrameShape(QFrame::NoFrame);
    extrasScroll->setWidgetResizable(true);
    extrasScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_extras = new SkinExtrasPanel(&m_document, extrasScroll);
    extrasScroll->setWidget(m_extras);
    m_inspectorTabs->addTab(extrasScroll, tr("Extras"));
    connect(m_extras, &SkinExtrasPanel::statusMessage, this, &SkinEditorDialog::showError);
    connect(m_extras, &SkinExtrasPanel::inventorySaved, extrasScroll, [extrasScroll] {
        // Return to the new inventory item once the save form has collapsed.
        QTimer::singleShot(0, extrasScroll, [extrasScroll] { extrasScroll->verticalScrollBar()->setValue(0); });
    });
    m_position = new QLabel(drawing);
    m_position->setObjectName("skinPixelPosition");
    drawingLayout->addWidget(m_position);
    auto hovered = [this](QPoint point, QColor color) {
        m_position->setText(tr("%1, %2 · %3").arg(point.x()).arg(point.y()).arg(color.name()));
    };
    connect(m_canvas, &SkinCanvas::pixelHovered, this, hovered);
    if (m_preview)
        connect(m_preview, &SkinOpenGLWindow::pixelHovered, this, hovered);

    m_status = new QLabel(this);
    m_status->setObjectName("skinEditorStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    updateEditingMode();
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
    m_close->setObjectName("skinClose");
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
    // Editor controls use compact dimensions; general launcher form padding
    // otherwise enlarges them beyond their fixed heights and clips labels.
    for (auto* button : findChildren<QPushButton*>())
        if (!m_extras->isAncestorOf(button))
            button->setStyleSheet("QPushButton { min-height: 0; padding: 2px 8px; border-radius: 8px; }");
    for (auto* button : { m_save, m_apply, m_close })
        button->setStyleSheet(QString("QPushButton#%1 { min-height: 26px; max-height: 26px; padding: 0 8px; border-radius: 8px; }")
                                  .arg(button->objectName()));
    for (auto* button : { referenceImport, copy, paste, referenceCopy })
        button->setStyleSheet(QString("QPushButton#%1 { min-height: 22px; max-height: 22px; padding: 0 4px; border-radius: 7px; }")
                                  .arg(button->objectName()));
    for (auto* button : { zoomOut, zoomIn })
        button->setStyleSheet(
            "QPushButton { min-height: 22px; max-height: 22px; min-width: 24px; max-width: 24px; "
            "padding: 0; border-radius: 6px; }");
    fit->setStyleSheet(
        "QPushButton { min-height: 22px; max-height: 22px; min-width: 38px; max-width: 38px; "
        "padding: 0; border-radius: 6px; }");
    for (auto* combo : { m_editMode, part, m_model, m_effect })
        combo->setStyleSheet("QComboBox { min-height: 0; padding: 2px 8px; border-radius: 8px; }");
    for (auto* combo : { m_referenceMode, m_referenceModelChoice, m_referenceWorkspace })
        combo->setStyleSheet(
            QString("QComboBox#%1 { min-height: 22px; max-height: 22px; padding: 0 5px; border-radius: 7px; }").arg(combo->objectName()));
    brush->setStyleSheet("QSpinBox { min-height: 0; padding: 2px 4px; border-radius: 7px; }");
    m_colorHex->setStyleSheet("QLineEdit { min-height: 0; padding: 2px 8px; border-radius: 8px; }");
    setColor(Qt::white);
    m_document.load(skin.getTexture(), skin.getModel());
    updateActions();
    updateLayout();
    qApp->installEventFilter(this);
}

SkinEditorDialog::~SkinEditorDialog()
{
    // QWidget normally destroys children after C++ members. The canvas and GL
    // provider refer to those members, including during focus-out handling.
    qApp->removeEventFilter(this);
    disconnect(&m_document, nullptr, this, nullptr);
    disconnect(&m_referenceDocument, nullptr, this, nullptr);
    m_canvas->clearFocus();
    m_referenceCanvas->clearFocus();
    m_document.endStroke();
    m_referenceDocument.endStroke();
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
    if (m_previewModel.getModel() != m_document.model())
        m_previewModel.setModel(m_document.model());
    if (m_previewModel.getTexture() != m_document.image())
        m_previewModel.setTexture(m_document.image());
    if (!m_preview || m_previewFailed)
        m_fallback->setPixmap(
            QPixmap::fromImage(m_previewModel.getPreview()).scaled(180, 180, Qt::KeepAspectRatio, Qt::FastTransformation));
    const QSignalBlocker blocker(m_model);
    m_model->setCurrentIndex(m_document.model());
    m_undo->setEnabled(m_document.canUndo());
    m_redo->setEnabled(m_document.canRedo());
    setWindowTitle(m_document.isDirty() ? tr("Skin Studio — Unsaved changes") : tr("Skin Studio"));
    updateActions();
}

void SkinEditorDialog::setColor(QColor color)
{
    if (!color.isValid())
        return;
    m_color = color;
    m_canvas->setColor(color);
    if (m_preview)
        m_preview->setColor(color);
    m_colorWheel->setColor(color);
    const QSignalBlocker hexBlocker(m_colorHex);
    const QSignalBlocker opacityBlocker(m_opacity);
    m_colorHex->setText(color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));
    m_opacity->setValue(color.alpha());
    m_opacity->setToolTip(tr("%1% opacity (base pixels stay opaque)").arg(qRound(color.alphaF() * 100)));
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
    if (event->type() == QEvent::PaletteChange)
        updateToolIcons();
    if (event->type() == QEvent::EnabledChange) {
        if (!isEnabled())
            setBodyThroughOverlay(false);
        if (m_previewContainer)
            m_previewContainer->setVisible(isEnabled() && !m_previewFailed);
        if (m_referencePanel)
            updateReferenceView();
    }
    if (event->type() == QEvent::WindowDeactivate)
        setBodyThroughOverlay(false);
}

void SkinEditorDialog::hideEvent(QHideEvent* event)
{
    setBodyThroughOverlay(false);
    QDialog::hideEvent(event);
}

bool SkinEditorDialog::eventFilter(QObject* watched, QEvent* event)
{
    auto* widget = qobject_cast<QWidget*>(watched);
    const bool belongs = widget && (widget == this || isAncestorOf(widget));
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        if (widget == m_referenceCanvas || widget == m_referencePreview)
            m_activeReference = true;
        else if (widget == m_canvas || widget == m_preview)
            m_activeReference = false;
    }
    if (event->type() == QEvent::KeyRelease) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Shift && !key->isAutoRepeat())
            setBodyThroughOverlay(false);
    } else if (event->type() == QEvent::ApplicationDeactivate ||
               (event->type() == QEvent::WindowDeactivate && (belongs || widget == window())) ||
               (event->type() == QEvent::EnabledChange && belongs && !widget->isEnabled()) ||
               (event->type() == QEvent::FocusIn && !belongs)) {
        setBodyThroughOverlay(false);
    } else if (belongs && isVisible() && isEnabled() && !m_applying) {
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Shift)
                setBodyThroughOverlay(true);
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            setBodyThroughOverlay(mouse->modifiers().testFlag(Qt::ShiftModifier));
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SkinEditorDialog::setBodyThroughOverlay(bool enabled)
{
    m_bodyThroughOverlay = enabled;
    if (m_canvas)
        m_canvas->setBodyThroughOverlay(enabled);
    if (m_preview)
        m_preview->setBodyThroughOverlay(enabled);
    updateLayerHint();
}

void SkinEditorDialog::updateLayerHint()
{
    if (!m_layerHint || !m_showBody || !m_showOuter)
        return;
    if (m_bodyThroughOverlay)
        m_layerHint->setText(m_showBody->isChecked() ? tr("Painting: Body · Shift") : tr("Body hidden · release Shift"));
    else
        m_layerHint->setText(m_showOuter->isChecked()  ? tr("Painting: Outer layer · Shift for body")
                             : m_showBody->isChecked() ? tr("Painting: Body")
                                                       : tr("Layers hidden"));
}

QRegion SkinEditorDialog::effectRegion() const
{
    QRegion allowed;
    for (int i = 0; i < m_partButtons.size(); ++i) {
        if (!m_partButtons[i]->isChecked())
            continue;
        const auto part = SkinTextureDocument::Part(i);
        if (m_showBody->isChecked())
            allowed += SkinTextureDocument::uvRegion(part, SkinTextureDocument::Base, m_document.model());
        if (m_showOuter->isChecked())
            allowed += SkinTextureDocument::uvRegion(part, SkinTextureDocument::Overlay, m_document.model());
    }
    return allowed;
}

void SkinEditorDialog::applyEffect()
{
    if (m_applying)
        return;
    m_document.endStroke();
    const auto before = m_document.image();
    m_document.applyEffect(SkinTextureDocument::Effect(m_effect->currentIndex()), m_effectValue->value(), effectRegion());
    m_status->setText(m_document.image() == before ? tr("No pixels changed. Check the effect amount, selection and visible layers.")
                                                   : tr("Effect applied. Undo restores the previous colors."));
}

void SkinEditorDialog::importReference()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("Import reference skin"), {}, tr("PNG images (*.png)"));
    if (!path.isEmpty())
        loadReference(path);
}

bool SkinEditorDialog::loadReference(const QString& path)
{
    QString error;
    const auto image = SkinTextureDocument::readPng(path, &error);
    if (image.isNull()) {
        showError(error);
        return false;
    }
    m_referenceCanvas->clearFocus();
    m_referenceDocument.load(image, SkinModel::Model(m_referenceModelChoice->currentIndex()));
    m_referenceLoaded = true;
    updateReferenceVisibility();
    if (width() < 800)
        m_referenceWorkspace->setCurrentIndex(1);
    m_referenceToggle->setEnabled(true);
    m_referenceToggle->setChecked(true);
    m_extras->setReferenceDocument(&m_referenceDocument);
    updateReferenceView();
    m_referenceCanvas->fitToView();
    if (m_referencePreview)
        m_referencePreview->resetView();
    m_status->setText(tr("Reference loaded. Select pixels to copy, or pick a color. The editing skin is unchanged."));
    return true;
}

void SkinEditorDialog::updateReferenceView()
{
    const bool compact = width() < 800;
    const bool visible = m_referenceLoaded && m_referenceToggle->isChecked() && (!compact || m_referenceWorkspace->currentIndex() == 1);
    if (compact && m_referenceLoaded)
        m_activeReference = visible;
    const bool use3D = m_referencePreview && !m_referenceFailed && m_referenceMode->currentIndex() == 0;
    m_referencePanel->setVisible(visible);
    m_split->setVisible(!compact || !visible);
    m_paintControls->setVisible(!compact || !visible);
    m_referenceWorkspace->setVisible(compact && m_referenceLoaded);
    m_referenceToggle->setVisible(!compact || !m_referenceLoaded);
    m_referenceCanvas->setVisible(!use3D);
    if (m_referencePreview)
        m_referencePreview->setVisible(use3D && isEnabled());
    if (m_referenceSplit) {
        m_referenceSplit->setOrientation(width() < 800 ? Qt::Vertical : Qt::Horizontal);
        if (visible && m_referenceSplit->sizes().value(1) == 0)
            m_referenceSplit->setSizes({ 300, 200 });
    }
    updateLayout();
}

void SkinEditorDialog::copyMainSelection()
{
    if (width() < 800 && m_referenceLoaded && m_referencePanel->isVisible()) {
        const bool copied = m_referencePreview && !m_referenceFailed && m_referenceMode->currentIndex() == 0
                                ? m_referencePreview->copySelection()
                                : m_referenceCanvas->copySelection();
        m_status->setText(copied ? tr("Reference pixels copied. Select Paste to return to the editing skin.")
                                 : tr("Select reference pixels to copy."));
        return;
    }
    const bool copied =
        m_preview && !m_previewFailed && m_editMode->currentIndex() == 0 ? m_preview->copySelection() : m_canvas->copySelection();
    m_status->setText(copied ? tr("Pixels copied. Select Paste, then click to place them.") : tr("There are no editable pixels to copy."));
}

void SkinEditorDialog::pasteMainSelection()
{
    if (width() < 800 && m_referenceLoaded)
        m_referenceWorkspace->setCurrentIndex(0);
    bool ready;
    if (m_preview && !m_previewFailed && m_editMode->currentIndex() == 0) {
        m_canvas->cancelPaste();
        ready = m_preview->beginPaste();
        m_preview->setFocus(Qt::ShortcutFocusReason);
    } else {
        if (m_preview)
            m_preview->cancelPaste();
        ready = m_canvas->beginPaste();
        m_canvas->setFocus(Qt::ShortcutFocusReason);
    }
    m_status->setText(ready ? tr("Click the editing skin to place the copied pixels. Escape cancels.")
                            : tr("Copy some skin pixels before pasting."));
}

void SkinEditorDialog::setSharedTool(SkinCanvas::Tool tool)
{
    if (m_sharingTool)
        return;
    m_sharingTool = true;
    for (int i = 0; i < m_toolButtons.size(); ++i)
        m_toolButtons[i]->setChecked(i == tool);
    m_canvas->setTool(tool);
    if (m_preview)
        m_preview->setTool(tool);
    if (m_referenceCanvas)
        m_referenceCanvas->setTool(tool);
    if (m_referencePreview)
        m_referencePreview->setTool(tool);
    m_sharingTool = false;
}

void SkinEditorDialog::updateReferenceVisibility()
{
    const bool body = m_referenceBody->isChecked();
    const bool outer = m_referenceOuter->isChecked();
    for (int i = 0; i < m_referencePartButtons.size(); ++i) {
        const bool visible = m_referencePartButtons[i]->isChecked();
        auto* button = m_referencePartButtons[i];
        button->setToolTip(visible ? tr("Reference %1 visible · Click to hide").arg(button->text())
                                   : tr("Reference %1 hidden · Click to show").arg(button->text()));
        m_referenceCanvas->setPartVisible(i, visible);
        if (m_referencePreview) {
            m_referencePreview->setPartLayerVisible(i, SkinTextureDocument::Base, body && visible);
            m_referencePreview->setPartLayerVisible(i, SkinTextureDocument::Overlay, outer && visible);
        }
    }
    m_referenceCanvas->setLayerVisibility(body, outer);
}

void SkinEditorDialog::updateEffectControls()
{
    const int effect = m_effect->currentIndex();
    const bool numeric = effect < SkinTextureDocument::Grayscale;
    const QSignalBlocker sliderBlock(m_effectAmount), valueBlock(m_effectValue);
    const int limit = effect == SkinTextureDocument::Hue ? 180 : 100;
    m_effectAmount->setRange(-limit, limit);
    m_effectValue->setRange(-limit, limit);
    m_effectValue->setSuffix(effect == SkinTextureDocument::Hue ? tr("°") : tr("%"));
    const int value = effect == SkinTextureDocument::Hue ? m_document.hueAdjustment() : m_document.brightnessAdjustment();
    m_effectAmount->setValue(value);
    m_effectValue->setValue(value);
    m_effectAmount->setVisible(numeric);
    m_effectValue->setVisible(numeric);
    m_effectApply->setVisible(!numeric);
}

void SkinEditorDialog::previewColorAdjustments()
{
    if (m_applying || m_effect->currentIndex() >= SkinTextureDocument::Grayscale)
        return;
    int hue = m_document.hueAdjustment(), brightness = m_document.brightnessAdjustment();
    if (m_effect->currentIndex() == SkinTextureDocument::Hue)
        hue = m_effectValue->value();
    else
        brightness = m_effectValue->value();
    m_document.setColorAdjustments(hue, brightness, effectRegion());
    m_status->setText(tr("Live adjustment · 0 clears this adjustment · Undo restores the previous skin"));
}

void SkinEditorDialog::updateToolIcons()
{
    for (int i = 0; i < m_toolButtons.size(); ++i) {
        QIcon icon;
        const auto tool = SkinCanvas::Tool(i);
        for (auto mode : { QIcon::Normal, QIcon::Active, QIcon::Selected }) {
            icon.addPixmap(toolPixmap(tool, palette().color(QPalette::ButtonText)), mode, QIcon::Off);
            icon.addPixmap(toolPixmap(tool, palette().color(QPalette::HighlightedText)), mode, QIcon::On);
        }
        icon.addPixmap(toolPixmap(tool, palette().color(QPalette::Disabled, QPalette::ButtonText)), QIcon::Disabled);
        m_toolButtons[i]->setIcon(icon);
    }
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
    const int compact = width() < 800 ? 1 : 0;
    const bool compactReference = compact && m_referenceLoaded;
    if (compactReference && !m_referenceToggle->isChecked() && m_referenceWorkspace->currentIndex() != 0) {
        const QSignalBlocker blocker(m_referenceWorkspace);
        m_referenceWorkspace->setCurrentIndex(0);
    }
    const bool showingReference = compactReference && m_referenceToggle->isChecked() && m_referenceWorkspace->currentIndex() == 1;
    m_referencePanel->setVisible(m_referenceLoaded && m_referenceToggle->isChecked() && (!compact || showingReference));
    m_split->setVisible(!showingReference);
    m_paintControls->setVisible(!showingReference);
    for (auto* button : m_fileButtons)
        button->setVisible(!showingReference);
    m_referenceWorkspace->setVisible(compactReference);
    m_referenceToggle->setVisible(!compactReference);
    const bool painting3D = m_preview && !m_previewFailed && m_editMode->currentIndex() == 0;
    m_region->setVisible(!showingReference);
    for (auto* button : m_zoomButtons)
        button->setVisible(!compactReference);
    m_canvas->setMinimumHeight(compactReference ? 120 : 100);
    if (m_preview)
        m_preview->setMinimumHeight(compactReference ? 120 : 100);
    m_referenceCanvas->setMinimumHeight(compactReference ? 120 : 70);
    if (m_referencePreview)
        m_referencePreview->setMinimumHeight(compactReference ? 120 : 70);
    m_previewPanel->setVisible(!compactReference || painting3D);
    if (m_referenceSplit)
        m_referenceSplit->setOrientation(compact ? Qt::Vertical : Qt::Horizontal);
    if (m_layoutMode == compact)
        return;
    m_layoutMode = compact;
    m_split->setOrientation(Qt::Horizontal);
    m_inspector->setFixedWidth(compact ? 188 : 210);
    m_colorWheel->setFixedHeight(compact ? 144 : 180);
    m_subtitle->setVisible(!compact);
    m_position->setVisible(!compact);
    const int swatchColumns = compact ? 4 : 8;
    for (int i = 0; i < m_swatches.size(); ++i) {
        m_paletteLayout->removeWidget(m_swatches[i]);
        m_paletteLayout->addWidget(m_swatches[i], i / swatchColumns, i % swatchColumns);
    }
    const int columns = compact ? 3 : 6;
    for (int i = 0; i < m_fileButtons.size(); ++i) {
        m_fileButtons[i]->setFixedHeight(compact ? 28 : 32);
        m_fileLayout->removeWidget(m_fileButtons[i]);
        m_fileLayout->addWidget(m_fileButtons[i], i / columns, i % columns);
    }
}

void SkinEditorDialog::updateEditingMode()
{
    m_canvas->cancelPaste();
    if (m_preview)
        m_preview->cancelPaste();
    const bool painting3D = m_preview && !m_previewFailed && m_editMode->currentIndex() == 0;
    m_canvasPanel->setVisible(!painting3D);
    updateVisibility();
    updateLayout();
    if (m_status)
        m_status->setText(painting3D ? tr("Left drag to paint · Right drag to rotate · Shift for body · Alt-click to pick color")
                                     : tr("Paint the PNG · Middle-drag to pan · Shift for body · Alt-click to pick color"));
}

void SkinEditorDialog::updateVisibility()
{
    if (!m_showBody || !m_showOuter || m_partButtons.size() != 6)
        return;
    m_document.endStroke();
    const auto scope = effectRegion();
    if (scope != m_effectRegion) {
        m_document.finishColorAdjustments();
        m_effectRegion = scope;
    }
    const bool body = m_showBody->isChecked();
    const bool outer = m_showOuter->isChecked();
    const auto target = outer ? SkinTextureDocument::Overlay : SkinTextureDocument::Base;
    const auto region = SkinTextureDocument::Part(m_region->currentIndex() - 1);
    m_canvas->setLayerVisibility(body, outer);
    m_canvas->setRegion(region, target);
    unsigned visibleParts = 0;
    for (int i = 0; i < m_partButtons.size(); ++i) {
        auto* button = m_partButtons[i];
        const bool visible = button->isChecked();
        if (visible)
            visibleParts |= 1u << i;
        button->setToolTip(visible ? tr("%1 visible · Click to hide").arg(button->text())
                                   : tr("%1 hidden · Click to show").arg(button->text()));
        m_canvas->setPartVisible(i, visible);
        if (m_preview) {
            m_preview->setPartLayerVisible(i, SkinTextureDocument::Base, body && visible);
            m_preview->setPartLayerVisible(i, SkinTextureDocument::Overlay, outer && visible);
        }
    }
    if (m_preview) {
        m_preview->setRegion(m_editMode->currentIndex() == 0 ? SkinTextureDocument::All : region, target);
        m_preview->setEditingEnabled(!m_previewFailed && m_editMode->currentIndex() == 0 && (body || outer));
    }
    if (m_extras)
        m_extras->setVisibleParts(visibleParts);
    updateLayerHint();
}
