// SPDX-License-Identifier: GPL-3.0-only
#include "UpdateNotice.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "updater/ChromaUpdater.h"

namespace {
QString summarizeNotes(const QString& markdown)
{
    QStringList bullets;
    QStringList paragraphs;
    static const QRegularExpression bullet("^(?:[-*+] |[0-9]+[.)] )");
    static const QRegularExpression link("\\[([^\\]]+)\\]\\([^)]*\\)");
    for (const auto& raw : markdown.left(16 * 1024).split('\n')) {
        auto line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('!') || line.startsWith('<') || line.startsWith("```"))
            continue;
        const bool isBullet = bullet.match(line).hasMatch();
        line.remove(bullet);
        line.replace(link, "\\1");
        line.remove('`');
        line.remove('*');
        line = line.simplified();
        if (line.isEmpty())
            continue;
        if (isBullet && bullets.size() < 2)
            bullets.append(line);
        else if (!isBullet && paragraphs.size() < 1)
            paragraphs.append(line);
        if (bullets.size() == 2)
            break;
    }
    auto summary = (bullets.isEmpty() ? paragraphs : bullets).join(QStringLiteral(" · "));
    if (summary.size() > 180)
        summary = summary.left(177).trimmed() + QChar(0x2026);
    return summary;
}
}  // namespace

UpdateNotice::UpdateNotice(QWidget* parent) : QFrame(parent)
{
    setObjectName("chromaUpdateNotice");
    setAccessibleName(tr("Chroma update available"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(14, 10, 14, 10);
    m_layout->setSpacing(12);
    auto* text = new QWidget(this);
    text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(3);
    m_title = new QLabel(text);
    m_title->setObjectName("chromaUpdateNoticeTitle");
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_summary = new QLabel(text);
    m_summary->setObjectName("chromaUpdateNoticeSummary");
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    textLayout->addWidget(m_title);
    textLayout->addWidget(m_summary);
    m_layout->addWidget(text, 1);
    auto* actions = new QWidget(this);
    auto* actionLayout = new QHBoxLayout(actions);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(8);
    m_review = new QPushButton(tr("What’s new"), actions);
    m_review->setObjectName("chromaUpdateWhatsNew");
    m_review->setAccessibleName(tr("Review update features and download options"));
    m_review->setToolTip(tr("Review the new features. Your current page and unsaved work stay open."));
    m_later = new QPushButton(tr("Later"), actions);
    m_later->setObjectName("chromaUpdateLater");
    m_later->setToolTip(tr("Hide this release notice for this session. Check for updates in the launcher menu to see it again."));
    for (auto* button : { m_review, m_later }) {
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(34);
        actionLayout->addWidget(button);
    }
    m_layout->addWidget(actions, 0, Qt::AlignVCenter);
    // Palette-based colors follow both clay themes and the native theme.
    setStyleSheet(QStringLiteral(R"(
        QFrame#chromaUpdateNotice {
            background: palette(alternate-base); border: 1px solid palette(highlight);
            border-left: 4px solid palette(highlight); border-radius: 12px;
        }
        QLabel#chromaUpdateNoticeTitle { font-weight: 700; font-size: 14px; background: transparent; }
        QLabel#chromaUpdateNoticeSummary { font-size: 12px; background: transparent; color: palette(text); }
        QPushButton#chromaUpdateWhatsNew, QPushButton#chromaUpdateLater {
            min-height: 22px; max-height: 22px; padding: 4px 10px;
            border: 1px solid palette(mid); border-radius: 9px; font-size: 12px;
        }
        QPushButton#chromaUpdateWhatsNew { background: palette(highlight); color: palette(highlighted-text); font-weight: 700; }
        QPushButton#chromaUpdateLater { background: palette(button); color: palette(button-text); }
        QPushButton#chromaUpdateWhatsNew:focus, QPushButton#chromaUpdateLater:focus { border: 2px solid palette(text); }
    )"));
    connect(m_review, &QPushButton::clicked, this, [this] {
        if (m_updater)
            m_updater->showAvailableUpdate();
    });
    connect(m_later, &QPushButton::clicked, this, [this] {
        if (m_updater)
            m_updater->dismissAvailableUpdate();
    });
    hide();
}

void UpdateNotice::setUpdater(ChromaUpdater* updater)
{
    if (m_updater)
        disconnect(m_updater, nullptr, this, nullptr);
    m_updater = updater;
    if (m_updater) {
        connect(m_updater, &ChromaUpdater::availableUpdateChanged, this, &UpdateNotice::refresh);
        connect(m_updater, &QObject::destroyed, this, [this] {
            m_updater.clear();
            refresh();
        });
    }
    refresh();
}

void UpdateNotice::refresh()
{
    if (!m_updater || !m_updater->shouldShowUpdateNotice()) {
        hide();
        return;
    }
    m_title->setText(tr("Chroma %1 is available%2")
                         .arg(m_updater->availableVersion(), m_updater->availableIsPrerelease() ? tr(" · Preview") : QString()));
    const auto summary = summarizeNotes(m_updater->availableNotes());
    m_summary->setText(summary.isEmpty() ? tr("New features and improvements are ready to explore.") : summary);
    show();
}

void UpdateNotice::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    m_layout->setDirection(width() < 480 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}
