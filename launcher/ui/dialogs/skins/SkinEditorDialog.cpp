// SPDX-License-Identifier: GPL-3.0-only
#include "SkinEditorDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>

#include "Application.h"
#include "ui/dialogs/skins/SkinCanvas.h"
#include "ui/dialogs/skins/SkinColorWheel.h"
#include "ui/widgets/ClayWidgets.h"

namespace {
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
    }
    return pixmap;
}
}  // namespace

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

    auto* toolRow = new QHBoxLayout;
    auto* toolbox = new QWidget(drawing);
    toolbox->setObjectName("skinToolbox");
    toolbox->setAccessibleName(tr("Painting tools"));
    auto* tools = new QHBoxLayout(toolbox);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(4);
    auto* group = new QButtonGroup(this);
    const QStringList toolNames{ tr("Brush"), tr("Eraser"), tr("Pick Color"), tr("Pan / Rotate") };
    const QStringList toolIds{ "skinToolBrush", "skinToolEraser", "skinToolPicker", "skinToolPan" };
    const QList<Qt::Key> toolKeys{ Qt::Key_B, Qt::Key_E, Qt::Key_I, Qt::Key_H };
    const QStringList descriptions{ tr("Paint with the selected color."), tr("Erase outer-layer pixels. Base layers stay opaque."),
                                    tr("Sample a color. Alt-click also picks a color."),
                                    tr("Drag to rotate the model or pan the texture.") };
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
    toolRow->addWidget(brush);
    drawingLayout->addLayout(toolRow);

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
    modes->addWidget(grid);
    drawingLayout->addLayout(modes);

    auto* split = m_split = new QSplitter(Qt::Horizontal, drawing);
    split->setChildrenCollapsible(false);
    drawingLayout->addWidget(split, 1);
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

    auto* previewPanel = new QWidget(split);
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
        m_canvas->setTool(SkinCanvas::Tool(id));
        if (m_preview && m_editMode->currentIndex() == 0)
            m_preview->setFocus(Qt::ShortcutFocusReason);
        else
            m_canvas->setFocus(Qt::ShortcutFocusReason);
    });
    connect(m_canvas, &SkinCanvas::toolChanged, this, [this, group](SkinCanvas::Tool tool) {
        group->button(tool)->setChecked(true);
        if (m_preview)
            m_preview->setTool(tool);
    });
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
    inspector->addWidget(visibilityScroll, 1);
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
        button->setStyleSheet("QPushButton { min-height: 0; padding: 2px 8px; border-radius: 8px; }");
    for (auto* button : { zoomOut, zoomIn })
        button->setStyleSheet(
            "QPushButton { min-height: 22px; max-height: 22px; min-width: 24px; max-width: 24px; "
            "padding: 0; border-radius: 6px; }");
    fit->setStyleSheet(
        "QPushButton { min-height: 22px; max-height: 22px; min-width: 38px; max-width: 38px; "
        "padding: 0; border-radius: 6px; }");
    for (auto* combo : { m_editMode, part, m_model })
        combo->setStyleSheet("QComboBox { min-height: 0; padding: 2px 8px; border-radius: 8px; }");
    brush->setStyleSheet("QSpinBox { min-height: 0; padding: 2px 4px; border-radius: 7px; }");
    m_colorHex->setStyleSheet("QLineEdit { min-height: 0; padding: 2px 8px; border-radius: 8px; }");
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
    if (event->type() == QEvent::EnabledChange && m_previewContainer)
        m_previewContainer->setVisible(isEnabled() && !m_previewFailed);
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
    const bool painting3D = m_preview && !m_previewFailed && m_editMode->currentIndex() == 0;
    m_canvasPanel->setVisible(!painting3D);
    updateVisibility();
    if (m_status)
        m_status->setText(painting3D ? tr("Paint with left drag · Rotate with right drag · Scroll to zoom · Alt-click to pick color")
                                     : tr("Paint the PNG · Middle-drag to pan · Scroll to zoom · Alt-click to pick color"));
}

void SkinEditorDialog::updateVisibility()
{
    if (!m_showBody || !m_showOuter || m_partButtons.size() != 6)
        return;
    m_document.endStroke();
    const bool body = m_showBody->isChecked();
    const bool outer = m_showOuter->isChecked();
    const auto target = outer ? SkinTextureDocument::Overlay : SkinTextureDocument::Base;
    const auto region = SkinTextureDocument::Part(m_region->currentIndex() - 1);
    m_canvas->setLayerVisibility(body, outer);
    m_canvas->setRegion(region, target);
    for (int i = 0; i < m_partButtons.size(); ++i) {
        auto* button = m_partButtons[i];
        const bool visible = button->isChecked();
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
    m_layerHint->setText(outer ? tr("Painting: Outer layer") : body ? tr("Painting: Body") : tr("Layers hidden"));
}
