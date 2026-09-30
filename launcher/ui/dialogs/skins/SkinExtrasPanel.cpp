// SPDX-License-Identifier: GPL-3.0-only
#include "SkinExtrasPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include "Application.h"

namespace {
using D = SkinTextureDocument;
QString extrasPath()
{
    return QDir(APPLICATION_DYN ? APPLICATION_DYN->dataRoot() : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath("skin-extras");
}
}  // namespace

SkinExtrasPanel::SkinExtrasPanel(D* document, QWidget* parent, QString directory)
    : QWidget(parent), m_document(document), m_library(directory.isEmpty() ? extrasPath() : std::move(directory))
{
    setObjectName("skinExtrasPanel");
    setMinimumWidth(0);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    m_saveToggle = new QToolButton(this);
    m_saveToggle->setObjectName("skinExtraSaveToggle");
    m_saveToggle->setText(tr("Save new extra"));
    m_saveToggle->setCheckable(true);
    m_saveToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_saveToggle->setArrowType(Qt::RightArrow);
    m_saveToggle->setCursor(Qt::PointingHandCursor);
    m_saveToggle->setFixedHeight(26);
    m_saveToggle->setStyleSheet("QToolButton#skinExtraSaveToggle { min-height: 26px; max-height: 26px; padding: 0 4px; }");
    m_saveForm = new QWidget(this);
    m_saveForm->setObjectName("skinExtraSaveForm");
    auto* saveLayout = new QVBoxLayout(m_saveForm);
    saveLayout->setContentsMargins(0, 0, 0, 0);
    saveLayout->setSpacing(4);
    m_saveForm->hide();
    connect(m_saveToggle, &QToolButton::toggled, this, [this](bool expanded) {
        m_saveForm->setVisible(expanded);
        m_saveToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    m_source = new QComboBox(this);
    m_source->setObjectName("skinExtraSource");
    m_source->setAccessibleName(tr("Skin to save an extra from"));
    m_source->addItem(tr("Editing skin"));
    m_part = new QComboBox(this);
    m_part->setObjectName("skinExtraPart");
    m_part->setAccessibleName(tr("Body part to save"));
    m_part->addItem(tr("Visible parts"), -2);
    m_part->addItem(tr("Whole skin"), int(D::All));
    const QStringList parts{ tr("Head"), tr("Torso"), tr("Right arm"), tr("Left arm"), tr("Right leg"), tr("Left leg") };
    for (int part = 0; part < parts.size(); ++part)
        m_part->addItem(parts[part], part);
    m_layer = new QComboBox(this);
    m_layer->setObjectName("skinExtraLayer");
    m_layer->setAccessibleName(tr("Skin layer to save"));
    m_layer->addItem(tr("Outer layer"), int(D::Overlay));
    m_layer->addItem(tr("Body layer"), int(D::Base));
    m_layer->addItem(tr("Both layers"), int(D::Both));
    for (auto* combo : { m_source, m_part, m_layer }) {
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->setMinimumWidth(0);
        combo->setFixedHeight(26);
        combo->setStyleSheet("QComboBox { min-height: 22px; max-height: 22px; padding: 1px 6px; border-radius: 6px; }");
        saveLayout->addWidget(combo);
    }
    m_name = new QLineEdit(this);
    m_name->setObjectName("skinExtraName");
    m_name->setPlaceholderText(tr("Name, e.g. Helmet"));
    m_name->setAccessibleName(tr("Skin extra name"));
    m_name->setMaxLength(80);
    m_name->setFixedHeight(26);
    m_name->setStyleSheet("QLineEdit { min-height: 22px; max-height: 22px; padding: 1px 6px; border-radius: 6px; }");
    saveLayout->addWidget(m_name);
    auto* saveButton = new QPushButton(tr("Save extra"), this);
    saveButton->setObjectName("skinExtraSave");
    saveLayout->addWidget(saveButton);
    connect(saveButton, &QPushButton::clicked, this, &SkinExtrasPanel::save);
    m_entries = new QListWidget(this);
    m_entries->setObjectName("skinExtraInventory");
    m_entries->setAccessibleName(tr("Saved skin extras"));
    m_entries->setIconSize(QSize(36, 36));
    // The list's default 192px size hint can consume the whole inspector.
    // Keep the inventory and its actions together; the list scrolls internally.
    m_entries->setFixedHeight(80);
    m_entries->setMinimumWidth(0);
    m_entries->setTextElideMode(Qt::ElideRight);
    m_entries->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_entries, 1);
    m_replace = new QCheckBox(tr("Replace transparency"), this);
    m_replace->setObjectName("skinExtraReplaceTransparent");
    m_replace->setToolTip(tr("Clear existing outer pixels where the saved extra is transparent. Leave off to combine extras."));
    layout->addWidget(m_replace);
    auto* buttons = new QHBoxLayout;
    m_apply = new QPushButton(tr("Apply"), this);
    m_apply->setObjectName("skinExtraApply");
    m_apply->setToolTip(tr("Apply to the original body parts. An active selection limits where pixels change."));
    m_remove = new QPushButton(tr("Delete"), this);
    m_remove->setObjectName("skinExtraDelete");
    buttons->addWidget(m_apply);
    buttons->addWidget(m_remove);
    layout->addLayout(buttons);
    for (auto* button : { saveButton, m_apply, m_remove }) {
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(26);
        button->setStyleSheet(QString("QPushButton#%1 { min-height: 22px; max-height: 22px; padding: 1px 6px; border-radius: 6px; }")
                                  .arg(button->objectName()));
    }
    layout->addWidget(m_saveToggle);
    layout->addWidget(m_saveForm);
    m_message = new QLabel(this);
    m_message->setObjectName("skinExtraStatus");
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    connect(m_apply, &QPushButton::clicked, this, &SkinExtrasPanel::apply);
    connect(m_remove, &QPushButton::clicked, this, &SkinExtrasPanel::remove);
    connect(m_entries, &QListWidget::itemSelectionChanged, this, [this] {
        m_apply->setEnabled(m_entries->currentItem());
        m_remove->setEnabled(m_entries->currentItem());
    });
    refresh();
    m_saveToggle->setChecked(m_entries->count() == 0);
}

void SkinExtrasPanel::setReferenceDocument(D* document)
{
    m_reference = document;
    if (document && m_source->count() == 1)
        m_source->addItem(tr("Reference skin"));
    else if (!document && m_source->count() > 1)
        m_source->removeItem(1);
}

QRegion SkinExtrasPanel::sourceRegion(const D* document) const
{
    const auto layer = static_cast<D::Layer>(m_layer->currentData().toInt());
    const int part = m_part->currentData().toInt();
    if (part != -2)
        return D::uvRegion(static_cast<D::Part>(part), layer, document->model());
    QRegion region;
    for (int p = D::Head; p <= D::LeftLeg; ++p)
        if (m_visibleParts & (1u << p))
            region += D::uvRegion(static_cast<D::Part>(p), layer, document->model());
    return region;
}

void SkinExtrasPanel::report(const QString& text)
{
    m_message->setText(text);
    emit statusMessage(text);
}

void SkinExtrasPanel::refresh(const QString& select)
{
    m_entries->clear();
    QString warning;
    for (const auto& entry : m_library.entries(&warning)) {
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(entry.pixels.image)), entry.name, m_entries);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(tr("%1 · %2").arg(entry.name, entry.model == SkinModel::SLIM ? tr("Slim") : tr("Classic")));
        if (entry.id == select)
            m_entries->setCurrentItem(item);
    }
    m_apply->setEnabled(m_entries->currentItem());
    m_remove->setEnabled(m_entries->currentItem());
    if (!warning.isEmpty())
        report(warning);
    else if (!m_entries->count())
        report(tr("Your saved skin extras will appear here."));
}

