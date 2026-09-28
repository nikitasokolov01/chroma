// SPDX-License-Identifier: GPL-3.0-only
#include "PrismProfileDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Application.h"
#include "ChromaProfile.h"

namespace {
QLabel* plainLabel(const QString& text, QWidget* parent)
{
    auto label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

}  // namespace

PrismProfileDialog::PrismProfileDialog(QWidget* parent, const QString& initialSource) : QDialog(parent)
{
    setObjectName("prismProfileDialog");
    setWindowTitle(tr("Use Prism folder"));
    setWindowModality(Qt::ApplicationModal);
    resize(700, 450);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(16);
    auto heading = plainLabel(tr("Your Prism library, in Chroma"), this);
    auto headingFont = heading->font();
    headingFont.setPointSizeF(qMax(10.0, headingFont.pointSizeF()) + 5);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    layout->addWidget(plainLabel(tr("Choose the Prism data folder you want Chroma to open."), this));

    auto sourceLabel = plainLabel(tr("Prism data folder (contains prismlauncher.cfg)"), this);
    auto sourceLayout = new QVBoxLayout;
    sourceLayout->setSpacing(6);
    sourceLayout->addWidget(sourceLabel);
    auto sourceRow = new QHBoxLayout;
    m_profilePath = new QComboBox(this);
    m_profilePath->setObjectName("prismProfilePath");
    m_profilePath->setAccessibleName(tr("Prism data folder"));
    m_profilePath->setEditable(true);
    m_profilePath->setInsertPolicy(QComboBox::NoInsert);
    m_profilePath->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_profilePath->lineEdit()->setPlaceholderText(tr("Choose an installed or portable Prism data folder"));
    m_profilePath->lineEdit()->installEventFilter(this);
    sourceLabel->setBuddy(m_profilePath);
    sourceRow->addWidget(m_profilePath, 1);
    auto browse = new QPushButton(tr("Browse…"), this);
    browse->setObjectName("prismBrowseProfileButton");
    sourceRow->addWidget(browse);
    m_inspect = new QPushButton(tr("Inspect"), this);
    m_inspect->setObjectName("prismInspectButton");
    sourceRow->addWidget(m_inspect);
    sourceLayout->addLayout(sourceRow);
    layout->addLayout(sourceLayout);

    m_summary = plainLabel(QString(), this);
    m_summary->setObjectName("prismProfileSummary");
    m_summary->setAccessibleName(tr("Prism profile summary"));
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_summary->setMinimumHeight(72);
    layout->addWidget(m_summary);

    auto notice = plainLabel(tr("Chroma will use this profile directly. Changes to settings and worlds are shared. "
                                "Close Prism before continuing. Chroma will restart to open this folder."),
                             this);
    notice->setObjectName("prismSharedProfileNotice");
    layout->addWidget(notice);
    m_status = plainLabel(QString(), this);
    m_status->setObjectName("prismProfileStatus");
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(m_status);
    layout->addStretch();

    auto buttons = new QDialogButtonBox(this);
    m_use = buttons->addButton(tr("Use this folder"), QDialogButtonBox::ActionRole);
    m_use->setObjectName("prismUseProfileButton");
    m_use->setDefault(true);
    auto cancel = buttons->addButton(QDialogButtonBox::Cancel);
    cancel->setObjectName("prismProfileCancelButton");
    layout->addWidget(buttons);

    setStyleSheet(QStringLiteral(R"(
        QDialog#prismProfileDialog QLabel { background: transparent; }
        QLabel#prismProfileSummary { background: palette(alternate-base); border: 1px solid palette(mid); border-radius: 10px; padding: 14px; }
        QDialog#prismProfileDialog QPushButton { background: palette(button); color: palette(button-text); border: 1px solid palette(mid); border-radius: 9px; padding: 8px 14px; }
        QDialog#prismProfileDialog QPushButton:hover { border-color: palette(highlight); }
        QDialog#prismProfileDialog QPushButton:focus { border: 2px solid palette(highlight); }
        QDialog#prismProfileDialog QPushButton:disabled { color: palette(placeholder-text); }
        QDialog#prismProfileDialog QPushButton#prismUseProfileButton { background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); font-weight: 600; }
        QDialog#prismProfileDialog QPushButton#prismUseProfileButton:disabled { background: palette(button); color: palette(placeholder-text); border-color: palette(mid); }
    )"));

    connect(m_profilePath, &QComboBox::editTextChanged, this, &PrismProfileDialog::clearInspection);
    connect(m_profilePath, qOverload<int>(&QComboBox::activated), this, &PrismProfileDialog::inspectProfile);
    connect(m_inspect, &QPushButton::clicked, this, &PrismProfileDialog::inspectProfile);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, tr("Choose Prism data folder"), m_profilePath->currentText());
        if (!path.isEmpty()) {
            m_profilePath->setEditText(QDir::toNativeSeparators(path));
            inspectProfile();
        }
    });
    connect(m_use, &QPushButton::clicked, this, &PrismProfileDialog::useProfile);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    if (initialSource.isEmpty()) {
        for (const auto& path : ChromaProfile::detectedRoots())
            m_profilePath->addItem(QDir::toNativeSeparators(path));
    } else {
        m_profilePath->addItem(QDir::toNativeSeparators(initialSource));
    }
    clearInspection();
    if (!m_profilePath->currentText().isEmpty()) {
        inspectProfile();
    } else {
        m_status->setText(tr("No Prism folder was found automatically. Browse to your Prism data folder to continue."));
    }
}

void PrismProfileDialog::clearInspection()
{
    m_inspectedPath.clear();
    m_selectedProfilePath.clear();
    m_use->setEnabled(false);
    m_inspect->setEnabled(!m_profilePath->currentText().trimmed().isEmpty());
    m_summary->setText(tr("Inspect a folder to see the library it contains."));
    m_status->setText(tr("Choose a folder, then select Inspect."));
}

void PrismProfileDialog::inspectProfile()
{
    clearInspection();
    const auto profile = ChromaProfile::inspect(m_profilePath->currentText().trimmed());
    if (!profile.error.isEmpty()) {
        m_status->setText(profile.error);
        return;
    }
    m_summary->setText(tr("%n instance(s)", nullptr, profile.instanceCount) + "\n\n" +
                       tr("Instance folder: %1").arg(QDir::toNativeSeparators(profile.instancesPath)));
    if (ChromaProfile::sameProfilePath(profile.root, APPLICATION->dataRoot())) {
        m_status->setText(tr("Chroma is already using this folder."));
        return;
    }
    m_inspectedPath = profile.root;
    m_status->setText(tr("This folder is ready to use."));
    m_use->setEnabled(true);
}

void PrismProfileDialog::useProfile()
{
    // Reinspect when accepted so a folder removed or changed since inspection is not selected blindly.
    inspectProfile();
    if (!m_use->isEnabled() || m_inspectedPath.isEmpty())
        return;
    m_selectedProfilePath = m_inspectedPath;
    accept();
}

bool PrismProfileDialog::eventFilter(QObject* watched, QEvent* event)
{
    // Enter in the path field inspects it without activating the default Use button.
    if (watched == m_profilePath->lineEdit() && event->type() == QEvent::KeyPress) {
        const auto key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            inspectProfile();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
