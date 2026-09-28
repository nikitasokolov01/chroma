/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "PageDialog.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include "Application.h"

#include "ui/widgets/PageContainer.h"

PageDialog::PageDialog(BasePageProvider* pageProvider, QString defaultId, QWidget* parent) : QDialog(parent)
{
    setObjectName("pageDialog");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setWindowTitle(pageProvider->dialogTitle());
    m_container = new PageContainer(pageProvider, std::move(defaultId), this);

    auto* mainLayout = new QVBoxLayout(this);

    auto* focusStealer = new QPushButton(this);
    mainLayout->addWidget(focusStealer);
    focusStealer->setDefault(true);
    focusStealer->hide();

    mainLayout->addWidget(m_container);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setLayout(mainLayout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Help | QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->setObjectName("pageDialogButtons");
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&Save"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName("savePageButton");
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("&Cancel"));
    buttons->button(QDialogButtonBox::Help)->setText(tr("Help"));
    buttons->setContentsMargins(0, 0, 0, 0);
    m_container->addButtons(buttons);

    connect(buttons->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, &PageDialog::accept);
    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &PageDialog::reject);
    connect(buttons->button(QDialogButtonBox::Help), &QPushButton::clicked, m_container, &PageContainer::help);

    if (!restoreGeometry(QByteArray::fromBase64(APPLICATION->settings()->get("PagedGeometry").toString().toUtf8()))) {
        if (auto activeScreen = parent ? parent->screen() : QApplication::primaryScreen()) {
            const auto available = activeScreen->availableSize();
            resize(qMin(960, available.width() - 60), qMin(740, available.height() - 70));
        }
    }
}

void PageDialog::accept()
{
    if (handleClose())
        QDialog::accept();
}

bool PageDialog::selectPage(const QString& id)
{
    return m_container->selectPage(id);
}

void PageDialog::closeEvent(QCloseEvent* event)
{
    if (handleClose())
        QDialog::closeEvent(event);
    else
        event->ignore();
}

bool PageDialog::handleClose()
{
    qDebug() << "Paged dialog close requested";
    if (!m_container->prepareToClose())
        return false;

    qDebug() << "Paged dialog close approved";
    APPLICATION->settings()->set("PagedGeometry", QString::fromUtf8(saveGeometry().toBase64()));
    qDebug() << "Paged dialog geometry saved";

    emit applied();
    return true;
}
