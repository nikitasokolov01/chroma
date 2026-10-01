// SPDX-License-Identifier: GPL-3.0-only
#include "SkinApplyTask.h"

#include <QBuffer>
#include <QFile>

#include "Application.h"
#include "minecraft/auth/Parsers.h"
#include "minecraft/skins/CapeChange.h"
#include "minecraft/skins/SkinTextureDocument.h"
#include "minecraft/skins/SkinUpload.h"
#include "net/NetJob.h"

SkinApplyTask::SkinApplyTask(MinecraftAccountPtr account, QString path, SkinModel::Model model, std::optional<QString> cape)
    : Task(false), m_account(account), m_path(path), m_model(model), m_cape(cape)
{
    setObjectName("Apply Minecraft skin");
}

shared_qobject_ptr<SkinApplyTask> SkinApplyTask::forCape(MinecraftAccountPtr account,
                                                         QString cape,
                                                         shared_qobject_ptr<QNetworkAccessManager> network)
{
    auto task = makeShared<SkinApplyTask>(std::move(account), QString(), SkinModel::CLASSIC, std::move(cape));
    task->m_capeOnly = true;
    task->m_capeNetwork = std::move(network);
    task->setObjectName("Apply Minecraft cape");
    return task;
}

void SkinApplyTask::executeTask()
{
    if (!m_account || m_account->accountType() == AccountType::Offline || !m_account->hasProfile()) {
        emitFailed(m_capeOnly ? tr("Select a Microsoft account with a Minecraft Java profile to change its cape.")
                              : tr("Select a Microsoft account with a Minecraft Java profile to apply this skin."));
        return;
    }
    if (m_account->isActive() || m_account->isInUse()) {
        emitFailed(
            m_capeOnly
                ? tr("Close Minecraft and wait for account sign-in to finish before changing a cape.")
                : tr("Close Minecraft and wait for account sign-in to finish before applying a skin. Your edited skin is saved locally."));
        return;
    }
    if (m_cape && !m_cape->isEmpty() && !m_account->accountData()->minecraftProfile.capes.contains(*m_cape)) {
        emitFailed(tr("Choose a cape owned by the selected Minecraft account."));
        return;
    }
    if (m_capeOnly) {
        if (m_account->shouldRefresh()) {
            setStatus(tr("Refreshing Minecraft sign-in…"));
            runStep(m_account->refresh().staticCast<Task>(), [this] { applyCape(); });
        } else
            applyCape();
        return;
    }
    QString error;
    const auto texture = SkinTextureDocument::readPng(m_path, &error);
    if (texture.isNull()) {
        emitFailed(error);
        return;
    }
    QBuffer snapshot(&m_png);
    snapshot.open(QIODevice::WriteOnly);
    if (!texture.save(&snapshot, "PNG")) {
        emitFailed(tr("The skin could not be prepared for upload. Export it again, then retry."));
        return;
    }
    if (m_account->shouldRefresh()) {
        setStatus(tr("Refreshing Minecraft sign-in…"));
        runStep(m_account->refresh().staticCast<Task>(), [this] { upload(); });
    } else {
        upload();
    }
}

void SkinApplyTask::runStep(Task::Ptr task, std::function<void()> success)
{
    m_step = task;
    m_steps.append(task);
    connect(task.get(), &Task::status, this, &Task::setStatus);
    connect(task.get(), &Task::progress, this, &Task::setProgress);
    connect(task.get(), &Task::succeeded, this, [this, success] {
        if (isRunning())
            success();
    });
    connect(task.get(), &Task::failed, this, [this](const QString&) {
        // Do not surface authentication service response bodies or credentials.
        emitFailed(m_capeOnly ? tr("Minecraft sign-in could not be refreshed. Refresh this account in Accounts, then retry.")
                              : tr("Minecraft sign-in could not be refreshed. Refresh this account in Accounts, then retry. Your skin is "
                                   "saved locally."));
    });
    connect(task.get(), &Task::aborted, this, [this] { emitAborted(); });
    task->start();
}