void SkinExtrasPanel::save()
{
    const auto* source = m_source->currentIndex() == 1 ? m_reference.data() : m_document;
    if (!source) {
        report(tr("Import a reference skin first."));
        return;
    }
    QString error;
    const auto id = m_library.save(m_name->text(), source->copyPixels(sourceRegion(source)), source->model(), &error);
    if (id.isEmpty()) {
        report(error);
        return;
    }
    refresh(id);
    m_saveToggle->setChecked(false);
    report(tr("Saved %1. It will be available in future designs.").arg(m_name->text().trimmed()));
    emit inventorySaved();
}

void SkinExtrasPanel::apply()
{
    if (!m_entries->currentItem())
        return;
    QString error;
    const auto entry = m_library.load(m_entries->currentItem()->data(Qt::UserRole).toString(), &error);
    if (!entry) {
        report(error);
        return;
    }
    auto patch = SkinExtrasLibrary::forModel(*entry, m_document->model());
    if (!m_replace->isChecked()) {
        QRegion opaque;
        for (const auto& rect : patch.mask)
            for (int y = rect.top(); y <= rect.bottom(); ++y)
                for (int x = rect.left(); x <= rect.right(); ++x)
                    if (patch.image.pixelColor(x, y).alpha())
                        opaque += QRect(x, y, 1, 1);
        patch.mask = opaque;
    }
    const auto before = m_document->image();
    m_document->pastePixels(patch, patch.origin, D::uvRegion(D::All, D::Both, m_document->model()));
    report(before == m_document->image() ? tr("No pixels changed. Check your selection and the saved extra.")
                                         : tr("Applied %1 to its body parts. Undo restores the previous pixels.").arg(entry->name));
}

void SkinExtrasPanel::remove()
{
    if (!m_entries->currentItem())
        return;
    const auto id = m_entries->currentItem()->data(Qt::UserRole).toString();
    const auto name = m_entries->currentItem()->text();
    QMessageBox confirmation(QMessageBox::Question, tr("Delete skin extra"), tr("Delete %1 from Skin Extras?").arg(name),
                             QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setTextFormat(Qt::PlainText);
    confirmation.setDefaultButton(QMessageBox::No);
    if (confirmation.exec() != QMessageBox::Yes)
        return;
    QString error;
    if (!m_library.remove(id, &error)) {
        report(error);
        return;
    }
    refresh();
    report(tr("Deleted %1.").arg(name));
}
