// SPDX-License-Identifier: GPL-3.0-only
#include "SkinPalettePanel.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "minecraft/skins/SkinPalette.h"
#include "ui/dialogs/skins/SkinColorWheel.h"
#include "ui/widgets/ClayWidgets.h"
#include "ui/widgets/FloatingUi.h"

namespace {
using D = SkinTextureDocument;
constexpr int BothArms = -2;
constexpr int BothLegs = -3;
QIcon colorIcon(QColor color)
{
    QPixmap swatch(18, 18);
    swatch.fill(color);
    return QIcon(swatch);
}
QColor targetColor(const QLineEdit* input)
{
    const auto text = input->text().trimmed();
    return text.size() == 7 && text.startsWith('#') ? QColor(text) : QColor();
}
}  // namespace

SkinPalettePanel::SkinPalettePanel(D* document, QWidget* parent) : QWidget(parent), m_document(document)
{
    setObjectName("skinPalettePanel");
    setMinimumWidth(0);
    m_allowed = QRegion(QRect(0, 0, 64, 64));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto* description = new QLabel(tr("Recolor related shades. Compare the preview before applying."), this);
    description->setWordWrap(true);
    description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(description);

    m_part = new ClayComboBox(this);
    m_part->setObjectName("skinPalettePart");
    m_part->setAccessibleName(tr("Palette body part"));
    m_part->setToolTip(tr("Find and replace colors only on these visible body parts. Your selection also limits the change."));
    m_part->addItem(tr("Whole skin"), int(D::All));
    m_part->addItem(tr("Head"), int(D::Head));
    m_part->addItem(tr("Torso"), int(D::Body));
    m_part->addItem(tr("Both arms"), BothArms);
    m_part->addItem(tr("Both legs"), BothLegs);
    m_part->addItem(tr("Right arm"), int(D::RightArm));
    m_part->addItem(tr("Left arm"), int(D::LeftArm));
    m_part->addItem(tr("Right leg"), int(D::RightLeg));
    m_part->addItem(tr("Left leg"), int(D::LeftLeg));
    m_layer = new ClayComboBox(this);
    m_layer->setObjectName("skinPaletteLayer");
    m_layer->setAccessibleName(tr("Palette skin layer"));
    m_layer->setToolTip(tr("Restrict the palette to the body, outer layer, or both visible layers."));
    m_layer->addItem(tr("Both layers"), int(D::Both));
    m_layer->addItem(tr("Body layer"), int(D::Base));
    m_layer->addItem(tr("Outer layer"), int(D::Overlay));
    auto* scopeLabel = new QLabel(tr("Where to recolor"), this);
    scopeLabel->setBuddy(m_part);
    layout->addWidget(scopeLabel);
    layout->addWidget(m_part);
    layout->addWidget(m_layer);

    m_source = new ClayComboBox(this);
    m_source->setObjectName("skinPaletteSource");
    m_source->setAccessibleName(tr("Source color family"));
    m_source->setToolTip(tr("Related shades found in this scope. Gray shades form their own family."));
    for (auto* combo : { m_part, m_layer, m_source }) {
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->setMinimumWidth(0);
        combo->setFixedHeight(26);
        combo->setStyleSheet("QComboBox { min-height: 22px; max-height: 22px; padding: 1px 6px; border-radius: 6px; }");
    }
    auto* sourceLabel = new QLabel(tr("Color family"), this);
    sourceLabel->setBuddy(m_source);
    layout->addWidget(sourceLabel);
    m_swatches = new QGridLayout;
    m_swatches->setSpacing(4);
    m_swatches->setAlignment(Qt::AlignLeft);
    layout->addLayout(m_swatches);
    layout->addWidget(m_source);

    auto* targetRow = new QHBoxLayout;
    m_target = new QLineEdit(this);
    m_target->setObjectName("skinPaletteTarget");
    m_target->setAccessibleName(tr("Replacement color in hexadecimal"));
    m_target->setPlaceholderText("#RRGGBB");
    m_target->setMaxLength(7);
    m_target->setMinimumWidth(0);
    m_target->setFixedHeight(26);
    m_target->setStyleSheet("QLineEdit { min-height: 22px; max-height: 22px; padding: 1px 6px; border-radius: 6px; }");
    m_choose = new QPushButton(tr("Pick"), this);
    m_choose->setObjectName("skinPaletteChooseColor");
    m_choose->setAccessibleName(tr("Pick replacement color"));
    m_choose->setToolTip(tr("Open the floating color picker. Changes appear in the preview until you apply."));
    targetRow->addWidget(m_target, 1);
    targetRow->addWidget(m_choose);
    auto* targetLabel = new QLabel(tr("New color"), this);
    targetLabel->setBuddy(m_target);
    layout->addWidget(targetLabel);
    layout->addLayout(targetRow);
    auto* toleranceRow = new QHBoxLayout;
    m_tolerance = new QSpinBox(this);
    m_tolerance->setObjectName("skinPaletteTolerance");
    m_tolerance->setAccessibleName(tr("Color family hue tolerance"));
    m_tolerance->setRange(0, 90);
    m_tolerance->setValue(24);
    m_tolerance->setSuffix(tr("°"));
    m_tolerance->setToolTip(tr("Include colors within this hue distance. Shading is included automatically; gray shades stay separate."));
    m_tolerance->setFixedHeight(26);
    m_tolerance->setFixedWidth(62);
    m_tolerance->setStyleSheet("QSpinBox { min-height: 22px; max-height: 22px; padding: 1px 4px; border-radius: 6px; }");
    auto* toleranceLabel = new QLabel(tr("Tolerance"), this);
    toleranceLabel->setBuddy(m_tolerance);
    toleranceRow->addWidget(toleranceLabel, 1);
    toleranceRow->addWidget(m_tolerance);
    layout->addLayout(toleranceRow);
    auto* buttons = new QVBoxLayout;
    m_apply = new QPushButton(tr("Apply swap"), this);
    m_apply->setObjectName("skinPaletteApply");
    m_apply->setToolTip(tr("Apply the preview as one undoable edit."));
    m_reset = new QPushButton(tr("Reset preview"), this);
    m_reset->setObjectName("skinPaletteReset");
    buttons->addWidget(m_apply);
    buttons->addWidget(m_reset);
    layout->addLayout(buttons);
    for (auto* button : { m_choose, m_apply, m_reset }) {
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(26);
        button->setStyleSheet(QString("QPushButton#%1 { min-height: 22px; max-height: 22px; padding: 1px 6px; border-radius: 6px; }")
                                 .arg(button->objectName()));
    }
    m_message = new QLabel(this);
    m_message->setObjectName("skinPaletteStatus");
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_message);
    layout->addStretch();

    // QMenu stays a native, dismissible popup; QDialog would become a workspace tab.
    FloatingUi::install();
    m_picker = new QMenu(this);
    m_picker->setObjectName("skinPaletteColorPopup");
    m_picker->setAccessibleName(tr("Replacement color picker"));
    auto* pickerContent = new QWidget(m_picker);
    pickerContent->setFixedWidth(216);
    auto* pickerLayout = new QVBoxLayout(pickerContent);
    pickerLayout->setContentsMargins(10, 8, 10, 8);
    pickerLayout->setSpacing(6);
    auto* pickerTitle = new QLabel(tr("New color"), pickerContent);
    auto titleFont = pickerTitle->font();
    titleFont.setBold(true);
    pickerTitle->setFont(titleFont);
    pickerLayout->addWidget(pickerTitle);
    m_wheel = new SkinColorWheel(pickerContent);
    m_wheel->setObjectName("skinPaletteColorWheel");
    m_wheel->setAccessibleName(tr("Replacement color wheel"));
    m_wheel->setFixedSize(196, 196);
    pickerLayout->addWidget(m_wheel);
    m_popupHex = new QLineEdit(pickerContent);
    m_popupHex->setObjectName("skinPalettePopupHex");
    m_popupHex->setAccessibleName(tr("Replacement color in hexadecimal"));
    m_popupHex->setMaxLength(7);
    m_popupHex->setPlaceholderText("#RRGGBB");
    pickerLayout->addWidget(m_popupHex);
    auto* pickerHint = new QLabel(tr("Preview only. Click outside to close."), pickerContent);
    pickerHint->setWordWrap(true);
    pickerLayout->addWidget(pickerHint);
    auto* pickerAction = new QWidgetAction(m_picker);
    pickerAction->setDefaultWidget(pickerContent);
    m_picker->addAction(pickerAction);

    connect(m_part, &QComboBox::currentIndexChanged, this, &SkinPalettePanel::refresh);
    connect(m_layer, &QComboBox::currentIndexChanged, this, &SkinPalettePanel::refresh);
    connect(m_source, &QComboBox::currentIndexChanged, this, &SkinPalettePanel::sourceChanged);
    connect(m_target, &QLineEdit::textChanged, this, &SkinPalettePanel::updatePreview);
    connect(m_tolerance, &QSpinBox::valueChanged, this, &SkinPalettePanel::updatePreview);
    connect(m_choose, &QPushButton::clicked, this, &SkinPalettePanel::openColorPicker);
    connect(m_wheel, &SkinColorWheel::colorChanged, this, &SkinPalettePanel::setTarget);
    connect(m_popupHex, &QLineEdit::textEdited, m_target, &QLineEdit::setText);
    connect(m_apply, &QPushButton::clicked, this, &SkinPalettePanel::apply);
    connect(m_reset, &QPushButton::clicked, this, [this] { setTarget(sourceColor()); });
    connect(m_document, &D::changed, this, &SkinPalettePanel::refresh);
    connect(m_document, &D::selectionChanged, this, &SkinPalettePanel::refresh);
    refresh();
}