void SkinApplyTask::upload()
{
    if (m_account->isInUse() || m_account->accessToken().isEmpty()) {
        emitFailed(tr("The account is in use or needs sign-in. Refresh the account after closing Minecraft, then retry."));
        return;
    }
    setStatus(tr("Uploading skin…"));
    auto request = SkinUpload::makeBytes(m_account->accessToken(), m_png, m_model == SkinModel::SLIM ? "slim" : "classic");
    request->setNetwork(APPLICATION->network());
    m_step = request.staticCast<Task>();
    m_steps.append(m_step);
    connect(request.get(), &Task::progress, this, &Task::setProgress);
    connect(request.get(), &Task::aborted, this, [this] { emitAborted(); });
    connect(request.get(), &Task::failed, this, [this, request = request.get()](const QString&) {
        QString reason;
        switch (request->replyStatusCode()) {
            case 401:
                reason = tr("Minecraft sign-in has expired. Refresh this account and try again.");
                break;
            case 403:
                reason = tr("Minecraft Services refused the change. Check that this account can use Minecraft Java skins.");
                break;
            case 429:
                reason = tr("Minecraft Services received too many requests. Wait a little, then retry.");
                break;
            case 400:
                reason = tr("Minecraft Services rejected this skin. Check the PNG and Classic/Slim selection.");
                break;
            default:
                reason = tr("The skin could not be uploaded. Check your connection and retry.");
                break;
        }
        emitFailed(reason + " " + tr("Your edited skin is saved locally."));
    });
    connect(request.get(), &Task::succeeded, this, [this, request = request.get()] {
        auto response = request->response();
        MinecraftProfile profile;
        auto& current = m_account->accountData()->minecraftProfile;
        if (Parsers::parseMinecraftProfile(response, profile) && profile.id == current.id) {
            // Preserve cached cape textures, which are not in the API response.
            for (auto it = profile.capes.begin(); it != profile.capes.end(); ++it)
                it->data = current.capes.value(it.key()).data;
            current = profile;
        } else {
            current.skin.id.clear();
            current.skin.url.clear();
        }
        current.skin.data = m_png;
        current.skin.variant = m_model == SkinModel::SLIM ? "SLIM" : "CLASSIC";
        emit m_account->changed();
        applyCape();
    });
    request->start();
}

void SkinApplyTask::applyCape()
{
    if (!m_cape || *m_cape == m_account->accountData()->minecraftProfile.currentCape) {
        emitSucceeded();
        return;
    }
    if (m_account->isInUse() || m_account->accessToken().isEmpty()) {
        emitFailed(tr("Refresh this account after closing Minecraft, then retry the cape change."));
        return;
    }
    if (!m_cape->isEmpty() && !m_account->accountData()->minecraftProfile.capes.contains(*m_cape)) {
        emitFailed(tr("This cape is no longer listed on the selected Minecraft account."));
        return;
    }
    setStatus(tr("Updating cape…"));
    auto request = CapeChange::make(m_account->accessToken(), *m_cape);
    request->setNetwork(m_capeNetwork ? m_capeNetwork : APPLICATION->network());
    m_step = request.staticCast<Task>();
    m_steps.append(m_step);
    connect(request.get(), &Task::succeeded, this, [this] {
        m_account->accountData()->minecraftProfile.currentCape = *m_cape;
        emit m_account->changed();
        emitSucceeded();
    });
    connect(request.get(), &Task::failed, this, [this](const QString&) {
        emitFailed(m_capeOnly ? tr("The cape change failed. Check your connection and refresh the account, then retry.")
                              : tr("Your skin was applied, but the cape change failed. Check your connection and retry the cape change."));
    });
    connect(request.get(), &Task::aborted, this, [this] { emitAborted(); });
    request->start();
}

bool SkinApplyTask::abort()
{
    // Do not abandon an authentication refresh or leave an upload ambiguous.
    // Task deliberately remains non-abortable for this short mutation.
    return false;
}