void SkinPalettePanel::setAllowedRegion(QRegion region)
{
    if (m_allowed == region)
        return;
    m_allowed = std::move(region);
    refresh();
}

QRegion SkinPalettePanel::effectiveRegion() const
{
    const int part = m_part->currentData().toInt();
    const auto layer = static_cast<D::Layer>(m_layer->currentData().toInt());
    QRegion region;
    if (part == BothArms || part == BothLegs) {
        region = D::uvRegion(part == BothArms ? D::RightArm : D::RightLeg, layer, m_document->model()) |
                 D::uvRegion(part == BothArms ? D::LeftArm : D::LeftLeg, layer, m_document->model());
    } else {
        region = D::uvRegion(static_cast<D::Part>(part), layer, m_document->model());
    }
    return region & m_allowed;
}

QColor SkinPalettePanel::sourceColor() const
{
    return m_source->currentData().value<QColor>();
}

void SkinPalettePanel::refresh()
{
    const auto previous = sourceColor();
    const auto patch = m_document->copyPixels(effectiveRegion());
    const auto families = SkinPalette::extractFamilies(patch.image, patch.mask);
    while (auto* item = m_swatches->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int selected = 0;
    {
        const QSignalBlocker blocker(m_source);
        m_source->clear();
        for (const auto& family : families) {
            const int index = m_source->count();
            const auto hex = family.color.name().toUpper();
            const auto label = family.color.hsvSaturationF() < 0.12 ? tr("%1 · gray shades").arg(hex) : tr("%1 · related shades").arg(hex);
            m_source->addItem(colorIcon(family.color), label, family.color);
            if (previous.isValid() && family.color.rgb() == previous.rgb())
                selected = index;
            auto* swatch = new QPushButton(this);
            swatch->setObjectName(QString("skinPaletteSwatch%1").arg(index));
            swatch->setAccessibleName(tr("Select color family %1").arg(hex));
            swatch->setToolTip(tr("%1 · %n pixel(s)", nullptr, family.pixels).arg(hex));
            swatch->setCheckable(true);
            swatch->setAutoDefault(false);
            swatch->setCursor(Qt::PointingHandCursor);
            swatch->setFixedSize(24, 24);
            swatch->setStyleSheet(QString("QPushButton#%1 { min-width: 20px; max-width: 20px; min-height: 20px; max-height: 20px; "
                                         "padding: 0; background: %2; border: 2px solid palette(mid); border-radius: 6px; } "
                                         "QPushButton#%1:checked { border-color: palette(highlight); } "
                                         "QPushButton#%1:focus { border-color: palette(text); }")
                                     .arg(swatch->objectName(), hex));
            connect(swatch, &QPushButton::clicked, this, [this, index] {
                m_source->setCurrentIndex(index);
                // A click on the current family keeps its swatch checked too.
                for (int i = 0; i < m_swatches->count(); ++i)
                    static_cast<QPushButton*>(m_swatches->itemAt(i)->widget())->setChecked(i == m_source->currentIndex());
            });
            m_swatches->addWidget(swatch, index / 5, index % 5);
        }
        if (!families.isEmpty())
            m_source->setCurrentIndex(selected);
    }
    const bool available = !families.isEmpty();
    for (auto* control : QList<QWidget*>{ m_source, m_target, m_choose, m_tolerance, m_reset })
        control->setEnabled(available);
    if (!available)
        m_picker->hide();
    sourceChanged();
}

void SkinPalettePanel::sourceChanged()
{
    for (int i = 0; i < m_swatches->count(); ++i)
        static_cast<QPushButton*>(m_swatches->itemAt(i)->widget())->setChecked(i == m_source->currentIndex());
    setTarget(sourceColor());
}

void SkinPalettePanel::setTarget(QColor color)
{
    const QSignalBlocker blocker(m_target);
    m_target->setText(color.isValid() ? color.name().toUpper() : QString());
    updatePreview();
}

void SkinPalettePanel::openColorPicker()
{
    if (!m_choose->isEnabled())
        return;
    if (m_picker->isVisible()) {
        m_picker->hide();
        return;
    }
    const auto color = targetColor(m_target);
    m_wheel->setColor(color.isValid() ? color : sourceColor());
    FloatingUi::anchor(m_picker, m_choose);
    m_picker->popup(m_choose->mapToGlobal(QPoint(0, m_choose->height())));
    m_wheel->setFocus(Qt::PopupFocusReason);
}

void SkinPalettePanel::updatePreview()
{
    const auto source = sourceColor();
    const auto target = targetColor(m_target);
    m_patch = m_document->copyPixels(effectiveRegion());
    m_changedPixels = 0;
    m_patch.image = SkinPalette::swapFamily(m_patch.image, m_patch.mask, source, target, m_tolerance->value(), &m_changedPixels);
    m_previewImage = m_document->image();
    if (m_changedPixels)
        for (const auto& rect : m_patch.mask)
            for (int y = rect.top(); y <= rect.bottom(); ++y)
                for (int x = rect.left(); x <= rect.right(); ++x)
                    m_previewImage.setPixelColor(m_patch.origin + QPoint(x, y), m_patch.image.pixelColor(x, y));
    if (target.isValid())
        m_wheel->setColor(target);
    {
        const QSignalBlocker blocker(m_popupHex);
        if (m_popupHex->text() != m_target->text())
            m_popupHex->setText(m_target->text());
    }
    m_choose->setIcon(target.isValid() ? colorIcon(target) : QIcon());
    m_apply->setEnabled(m_changedPixels > 0);
    if (!source.isValid())
        m_message->setText(tr("No colors here. Check scope, visibility, and selection."));
    else if (!target.isValid())
        m_message->setText(tr("Enter a color as #RRGGBB."));
    else if (m_changedPixels)
        m_message->setText(tr("Preview · %n pixel(s) will change.", nullptr, m_changedPixels));
    else
        m_message->setText(tr("Choose a new color to preview."));
    emit previewChanged(m_previewImage);
}

void SkinPalettePanel::apply()
{
    if (!m_changedPixels)
        return;
    const auto patch = m_patch;
    const int count = m_changedPixels;
    m_document->pastePixels(patch, patch.origin, effectiveRegion());
    const auto message = tr("Recolored %n pixel(s). Undo restores the colors.", nullptr, count);
    m_message->setText(message);
    emit statusMessage(message);
}
